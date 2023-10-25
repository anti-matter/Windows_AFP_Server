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
        g_volumes->add_volume(share_name, share_path, 0);
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
    IN WORD afpVolumeID,
    IN WORD afpBitmap,
    IN afp_net_buffer& afpReply
)
{
    uint16* volNameOffset = nullptr;
    std::shared_ptr<afp_volume> afpVolume = g_volumes->find(afpVolumeID);
    ULARGE_INTEGER i64FreeBytesAvailable{};
    ULARGE_INTEGER i64NumBytesInDisk{};
    ULARGE_INTEGER i64NumFreeBytes{};

    //
    //Put the bitmap into the buffer.
    //
    afpReply.push_num(afpBitmap);

    //
    //Get a pointer to our volume information block which contains
    //a lot of the data we need here.
    //
    if (afpVolume == nullptr)
    {
        DBGWRITE(dbg_level_warning, L"[VOL]Volume ID not found in AFPGetVolumeParameters()!\n");
        return afpIDNotFound;
    }

    //
    //Get the disk information if it's requested for later use.
    //
    if (afpBitmap & (kFPVolBytesFreeBit | kFPVolBytesTotalBit |
        kFPVolExtBytesFree | kFPVolExtBytesTotal | kFPVolBlockSize))
    {
        i64FreeBytesAvailable.QuadPart = 0;
        i64NumBytesInDisk.QuadPart = 0;
        i64NumFreeBytes.QuadPart = 0;

        if (GetDiskFreeSpaceEx(
            afpVolume->get_share_path().c_str(),
            &i64FreeBytesAvailable,
            &i64NumBytesInDisk,
            &i64NumFreeBytes) == FALSE)
        {
            const DWORD dw_error = GetLastError();

            if ((dw_error != ERROR_IO_PENDING) && (dw_error != NO_ERROR))
            {
                DBGWRITE(dbg_level_warning, L"[VOL]GetFreeDiskSpaceEx() failed! (%d)\n", dw_error);
                return afpMiscErr;
            }
        }
    }

    if (afpBitmap & kFPVolAttributeBit)
    {
        int16 attributes = (kFPVolSupportsUnicodeNames | kFPVolSupportsFileIDs | kNoExchangeFiles | kSupportsExtAttrs);
        WCHAR szVolumeName[32];
        WCHAR fileSystemName[32];
        DWORD maxFileNameLen = 0;
        DWORD systemFlags = 0;

        if (GetVolumeInformation(
            afpVolume->get_share_path().root_name().c_str(),
            szVolumeName,
            sizeof(szVolumeName),
            nullptr,
            &maxFileNameLen,
            &systemFlags,
            fileSystemName,
            sizeof(fileSystemName)
        ))
        {
            //Fix non-support for FILE_READ_ONLY_VOLUME
#if 0
            if ((systemFlags & FILE_READ_ONLY_VOLUME) || (afpVolume->wVolumeFlags & FLAG_VOLUME_READONLY))
            {
                attributes |= kFPVolReadOnly;
            }
#endif
        }
        else if (afpVolume->get_internal_flags() & flag_volume_readonly)
        {
            attributes |= kFPVolReadOnly;
        }

        if (afpVolume->get_internal_flags() & flag_volume_has_password)
        {
            attributes |= kFPVolHasVolumePassword;
        }

        afpReply.push_int16(attributes);
    }

    if (afpBitmap & kFPVolSignatureBit) afpReply.push_int16(kFixedDirectoryID);

    if (afpBitmap & kFPVolCreateDateBit) afpReply.push_num(afp_windows_to_mac_time(afpVolume->get_create_date()));
    if (afpBitmap & kFPVolModDateBit) afpReply.push_num(afp_windows_to_mac_time(afpVolume->get_modified_date()));
    if (afpBitmap & kFPVolBackupDateBit) afpReply.push_num(afp_windows_to_mac_time(afpVolume->get_backup_date()));

    if (afpBitmap & kFPVolIDBit) afpReply.push_num(afpVolume->get_volume_id());

    if (afpBitmap & kFPVolBytesFreeBit)
    {
        //
        //The max allowable on AFP 2.2 clients is 2GB.
        //
        if (i64FreeBytesAvailable.QuadPart > MAXLONG)
        {
            afpReply.push_num(MAXLONG);
        }
        else
        {
            afpReply.push_num(i64FreeBytesAvailable.LowPart);
        }
    }

    if (afpBitmap & kFPVolBytesTotalBit)
    {
        //
        //The max allowable on AFP 2.2 clients is 2GB. As per the AFP 3.0 spec,
        //if the volume is greater than 4GB, we return the total size of the volume
        //minus 4GB.
        //
        if (i64NumBytesInDisk.QuadPart > MAXLONG)
        {
            afpReply.push_num(MAXLONG);
        }
        else
        {
            afpReply.push_num(i64NumBytesInDisk.LowPart);
        }
    }

    //
    //Save the pointer to where we need to write the offset
    //to the volume name.
    //
    if (afpBitmap & kFPVolNameBit)
    {
        volNameOffset = reinterpret_cast<uint16*>(afpReply.get_current_pos_ptr());
        afpReply.advance_pos_ptr(sizeof(int16));
    }

    //
    //The following 3 bits will only be set if the caller
    //is using AFP 2.2 or later.
    //
    if (afpBitmap & kFPVolExtBytesFree) afpReply.push_num(i64NumFreeBytes.QuadPart);
    if (afpBitmap & kFPVolExtBytesTotal) afpReply.push_num(i64NumBytesInDisk.QuadPart);

    if (afpBitmap & kFPVolBlockSize)
    {
        DWORD dwAllocationSize;

        //
        //Since we only work on NTFS volumes, we use the NTFS
        //file allocation table to determine what the block
        //size is.
        //
        //BUGBUG: Find API that will get us the real number if it exists!
        //

        if (const auto i64Capacity = i64NumBytesInDisk.QuadPart; i64Capacity <= 512000000)
        {
            dwAllocationSize = 512;
        }
        else if (i64Capacity <= 1024000000)
        {
            dwAllocationSize = 1024;
        }
        else if (i64Capacity <= 2048000000)
        {
            dwAllocationSize = 2048;
        }
        else
        {
            dwAllocationSize = 4096;
        }

        afpReply.push_num(dwAllocationSize);

        DBGWRITE(dbg_level_all, L"[VOL]Allocation block size = %d\n", dwAllocationSize);
    }

    if (afpBitmap & kFPVolNameBit)
    {
        *volNameOffset = htons(static_cast<uint16>(afpReply.get_data_length()) - sizeof(int16));
        afpReply.push_pascal_string(afpVolume->get_share_name());
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
