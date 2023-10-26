//===================================================================
// volumes.c
//===================================================================

#include "stdafx.h"
#include "afpdebug.h"
#include "afphelp.h"
#include "volumes.h"
#include "fdparms.h"
#include "afpnotify.h"
#include "afp_net_buffer.h"
#include "afp_registry.h"
#include "afp_volumes.h"

extern std::unique_ptr<afp_volumes> g_volumes;


DWORD AFPAddNewVolumeFromUI(
    IN WCHAR* share_name,
    IN WCHAR* share_path,
    IN WCHAR* share_password
)
{
    afp_registry theRegistry;

    const DWORD dw_error = theRegistry.add_volume(share_name, share_path, share_password);
    if (SUCCEEDED(dw_error))
    {
        std::filesystem::path path = share_path;
        g_volumes->add_volume(share_name, path, 0);
    }

    return dw_error;
}


DWORD AFPRemoveVolumeFromUI(
    IN WCHAR* share_name
)
{
    const auto volume = g_volumes->find(share_name);
    if (volume != nullptr)
    {
        g_volumes->remove_volume(volume->get_volume_id());
    }

    return afp_registry::remove_volume(share_name);
}


//---------------------------------------------------------------------
// AFPGetVolumeParameters()
//---------------------------------------------------------------------
// Returns volume parameters according to the provided bitmap. pBuffer
// needs to be big enough to handle all the parameters requested.
//
// This call is used primarily by the FPOpenVol and FPGetVolParms AFP
// API calls.
//

