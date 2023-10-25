#include "stdafx.h"
#include <thread>

#include "afp_volumes.h"

#include <afpEvents.h>

#include "fpinfo.h"
#include "afpdebug.h"
#include "afpnotify.h"
#include "afpServer.h"
#include "afp_registry.h"
#include "fdparms.h"

const auto g_volumes = std::make_unique<afp_volumes>();
const std::wstring parent_directory_name = L"Parent";

afp_volumes::afp_volumes()
    : next_volume_id_(MAXWORD)
    , index_queue_running_(true)
{
    index_event_ = CreateEvent(nullptr, TRUE, TRUE, nullptr);

    std::thread indexer(&afp_volumes::volume_indexer, this);
    indexer.detach();

    std::thread queued_worker(&afp_volumes::index_queue_thread, this);
    queued_worker.detach();
}

afp_volumes::~afp_volumes()
{
    index_queue_running_ = false;
    SetEvent(index_event_);
}

afp_volumes* afp_volumes::current()
{
    return g_volumes.get();
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

    auto new_volume = std::make_shared<afp_volume>(share_name, share_path, next_volume_id_--, 0, flags);
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

    if (index_build(share_path, kRootDirID, new_volume))
    {
        new_volume->set_internal_flag(flag_volume_shared);

        std::lock_guard guard(volumes_mutex_);
        volumes_.push_back(new_volume);

        std::thread change_notify_thread(afp_notify_thread, new_volume);
        change_notify_thread.detach();
    }
}

void afp_volumes::remove_volume(const UINT16 id)
{
    const auto volume = find(id);
    if (volume == nullptr)
    {
        ASSERT(0);
        return;
    }

    std::lock_guard guard(volumes_mutex_);
    const std::vector<std::shared_ptr<afp_volume>>::const_iterator it = std::ranges::find(volumes_, volume);

    auto flags = volume->get_internal_flags();
    flags &= ~flag_volume_shared;
    volume->set_internal_flag(flags);

    volumes_.erase(it);
}

void afp_volumes::remove_all_volumes()
{
    std::ranges::for_each(volumes_, [](const std::shared_ptr<afp_volume>& volume)
        {
            auto flags = volume->get_internal_flags();
            flags &= ~flag_volume_shared;
            volume->set_internal_flag(flags);
        });

    volumes_.clear();
}


bool afp_volumes::index_build(const std::filesystem::path& share_path, const UINT32  parent_dir_id, std::shared_ptr<afp_volume>& volume)
{
    if (parent_dir_id == kRootDirID)
    {
        DBGWRITE(dbg_level_info, L"Begin indexing share path: %s\n", share_path.c_str());
    }

    WIN32_FIND_DATA find_data;
    const auto h_find = FindFirstFile(share_path.c_str(), &find_data);
    if (h_find == INVALID_HANDLE_VALUE)
    {
        DBGWRITE(dbg_level_error, L"Bad directory, failed to index: %s\n", share_path.c_str());
        return false;
    }

    do
    {
        std::wstring file_name = find_data.cFileName;
        if (file_name == L"." || file_name == L"..")
        {
            continue;
        }

        const auto is_directory = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

        // Form path to new file or directory.
        std::filesystem::path item_path = share_path / file_name;

        UINT32 new_item_id;
        const auto entry = volume->get_index()->add_entry(
            parent_dir_id,
            file_name,
            item_path,
            is_directory,
            &new_item_id);

        if (entry == nullptr)
        {
            DBGWRITE(dbg_level_info, L"Failed to add entry to index: %s\n", item_path.c_str());
            ASSERT(0);

            continue;
        }

        // Now that we have our index entry, we need to get the char mapping
        // table for mac characters that didn't map to windows chars. Only
        // copy the mapping if the stream existed before we created it.
        if (AFPCheckForAndAddFinderInfoStream(entry))
        {
            AFPCheckForCharMap(entry);
        }

        if (is_directory)
        {
            // Make a recursive call to add dirs and files in the new directory
            // to the index. So we don't run out of stack, we post a new
            // worker element to a worker thread that will call this function again.

            index_queue_task(task_index_directory, volume, new_item_id, item_path);

            continue;
        }

        AFPCheckForAndCreateFinderRsrcStream(entry);

    } while (FindNextFile(h_find, &find_data));

    FindClose(h_find);

    return true;
}


void afp_volumes::index_queue_thread()
{
    while (index_queue_running_)
    {
        ResetEvent(index_event_);
        WaitForSingleObject(index_event_, INFINITE);

        if (!index_queue_running_)
        {
            CloseHandle(index_event_);
            break;
        }

        while (!index_queue_.empty())
        {
            std::unique_lock lock(index_queue_mutex_);

            const auto element = std::move(index_queue_.front());
            index_queue_.pop();

            lock.unlock();

            switch(element->task)
            {
            case task_delete_index_entry:
                element->volume->get_index()->remove_entry(element->item_id, true);
                break;

            case task_index_directory:
                index_build(element->path, element->item_id, element->volume);
                break;

            case task_index_complete:
                EventWriteAFPServerEvt_VolIndexingComplete(element->volume->get_share_name().c_str());
                SendMessage(HWND_BROADCAST, AFPWM_INDEXING_COMPLETE, static_cast<WPARAM>(NULL), static_cast<LPARAM>(NULL));
                DBGWRITE(dbg_level_info, L"Indexing complete for volume: %s\n", element->volume->get_share_name().c_str());
                break;
            }
        }
    }
}


void afp_volumes::index_queue_task(
    const index_queue_tasks task,
    const std::shared_ptr<afp_volume>& volume,
    const UINT32 item_id,
    std::filesystem::path& path)
{
    auto element = std::make_unique<index_queue_element>();
    element->task = task;
    element->volume = volume;
    element->item_id = item_id;
    element->path = std::move(path);

    {
        std::lock_guard lock(index_queue_mutex_);
        index_queue_.push(std::move(element));
    }

    SetEvent(index_event_);
}


std::shared_ptr<afp_volume> afp_volumes::find(const std::wstring& name)
{
    std::lock_guard guard(volumes_mutex_);

    const auto it = std::ranges::find_if(volumes_, [=](const std::shared_ptr<afp_volume>& i)
    {
        return i->get_share_name() == name;
    });

    if (it != volumes_.end())
    {
        return *it;
    }

    return nullptr;
}


std::shared_ptr<afp_volume> afp_volumes::find(const UINT16 id)
{
    std::lock_guard guard(volumes_mutex_);

    const auto it = std::ranges::find_if(volumes_, [=](const std::shared_ptr<afp_volume>& i)
        {
            return i->get_volume_id() == id;
        });

    if (it != volumes_.end())
    {
        return *it;
    }

    return nullptr;
}

bool afp_volumes::map_name_to_id(const std::wstring& share_name, UINT16* id)
{
    const auto volume = find(share_name);
    if (volume != nullptr)
    {
        *id = volume->get_volume_id();
        return true;
    }

    return false;
}

int afp_volumes::active_volumes()
{
    std::lock_guard guard(volumes_mutex_);

    const auto it = std::ranges::count_if(volumes_, [=](const std::shared_ptr<afp_volume>& i)
        {
            return (i->get_internal_flags() & flag_volume_shared) != 0;
        });

    return it;
}
