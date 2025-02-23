//===================================================================
// This unit contains the change notification mechanism that updates
// the afp server when a windows client changes files/folders on
// the afp volume.
//===================================================================

#include "stdafx.h"

#include "change_notify.h"
#include "afpdebug.h"
#include "afphelp.h"
#include "afp.h"
#include "afp_volumes.h"
#include "afp_volume.h"
#include "fdparms.h"
#include "fpinfo.h"

extern bool server_running;

change_notify::change_notify(const INT16 volume_id)
    : volume_id_(volume_id)
    , notify_suspended_(false)
    , notify_overlapped_({})
{
    result_buffer_ = std::make_unique<BYTE[]>(change_notify_result_buffer_size);

    std::thread change_notify_thread(&change_notify::afp_notify_thread, this);
    change_notify_thread.detach();
}

void change_notify::afp_notify_thread()
{
    DBGWRITE(dbg_level_trace, L"Notify thread started...\n");

    std::shared_ptr<afp_volume> volume;

    do
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        volume = afp_volumes::current()->find(volume_id_);

    } while (volume == nullptr);

    DBGWRITE(dbg_level_info, L"Change notify activating for %s\n", volume->get_share_path().c_str());

    const auto h_directory = CreateFile(
        volume->get_share_path().c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr
    );

    if (h_directory == INVALID_HANDLE_VALUE)
    {
        DBGWRITE(dbg_level_error,
                 L"Error opening directory at %s (%d)\n",
                 volume->get_share_path().c_str(),
                 GetLastError());
        return;
    }

    notify_overlapped_.hEvent = CreateEvent(nullptr, FALSE, TRUE, nullptr);
    if (notify_overlapped_.hEvent == nullptr)
    {
        ASSERT(0);
        return;
    }

    while (server_running && volume->get_internal_flags() & flag_volume_shared)
    {
        afp_read_dir_changes(volume, h_directory, result_buffer_.get());
        ZeroMemory(result_buffer_.get(), change_notify_result_buffer_size);
    }

    CloseHandle(notify_overlapped_.hEvent);
    CloseHandle(h_directory);

    DBGWRITE(dbg_level_info, L"Notify thread gone for %s\n", volume->get_share_name().c_str());
}


void change_notify::afp_read_dir_changes(
    IN const std::shared_ptr<afp_volume>& volume,
    IN const HANDLE h_directory,
    IN const PBYTE result_buffer
)
{
    constexpr DWORD notify_filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_CREATION;
    DWORD bytes_returned = 0;

    // This call will put the thread into a wait state until a change
    // happens to our directory.
    if (!ReadDirectoryChangesW(
        h_directory,
        result_buffer,
        change_notify_result_buffer_size,
        TRUE,
        notify_filter,
        &bytes_returned,
        &notify_overlapped_,
        nullptr))
    {
        DBGWRITE(dbg_level_error, L"ReadDirectoryChangesW() failed! (%d)\n", GetLastError());
        ASSERT(0);
        return;
    }

    if (const auto result = WaitForSingleObject(notify_overlapped_.hEvent, INFINITE); result != WAIT_OBJECT_0)
    {
        return;
    }

    if (notify_suspended_)
    {
        DBGWRITE(dbg_level_info, L"Change notify has been suspended.\n");
        return;
    }

    if (!GetOverlappedResult(h_directory, &notify_overlapped_, &bytes_returned, FALSE))
    {
        DBGWRITE(dbg_level_info, L"GetOverlappedResult() returned error (%d)\n", GetLastError());
        return;
    }

    // If the number of bytes transferred is 0, then the buffer we gave to
    // ReadDirectoryChangesW() was too small to hold the change data. We're
    // supposed to handle this now by enumerating the directory structure
    // manually.
    if (bytes_returned == 0)
    {
        DBGWRITE(dbg_level_info, L"0 bytes transferred in change! (%s)\n", volume->get_share_path().c_str());
        return;
    }

    auto p_info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(result_buffer);
    afp_index* index_entry = nullptr;
    
    // We have a successful change notify. Flip through the array of
    // changes and take appropriate action depending on the change.
    DBGWRITE(dbg_level_trace, L"Change notify data received!! (%d bytes)\n", bytes_returned);

    do
    {
        WCHAR file_name[MAX_PATH];

        switch (p_info->Action)
        {
        case FILE_ACTION_ADDED:
            afp_change_notify_add(p_info, volume);
            break;

        case FILE_ACTION_REMOVED:
            afp_change_notify_remove(p_info, volume);
            break;

        case FILE_ACTION_MODIFIED:
            break;

        // An object is being renamed. Here is where we are given a chance
        // to get a grip on the object before the rename actually happens.
        case FILE_ACTION_RENAMED_OLD_NAME:
            if (lstrcpyn(file_name, p_info->FileName, static_cast<int>(p_info->FileNameLength)) != nullptr)
            {
                DBGWRITE(dbg_level_trace, L"File renamed, old name = %s\n", file_name);
                index_entry = volume->find(kRootDirID, file_name);
            }
            break;

        // The object is now renamed. Update the afp index with the
        // newly changed name.
        case FILE_ACTION_RENAMED_NEW_NAME:
            if (lstrcpyn(file_name, p_info->FileName, static_cast<int>(p_info->FileNameLength)) != nullptr)
            {
                if (index_entry != nullptr)
                {
                    WCHAR new_name[64];

                    ZeroMemory(new_name, sizeof(new_name));
                    afp_get_object_name_from_path(file_name, new_name, sizeof(new_name));

                    DBGWRITE(dbg_level_trace, L"File renamed, new name = %s\n", new_name);

                    char_map_data char_map;
                    volume->get_index()->update_entry(index_entry, index_entry->par_entry, new_name, &char_map);
                    volume->set_is_dirty();
                }
            }
            break;

        default:
            ASSERT(0);
            break;
        }

        DBGWRITE(dbg_level_trace, L"NextEntryOffset = %d\n", p_info->NextEntryOffset);

        if (p_info->NextEntryOffset == 0)
        {
            break;
        }

        p_info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(reinterpret_cast<PBYTE>(p_info) + p_info->NextEntryOffset);
    } while (TRUE);
}


