#include "stdafx.h"
#include <thread>
#include <afpEvents.h>

#include "afp_volumes.h"
#include "fpinfo.h"
#include "afpdebug.h"
#include "afpServer.h"
#include "afp_registry.h"
#include "fdparms.h"
#include "afphelp.h"

std::unique_ptr<afp_volumes> g_volumes = std::make_unique<afp_volumes>();
const std::wstring parent_directory_name = L"Parent";

afp_volumes::afp_volumes()
    : next_volume_id_(-1)
    , index_queue_running_(true)
{
    index_event_ = CreateEvent(nullptr, TRUE, TRUE, nullptr);

    std::thread indexer(&afp_volumes::volume_indexer, this);
    indexer.detach();

    std::thread queued_worker(&afp_volumes::index_queue_thread, this);
    queued_worker.detach();
}

afp_volumes::~afp_volumes() = default;

afp_volumes* afp_volumes::current()
{
    return g_volumes.get();
}

void afp_volumes::shutdown()
{
    current()->instance_shutdown();
}

void afp_volumes::instance_shutdown()
{
    index_queue_running_ = false;
    SetEvent(index_event_);

    remove_all_volumes();
}

void afp_volumes::volume_indexer()
{
    if (afp_registry registry; registry.open_volume_list_reg_key())
    {
        BOOL keep_going;

        do
        {
            afp_clear_text_password password{};
            WCHAR share_path[MAX_PATH];
            WCHAR share_name[33];
            DWORD flags = 0;

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
                std::filesystem::path path = share_path;
                add_volume(share_name, path, flags);
            }
        } while (keep_going);
    }
}

void afp_volumes::add_volume(const std::wstring& share_name, std::filesystem::path& share_path, UINT32 flags)
{
    WIN32_FILE_ATTRIBUTE_DATA win_attributes;

    if (!GetFileAttributesEx(share_path.c_str(), GetFileExInfoStandard, &win_attributes))
    {
        DBGWRITE(dbg_level_error, L"Failed to get attributes for volume: %s\n", share_path.c_str());
        return;
    }

    const auto new_volume = std::make_shared<afp_volume>(share_name, share_path, next_volume_id_--, static_cast<BYTE>(0), flags);
    new_volume->set_create_date(win_attributes.ftCreationTime);
    new_volume->set_modified_date(win_attributes.ftLastWriteTime);
    new_volume->set_backup_date(default_backup_time);

    const auto new_index = new_volume->get_index();

    // Add parent of root.
    const std::filesystem::path parent_path = share_path.parent_path();
    std::wstring parent_of_root_name = parent_path.filename();
    if (parent_of_root_name.empty())
    {
        parent_of_root_name = get_root_drive_of_file_path(parent_path);
    }

    new_index->add_entry(kNoParent, parent_of_root_name, share_path.parent_path(), true);

    // Add root directory.
    const auto root_index = new_index->add_entry(kParentOfRoot, share_name, share_path, true);

    // Create the Afp_AfpInfo stream that holds FinderInfo for the root dir.
    create_finder_info_stream(root_index.get());

    std::filesystem::path create_path = share_path;
    create_path.concat(":");
    create_path.concat(afp_info_stream_name);

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
        new_volume->set_internal_flags(flag_volume_shared);

        std::lock_guard guard(volumes_mutex_);
        volumes_.push_back(new_volume);
    }
}

void afp_volumes::remove_volume(const INT16 id)
{
    const auto volume = find(id);
    if (volume == nullptr)
    {
        ASSERT(0);
        return;
    }

    std::lock_guard guard(volumes_mutex_);
    const std::vector<std::shared_ptr<afp_volume>>::const_iterator it = std::ranges::find(volumes_, volume);
    if (it == volumes_.end())
    {
        // Raced with another removal; nothing left to do.
        return;
    }

    volume->clear_internal_flags(flag_volume_shared);

    volumes_.erase(it);
}

void afp_volumes::remove_all_volumes()
{
    std::vector<std::shared_ptr<afp_volume>> to_stop;
    {
        std::lock_guard guard(volumes_mutex_);
        to_stop = volumes_;
        volumes_.clear();
    }

    for (const auto& volume : to_stop)
    {
        volume->stop_sharing();
    }
}


