#include "stdafx.h"
#include <thread>

#include "afp_volumes.h"
#include "fpinfo.h"
#include "afpdebug.h"
#include "afpnotify.h"
#include "afp_registry.h"

const auto g_volumes = std::make_unique<afp_volumes>();
const std::wstring parent_directory_name = L"Parent";

afp_volumes::afp_volumes()
    : next_volume_id_(MAXWORD)
{
    std::thread indexer(&afp_volumes::volume_indexer, this);
    indexer.detach();
}

afp_volumes::~afp_volumes()
{
}

void afp_volumes::volume_indexer()
{
    if (afp_registry registry; registry.open_volume_list_reg_key())
    {
        BOOL keep_going;

        do
        {
            PassWord password;
            WCHAR share_path[MAX_PATH];
            WCHAR share_name[33];
            DWORD flags;

            keep_going = registry.next_volume(
                share_name,
                sizeof(share_name),
                share_path,
                sizeof(share_path),
                reinterpret_cast<CHAR*>(password),
                sizeof(password),
                &flags);

            if (keep_going)
            {
                add_volume(share_name, share_path, flags);
            }
        } while (keep_going);
    }
}

void afp_volumes::add_volume(const std::wstring& share_name, const std::filesystem::path& share_path, UINT32 flags)
{
    WIN32_FILE_ATTRIBUTE_DATA win_attributes;

    if (!GetFileAttributesEx(share_path.c_str(), GetFileExInfoStandard, &win_attributes))
    {
        DBGWRITE(dbg_level_error, L"Failed to get attributes for volume: %s\n", share_path.c_str());
        return;
    }

    const auto new_volume = std::make_unique<afp_volume>(share_name, share_path, next_volume_id_--, 0, flags);
    new_volume->set_create_date(win_attributes.ftCreationTime);
    new_volume->set_modified_date(win_attributes.ftLastWriteTime);

    const auto new_index = new_volume->get_index();

    // Add parent of root.
    new_index->add_entry(-1, parent_directory_name, share_path.parent_path(), true);

    // Add root directory.
    const auto root_index = new_index->add_entry(kParentOfRoot, share_name, share_path, true);

    // Create the Afp_AfpInfo stream that holds FinderInfo for the root dir.
    AFPCheckForAndAddFinderInfoStream(root_index);

    std::filesystem::path create_path = share_path;
    create_path.concat(":");
    create_path.concat(AFP_INFO_STREAM_NAME);

    const HANDLE h_file = CreateFile(
        create_path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (h_file == INVALID_HANDLE_VALUE)
    {
        // If the file already exists, that's okay, just ignore the error.
        if (const auto error = GetLastError(); error != ERROR_FILE_EXISTS)
        {
            DBGWRITE(dbg_level_error, L"CreateFile() for info fork failed in AddVolume! (%d)\n", error);
        }
    }
    else
    {
        CloseHandle(h_file);
    }

    if (index_volume(share_path, kRootDirID, new_volume.get()))
    {
        new_volume->set_internal_flag(flag_volume_shared);

        std::lock_guard guard(volumes_mutex_);
        volumes_.push_back(new_volume);

        std::thread change_notify_thread(afp_notify_thread, new_volume.get());
        change_notify_thread.detach();
    }
}


bool afp_volumes::index_volume(const std::filesystem::path& share_path, const UINT32  parent_dir_id, afp_volume* volume)
{
}


afp_volume* afp_volumes::find(const std::wstring& name)
{
    std::lock_guard guard(volumes_mutex_);

    const auto it = std::ranges::find_if(volumes_, [=](const std::unique_ptr<afp_volume>& i)
    {
        return i->get_share_name() == name;
    });

    if (it != volumes_.end())
    {
        return it->get();
    }

    return nullptr;
}


afp_volume* afp_volumes::find(const UINT16 id)
{
    std::lock_guard guard(volumes_mutex_);

    const auto it = std::ranges::find_if(volumes_, [=](const std::unique_ptr<afp_volume>& i)
        {
            return i->get_volume_id() == id;
        });

    if (it != volumes_.end())
    {
        return it->get();
    }

    return nullptr;
}