void change_notify::afp_change_notify_add(const FILE_NOTIFY_INFORMATION* file_notify_info, const std::shared_ptr<afp_volume>& volume)
{
    WCHAR new_item_pathname[MAX_PATH]{};
    WIN32_FILE_ATTRIBUTE_DATA attribute_data;

    if (lstrcpyn(new_item_pathname, file_notify_info->FileName, static_cast<int>(file_notify_info->FileNameLength)) == nullptr)
    {
        return;
    }

    // First step: we need to find the index entry that is the parent
    // directory of the new object we're adding.
    const std::filesystem::path new_item_path(new_item_pathname);
    const std::filesystem::path new_item_parent_path = new_item_path.parent_path();
    const std::filesystem::path new_item_name = new_item_path.filename();

    DBGWRITE(dbg_level_info, L"New item pathname = %s\n", new_item_pathname);

    // Search the afp index for the entry corresponding to the parent
    // directory for the new item.
    const auto parent_entry = volume->find(kRootDirID, new_item_parent_path);
    if (parent_entry == nullptr)
    {
        DBGWRITE(
            dbg_level_info,
            L"AFPIndexFindEntryWithPath() failed (%s)\n", new_item_parent_path.c_str());

        return;
    }

    DBGWRITE(dbg_level_info, L"Parent dir path = %s\n", parent_entry->sz_path.c_str());

    // Build full path to the new item.
    std::filesystem::path new_full_path = parent_entry->sz_path / new_item_name;

    // See if there is already an index entry for this object. If so, then we
    // don't need to do anything.
    if (volume->find(kRootDirID, new_item_pathname) != nullptr)
    {
        DBGWRITE(dbg_level_trace, L"Object already exists in index: %s\n", new_full_path.c_str());
        return;
    }

    // Get the file attributes for the new file object. We use these to
    // determine if the object is a file or directory.
    if (!GetFileAttributesEx(new_full_path.c_str(), GetFileExInfoStandard, &attribute_data))
    {
        DBGWRITE(dbg_level_info, L"Item not found on disk! (%s)\n", new_full_path.c_str());
        return;
    }

    // Finally, add the new file object into the afp index.
    INT32 new_object_id;
    const char_map_data char_map;
    const auto new_entry = volume->get_index()->add_entry(
        parent_entry->object_id,
        new_item_name,
        new_full_path,
        (attribute_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0,
        &char_map,
        &new_object_id);

    DBGWRITE(
        dbg_level_trace,
        L"Added new item in notify id=%d name=%s\n",
        new_object_id,
        new_item_pathname);

    // Add the finder info stream to the file or directory if
    // it doesn't already exist.
    create_finder_info_stream(new_entry);

    if (attribute_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
        // If the item is a directory, we add all of its contents
        // to the volume index as well.
        afp_volumes::current()->index_queue_task(task_index_directory, volume, new_object_id, new_full_path);
    }
    else
    {
        // Add the resource streams if necessary for files.
        afp_check_for_and_create_rsrc_stream(new_entry);
    }

    // Mark the volume dirty so it will tell any connected macs
    // that the volume contents have changed.
    volume->set_is_dirty();
}


void change_notify::afp_change_notify_remove(
    IN const FILE_NOTIFY_INFORMATION* file_notify_info,
    IN const std::shared_ptr<afp_volume>& volume
)
{
    WCHAR remove_item_pathname[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA attribute_data;

    // Copy the pathname (path plus object name) into a null terminated
    // string buffer for later use.
    if (lstrcpyn(remove_item_pathname, file_notify_info->FileName, static_cast<int>(file_notify_info->FileNameLength)) == nullptr)
    {
        return;
    }

    DBGWRITE(dbg_level_info, L"Removing %s from index.\n", remove_item_pathname);

    // Search the afp index for the entry corresponding to the removed object.
    const auto entry = volume->get_index()->find(kRootDirID, remove_item_pathname);
    if (entry == nullptr)
    {
        // If we fail to find the parent directory's index entry, there's no
        // reason to continue.
        DBGWRITE(dbg_level_error, L"AFPIndexFindEntryWithPath() failed.\n");
        return;
    }

    // Now, we have to make sure the object doesn't exist on the disk. We do this
    // in case a new object with same name was created immediately after the old
    // one was deleted. This normally can only happen by batch file or stress
    // applications.
    if (GetFileAttributesEx(entry->sz_path.c_str(), GetFileExInfoStandard, &attribute_data))
    {
        DBGWRITE(dbg_level_info, L"Object still exists on disk! Leaving in index.\n");
        return;
    }

    // Now, make the call into the index utility routines to actually remove
    // the item from the index list.
    volume->get_index()->remove_entry(entry);

    // Tell the attention mechanism to notify clients the volume has changed.
    volume->set_is_dirty();
}