bool afp_volumes::index_build(std::filesystem::path& share_path, const INT32  parent_dir_id, const std::shared_ptr<afp_volume>& volume)
{
    if (parent_dir_id == kRootDirID)
    {
        DBGWRITE(dbg_level_info, L"Begin indexing share path: %s\n", share_path.c_str());
    }

    WIN32_FIND_DATA find_data;
    const std::filesystem::path find_path = share_path / "*";
    const auto h_find = FindFirstFile(find_path.c_str(), &find_data);
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

        if (should_ignore_object(file_name))
        {
            DBGWRITE(dbg_level_trace, L"Ignore object %s while indexing.\n", file_name.c_str());
            continue;
        }

        const auto is_directory = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

        // Form path to new file or directory.
        std::filesystem::path item_path = share_path / file_name;

        INT32 new_item_id;
        const auto entry = volume->get_index()->add_entry(
            parent_dir_id,
            file_name,
            item_path,
            is_directory,
            nullptr, // null will force the index to read it from file stream.
            &new_item_id);

        if (entry == nullptr)
        {
            DBGWRITE(dbg_level_error, L"Failed to add entry to index: %s\n", item_path.c_str());

            continue;
        }

        create_finder_info_stream(entry.get());

        if (is_directory)
        {
            // Make a recursive call to add dirs and files in the new directory
            // to the index. So we don't run out of stack, we post a new
            // worker element to a worker thread that will call this function again.

            index_queue_task(task_index_directory, volume, new_item_id, item_path);

            continue;
        }

        afp_check_for_and_create_rsrc_stream(entry.get());

    } while (FindNextFile(h_find, &find_data));

    FindClose(h_find);

    if (parent_dir_id == kRootDirID)
    {
        index_queue_task(task_index_complete, volume, kRootDirID, share_path);
    }

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

        for (;;)
        {
            // The empty() check must be inside the lock; producers push
            // under the same mutex.
            std::unique_ptr<index_queue_element> element;
            {
                std::unique_lock lock(index_queue_mutex_);

                if (index_queue_.empty())
                {
                    break;
                }

                element = std::move(index_queue_.front());
                index_queue_.pop();
            }

            switch(element->task)
            {
            case task_index_directory:
                {
                    index_build(element->path, element->item_id, element->volume);
                    break;
                }

            case task_index_complete:
                {
                    auto flags = element->volume->get_internal_flags();
                    flags |= flag_volume_indexing_complete;
                    element->volume->set_internal_flags(flags);

                    EventWriteAFPServerEvt_VolIndexingComplete(element->volume->get_share_name().c_str());
                    SendMessage(HWND_BROADCAST, AFPWM_INDEXING_COMPLETE, NULL, NULL);
                    DBGWRITE(dbg_level_info, L"Indexing complete for volume: %s\n", element->volume->get_share_name().c_str());
                    break;
                }
            case task_index_remove_dir:
                {
                    const auto volume_index = element->volume->get_index();
                    volume_index->remove_entry(element->item_id);
                    break;
                }
            case task_index_update_parent:
                {
                    const auto volume_index = element->volume->get_index();
                    volume_index->update_entry(element->item_id, element->new_parent_id);
                    break;
                }
            }
        }
    }
}


void afp_volumes::index_queue_task(
    const index_queue_tasks task,
    const std::shared_ptr<afp_volume>& volume,
    const INT32 item_id,
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

void afp_volumes::index_queue_task(
    const index_queue_tasks task,
    const std::shared_ptr<afp_volume>& volume,
    const INT32 item_id,
    const INT32 new_parent_id)
{
    auto element = std::make_unique<index_queue_element>();
    element->task = task;
    element->volume = volume;
    element->item_id = item_id;
    element->new_parent_id = new_parent_id;

    {
        std::lock_guard lock(index_queue_mutex_);
        index_queue_.push(std::move(element));
    }

    SetEvent(index_event_);
}


std::shared_ptr<afp_volume> afp_volumes::find(const std::wstring& name)
{
    std::lock_guard guard(volumes_mutex_);

    const auto it = std::ranges::find_if(volumes_, [=](const std::shared_ptr<afp_volume>& v)
    {
        return v->get_share_name() == name;
    });

    if (it != volumes_.end())
    {
        return *it;
    }

    return nullptr;
}


std::shared_ptr<afp_volume> afp_volumes::find(const INT16 id)
{
    std::lock_guard guard(volumes_mutex_);

    const auto it = std::ranges::find_if(volumes_, [=](const std::shared_ptr<afp_volume>& v)
        {
            return v->get_volume_id() == id;
        });

    if (it != volumes_.end())
    {
        return *it;
    }

    return nullptr;
}


bool afp_volumes::map_name_to_id(const std::wstring& share_name, INT16* id)
{
    const auto volume = find(share_name);
    if (volume != nullptr)
    {
        *id = volume->get_volume_id();
        return true;
    }

    return false;
}


UINT8 afp_volumes::active_volumes(std::vector<std::shared_ptr<afp_volume>>* volumes_out)
{
    std::lock_guard guard(volumes_mutex_);

    const auto it = std::ranges::count_if(volumes_, [=](const std::shared_ptr<afp_volume>& v)
    {
        if ((v->get_internal_flags() & flag_volume_shared) != 0)
        {
            if (volumes_out != nullptr)
            {
                volumes_out->push_back(v);
            }

            return true;
        }

        return false;
    });

    return static_cast<UINT8>(it);
}


void afp_volumes::dirty_volumes(std::vector<std::shared_ptr<afp_volume>>* dirty_volumes)
{
    std::lock_guard guard(volumes_mutex_);

    for (const auto& v : volumes_)
    {
        if (v->is_dirty())
        {
            dirty_volumes->push_back(v);
        }
    }
}