AFPERROR AFPGetVolumeParameters(
    IN const WORD afp_volume_id,
    IN const WORD afp_bitmap,
    IN afp_net_buffer& afp_reply
)
{
    uint16* volume_name_offset = nullptr;
    const std::shared_ptr<afp_volume> afp_volume = g_volumes->find(afp_volume_id);
    ULARGE_INTEGER free_bytes_available{};
    ULARGE_INTEGER num_bytes_in_disk{};
    ULARGE_INTEGER num_free_bytes{};

    if (afp_volume == nullptr)
    {
        DBGWRITE(dbg_level_warning, L"Volume ID not found in AFPGetVolumeParameters()!\n");
        return afpIDNotFound;
    }

    // Always send back the bitmap we were provided.
    afp_reply.push_num(afp_bitmap);

    if (afp_bitmap & (kFPVolBytesFreeBit | kFPVolBytesTotalBit |
        kFPVolExtBytesFree | kFPVolExtBytesTotal | kFPVolBlockSize))
    {
        free_bytes_available.QuadPart = 0;
        num_bytes_in_disk.QuadPart = 0;
        num_free_bytes.QuadPart = 0;

        if (GetDiskFreeSpaceEx(
            afp_volume->get_share_path().c_str(),
            &free_bytes_available,
            &num_bytes_in_disk,
            &num_free_bytes) == FALSE)
        {
            const DWORD dw_error = GetLastError();

            if ((dw_error != ERROR_IO_PENDING) && (dw_error != NO_ERROR))
            {
                DBGWRITE(dbg_level_warning, L"[VOL]GetFreeDiskSpaceEx() failed! (%d)\n", dw_error);
                return afpMiscErr;
            }
        }
    }

    if (afp_bitmap & kFPVolAttributeBit)
    {
        UINT16 attributes = kFPVolSupportsUnicodeNames | kFPVolSupportsFileIDs | kNoExchangeFiles | kSupportsExtAttrs;
        WCHAR volume_name[32]{};
        WCHAR file_system_name[32]{};
        DWORD max_file_name_len = 0;
        DWORD system_flags = 0;

        if (GetVolumeInformation(
            afp_volume->get_share_path().root_name().c_str(),
            volume_name,
            sizeof(volume_name),
            nullptr,
            &max_file_name_len,
            &system_flags,
            file_system_name,
            sizeof(file_system_name)
        ))
        {
//#if 0
            if (system_flags & FILE_READ_ONLY_VOLUME || afp_volume->get_internal_flags() & flag_volume_readonly)
            {
                attributes |= kFPVolReadOnly;
            }
//#endif
        }
        else if (afp_volume->get_internal_flags() & flag_volume_readonly)
        {
            attributes |= kFPVolReadOnly;
        }

        if (afp_volume->get_internal_flags() & flag_volume_has_password)
        {
            attributes |= kFPVolHasVolumePassword;
        }

        afp_reply.push_num(attributes);
    }

    if (afp_bitmap & kFPVolSignatureBit) afp_reply.push_num<UINT16>(kFixedDirectoryID);
    if (afp_bitmap & kFPVolCreateDateBit) afp_reply.push_num(afp_windows_to_mac_time(afp_volume->get_create_date()));
    if (afp_bitmap & kFPVolModDateBit) afp_reply.push_num(afp_windows_to_mac_time(afp_volume->get_modified_date()));
    if (afp_bitmap & kFPVolBackupDateBit) afp_reply.push_num(afp_windows_to_mac_time(afp_volume->get_backup_date()));
    if (afp_bitmap & kFPVolIDBit) afp_reply.push_num(afp_volume->get_volume_id());

    if (afp_bitmap & kFPVolBytesFreeBit)
    {
        // The max allowable on AFP 2.2 clients is 2GB.
        if (free_bytes_available.QuadPart > MAXLONG)
        {
            afp_reply.push_num(MAXLONG);
        }
        else
        {
            afp_reply.push_num(free_bytes_available.LowPart);
        }
    }

    if (afp_bitmap & kFPVolBytesTotalBit)
    {
        // The max allowable on AFP 2.2 clients is 2GB. As per the AFP 3.0 spec,
        // if the volume is greater than 4GB, we return the total size of the volume
        // minus 4GB.
        if (num_bytes_in_disk.QuadPart > MAXLONG)
        {
            afp_reply.push_num(MAXLONG);
        }
        else
        {
            afp_reply.push_num(num_bytes_in_disk.LowPart);
        }
    }

    // Save the pointer to where we need to write the offset
    // to the volume name.
    if (afp_bitmap & kFPVolNameBit)
    {
        volume_name_offset = reinterpret_cast<uint16*>(afp_reply.get_current_pos_ptr());
        afp_reply.advance_pos_ptr(sizeof(int16));
    }

    // The following 3 bits will only be set if the caller
    // is using AFP 2.2 or later.
    if (afp_bitmap & kFPVolExtBytesFree) afp_reply.push_num(num_free_bytes.QuadPart);
    if (afp_bitmap & kFPVolExtBytesTotal) afp_reply.push_num(num_bytes_in_disk.QuadPart);

    if (afp_bitmap & kFPVolBlockSize)
    {
        DWORD allocation_size;

        // Since we only work on NTFS volumes, we use the NTFS
        // file allocation table to determine what the block
        // size is.
        //
        // BUGBUG: Find API that will get us the real number if it exists!

        if (const auto capacity = num_bytes_in_disk.QuadPart; capacity <= 512000000)
        {
            allocation_size = 512;
        }
        else if (capacity <= 1024000000)
        {
            allocation_size = 1024;
        }
        else if (capacity <= 2048000000)
        {
            allocation_size = 2048;
        }
        else
        {
            allocation_size = 4096;
        }

        afp_reply.push_num(allocation_size);

        DBGWRITE(dbg_level_all, L"Allocation block size = %d (0x%x)\n", allocation_size, allocation_size);
    }

    if (afp_bitmap & kFPVolNameBit)
    {
        *volume_name_offset = htons(static_cast<uint16>(afp_reply.get_data_length()) - sizeof(int16));
        afp_reply.push_pascal_string(afp_volume->get_share_name());
    }

    return AFP_OK;
}


//---------------------------------------------------------------------
// AFPIndexUpdateEntryFlags()
//---------------------------------------------------------------------
// Update the flags field in an index entry. This happens usually to
// update the data and resource open flags.
//
// NOTE: wFlags contains only the flags to change.
//

AFPERROR AFPIndexUpdateEntryFlags(
    IN afp_index* pEntry,
    IN WORD wFlags
)
{
    ASSERT(pEntry != NULL);

    //
    //If the "set item" flag is set, then we are setting the flags. Otherwise
    //we are clearing the bits.
    //
    if (wFlags & flag_index_set_item)
    {
        if (wFlags & flag_index_resource_fork_open)
        {
            pEntry->flags |= flag_index_resource_fork_open;
        }

        if (wFlags & flag_index_data_fork_open)
        {
            pEntry->flags |= flag_index_data_fork_open;
        }
    }
    else
    {
        if (wFlags & flag_index_resource_fork_open)
        {
            pEntry->flags &= ~flag_index_resource_fork_open;
        }

        if (wFlags & flag_index_data_fork_open)
        {
            pEntry->flags &= ~flag_index_data_fork_open;
        }
    }

    return NO_ERROR;
}


