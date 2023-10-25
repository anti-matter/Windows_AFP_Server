//===================================================================
// This unit contains the change notification mechanism that updates
// the afp server when a windows client changes files/folders on
// the afp volume.
//===================================================================

#include "stdafx.h"

#include "afpnotify.h"
#include "afpdebug.h"
#include "afphelp.h"
#include "afp.h"
#include "afp_volumes.h"
#include "volumes.h"
#include "fdparms.h"
#include "fpinfo.h"

constexpr UINT32 change_notify_result_buffer_size = 64 * 1024;

extern bool server_running;
extern std::unique_ptr<afp_volumes> g_volumes;

void afp_notify_thread(
    IN const std::shared_ptr<afp_volume>& param
)
{
    auto volume = param;
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
        DBGWRITE(dbg_level_info,
                 L"Error opening directory at %s (%d)\n",
                 volume->get_share_path().c_str(),
                 GetLastError());
        return;
    }

    // Create the buffer that will hold the return data from the notify call.
    const auto result_buffer = std::make_unique<BYTE[]>(change_notify_result_buffer_size);
    if (result_buffer == nullptr)
    {
        DBGWRITE(dbg_level_info,
                 L"Error allocating result buffer for %s (%d)\n",
                 volume->get_share_path().c_str(),
                 GetLastError());

        CloseHandle(h_directory);
        return;
    }

    while (server_running && volume->get_internal_flags() & flag_volume_shared)
    {
        afp_read_dir_changes(volume, h_directory, result_buffer.get());
        ZeroMemory(result_buffer.get(), change_notify_result_buffer_size);
    }

    CloseHandle(h_directory);

    DBGWRITE(dbg_level_info, L"Notify thread gone for %s\n", volume->get_share_name().c_str());
    return;
}


void afp_read_dir_changes(
    IN std::shared_ptr<afp_volume>& volume,
    IN const HANDLE h_directory,
    IN const PBYTE result_buffer
)
{
    constexpr DWORD notify_filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME;
    DWORD bytes_returned = 0;
    OVERLAPPED overlapped;

    overlapped.hEvent = CreateEvent(nullptr, FALSE, TRUE, nullptr);
    if (overlapped.hEvent == nullptr)
    {
        ASSERT(0);
        return;
    }

    volume->set_notify_handle(overlapped.hEvent);

    // This call will put the thread into a wait state until an change
    // happens to our directory.
    if (!ReadDirectoryChangesW(
        h_directory,
        result_buffer,
        change_notify_result_buffer_size,
        TRUE,
        notify_filter,
        &bytes_returned,
        &overlapped,
        nullptr))
    {
        DBGWRITE(dbg_level_info, L"ReadDirectoryChangesW() failed! (%d)\n", GetLastError());
        return;
    }

    while (!GetOverlappedResult(h_directory, &overlapped, &bytes_returned, FALSE))
    {
        if (GetLastError() != ERROR_IO_INCOMPLETE || !server_running ||
            !(volume->get_internal_flags() & flag_volume_shared))
        {
            DBGWRITE(dbg_level_info,
                     L"ReadDirectoryChangesW() returned error, or volume terminated. (%d)\n",
                     GetLastError()
            );

            CloseHandle(overlapped.hEvent);
            return;
        }

        Sleep(5);
    }

    // If the number of bytes transferred is 0, then the buffer we gave to
    // ReadDirectoryChangesW() was too small to hold the change data. We're
    // supposed to handle this now by enumerating the directory structure
    // manually.
    if (bytes_returned == 0)
    {
        DBGWRITE(dbg_level_info, L"0 bytes transferred in change! (%s)\n", volume->get_share_path().c_str());

        CloseHandle(overlapped.hEvent);
        return;
    }

    auto p_info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(result_buffer);
    afp_index* index_entry = nullptr;

    // We have a successful change notify. Flip through the array of
    // changes and take appropriate action depending on the change.
    DBGWRITE(dbg_level_all, L"Change notify data received!! (%d bytes)\n", bytes_returned);
    DBGWRITE(dbg_level_all, L"NextEntryOffset = %d\n", p_info->NextEntryOffset);

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
            lstrcpyn(file_name, p_info->FileName, static_cast<int>(p_info->FileNameLength));

            DBGWRITE(dbg_level_all, L"File renamed, old name = %s\n", file_name);

            index_entry = volume->find(kRootDirID, file_name);
            break;

        // The object is now renamed. Update the afp index with the
        // newly changed name.
        case FILE_ACTION_RENAMED_NEW_NAME:
            lstrcpyn(file_name, p_info->FileName, static_cast<int>(p_info->FileNameLength));

            DBGWRITE(dbg_level_all, L"File renamed, new name = %s\n", file_name);

            if (index_entry != nullptr)
            {
                WCHAR new_name[64];
                AFPCHARMAPARRAY charmap;

                ZeroMemory(new_name, sizeof(new_name));
                afp_get_object_name_from_path(file_name, new_name, sizeof(new_name));

                volume->get_index()->update_entry(index_entry, static_cast<afp_index*>(index_entry->par_entry), file_name);

                // If the object was renamed on the windows box then all bets are
                // off and there's no way to preserve the character mapping. So,
                // clear the charmap and hope for the best.
                if ((index_entry->flags & flag_index_has_charmap) == 0)
                {
                    memset(charmap, 0, sizeof(AFPCHARMAPARRAY));
                    AFPAddCharMapToIndexEntry(index_entry, charmap);
                }
                volume->set_is_dirty();
            }
            break;

        default:
            ASSERT(0);
            break;
        }

        if (p_info->NextEntryOffset == 0)
        {
            break;
        }

        p_info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(reinterpret_cast<PBYTE>(p_info) + p_info->NextEntryOffset);
    }
    while (TRUE);

    if (overlapped.hEvent != nullptr)
    {
        CloseHandle(overlapped.hEvent);
    }
}