//---------------------------------------------------------------------
// AFPIndexFindEntryWithVolIDAndPath()
//---------------------------------------------------------------------
// Locate the index entry belonging to the object pointed to by
// volID, dirID and path.
//

AFPERROR AFPIndexFindEntryWithVolIDAndPath(
    IN WORD wVolID,
    IN DWORD dwDirID,
    IN PWCHAR pPath,
    OUT afp_index** ppEntry
)
{
    const auto volume = g_volumes->find(wVolID);

    // Find the volume info block that we need to access the index.
    if (volume == nullptr)
    {
        DBGWRITE(dbg_level_info, L"Bad volume ID!\n");
        return afpParmErr;
    }

    // Now, find the index entry using the supplied dirID and path.
    const auto dw_error = AFPIndexFindEntryWithPath(
        volume,
        dwDirID,
        pPath,
        ppEntry);

    return dw_error;
}


//---------------------------------------------------------------------
// AFPIndexFindEntryWithPath()
//---------------------------------------------------------------------
// Given a dirID and path, return the entry for the AFP object.
//

AFPERROR AFPIndexFindEntryWithPath(
    IN const std::shared_ptr<afp_volume>& pVolInfo,
    IN const DWORD dwSearchID,
    IN const PWCHAR pPath,
    OUT afp_index** ppEntry
)
{
    const auto pFileIndex = pVolInfo->get_index();
    WCHAR szTemp[MAX_PATH]{};

    *ppEntry = nullptr;
    auto pEntry = pFileIndex->get_entry(dwSearchID);

    if (pEntry == nullptr)
    {
        DBGWRITE(
            dbg_level_all,
            L"[AFPIndexFindEntryWithPath]Object ID not found in AFPIndexFindEntryWithPath\n"
        );

        return afpParmErr;
    }

    //
    //Build the full path to the object we're looking for.
    //
    if (pPath != nullptr && lstrlen(pPath) > 0)
    {
        StringCbPrintf(szTemp, sizeof(szTemp), TEXT("%s\\%s"), pEntry->sz_path.c_str(), pPath);
    }
    else
    {
        //
        //If no sub path was provided, then we're as far as we need to go.
        //
        *ppEntry = pEntry;
        return AFP_OK;
    }

    //
    //Now, search for the full path string in the index.
    //
    pEntry = pFileIndex->find(szTemp);

    if (pEntry != nullptr)
    {
        *ppEntry = pEntry;
        return AFP_OK;
    }

    DBGWRITE(
        dbg_level_all,
        L"[AFPIndexFindEntryWithPath]Failed to find %s\n", szTemp
    );

    return afpObjectNotFound;
}


//---------------------------------------------------------------------
// AFPVolumeWriteAccessCheck()
//---------------------------------------------------------------------
// Returns TRUE if the caller has write access to the volume.
//

AFPERROR
AFPVolumeWriteAccessCheck(
    IN WORD volume_id
)
{
    if (const auto volume = g_volumes->find(volume_id); volume != nullptr)
    {
        if (volume->get_internal_flags() & flag_volume_readonly)
        {
            return afpVolLocked;
        }
    }

    return AFP_OK;
}


//---------------------------------------------------------------------
// AFPCheckVolumePassword()
//---------------------------------------------------------------------
// Checks the volume password when opening a volume.
//

AFPERROR
AFPCheckVolumePassword(
    IN WORD volume_id,
    IN const PassWord* password
)
{
    afp_registry registry;
    PassWord password_from_registry;

    if (const auto volume = g_volumes->find(volume_id); volume != nullptr)
    {
        if (registry.get_volume_password(volume->get_share_name().c_str(), sizeof(PassWord), &password_from_registry) == NO_ERROR)
        {
            if (memcmp(password, password_from_registry, sizeof(PassWord)) != 0)
            {
                return afpAccessDenied;
            }
        }
    }

    return AFP_OK;
}


#ifdef _DEBUG
//---------------------------------------------------------------------
// AFPDumpAFPIndex()
//---------------------------------------------------------------------
// Debug function to dump the AFP index.
//

void AFPDumpAFPIndex(
    IN afp_file_index* index
)
{
    index->dump_index();
}
#endif