void afp_change_notify_add(const FILE_NOTIFY_INFORMATION* file_notify_info, const std::shared_ptr<afp_volume>& volume)
{
    WCHAR new_item_pathname[MAX_PATH] = {};
    WCHAR new_item_name[MAX_PATH];
    afp_index* pEntry = nullptr;
    WIN32_FILE_ATTRIBUTE_DATA attribute_data;

    lstrcpyn(new_item_pathname, file_notify_info->FileName, static_cast<int>(file_notify_info->FileNameLength));

    // Extract just the name of the new file object from the pathname.
    afp_get_object_name_from_path(new_item_pathname, new_item_name, sizeof(new_item_name));

    // First step: we need to find the index entry that is the parent
    // directory of the new object we're adding.
    std::filesystem::path new_item_path(new_item_pathname);
    const std::filesystem::path new_item_parent_path = new_item_path.parent_path();

    DBGWRITE(dbg_level_info, L"New item pathname = %s\n", new_item_pathname);
    DBGWRITE(dbg_level_info, L"Parent dir path = %s\n", new_item_parent_path.c_str());

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

    // See if there is already an index entry for this object. If so, then we
    // don't need to do anything.
    if (volume->find(kRootDirID, new_item_pathname) != nullptr)
    {
        DBGWRITE(dbg_level_all, L"Object already exists in index\n");
        return;
    }

    // Get the file attributes for the new file object. We use these to
    // determine if the object is a file or directory.
    if (!GetFileAttributesEx(new_item_path.c_str(), GetFileExInfoStandard, &attribute_data))
    {
        DBGWRITE(dbg_level_info, L"Item not found on disk!\n");
        return;
    }

    // Finally, add the new file object into the afp index.
    UINT32 new_object_id;
    volume->get_index()->add_entry(
        parent_entry->object_id,
        new_item_name,
        new_item_path,
        (attribute_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0,
        &new_object_id);

    DBGWRITE(
        dbg_level_all,
        L"Added new item in notify id=%d name=%s\n",
        new_object_id,
        new_item_pathname);

    // Add the finder info stream to the file or directory if
    // it doesn't already exist.
    AFPCheckForAndAddFinderInfoStream(pEntry);

    if (attribute_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
        // If the item is a directory, we add all of its contents
        // to the volume index as well.
        g_volumes->index_queue_task(task_index_directory, volume, new_object_id, new_item_path);
    }
    else
    {
        // Add the resource streams if necessary for files.
        AFPCheckForAndCreateFinderRsrcStream(pEntry);
    }

    // Mark the volume dirty so it will tell any connected macs
    // that the volume contents have changed.
    volume->set_is_dirty();
}


void afp_change_notify_remove(
    IN const FILE_NOTIFY_INFORMATION* file_notify_info,
    IN const std::shared_ptr<afp_volume>& volume
)
{
    TCHAR remove_item_pathname[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA attribute_data;

    // Copy the pathname (path plus object name) into a null terminated
    // string buffer for later use.
    lstrcpyn(remove_item_pathname, file_notify_info->FileName, static_cast<int>(file_notify_info->FileNameLength));

    DBGWRITE(dbg_level_info, L"Removing %s\n", remove_item_pathname);

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
    volume->get_index()->remove_entry(entry, true);

    // Tell the attention mechanism to notify clients the volume has changed.
    volume->set_is_dirty();
}
