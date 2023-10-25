//===================================================================
// volumes.c
//===================================================================

#include "stdafx.h"
#include "afpdebug.h"
#include "afphelp.h"
#include "volumes.h"
#include "fpinfo.h"
#include "fdparms.h"
#include "afpnotify.h"
#include "afpworker.h"
#include "afp_net_buffer.h"
#include "afp_registry.h"
#include "afp_volumes.h"
#include "volume_array.h"

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
    afp_registry registry;

    const auto volume = g_volumes->find(share_name);
    if (volume != nullptr)
    {
        g_volumes->remove_volume(volume->get_volume_id());
    }

    return registry.remove_volume(share_name);;
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
    DWORD dwError;
    uint16* volNameOffset = nullptr;
    std::shared_ptr<afp_volume> afpVolume = g_volumes->find(afpVolumeID);
    ULARGE_INTEGER i64FreeBytesAvailable = {0};
    ULARGE_INTEGER i64NumBytesInDisk = {0};
    ULARGE_INTEGER i64NumFreeBytes = {0};

    //
    //Put the bitmap into the buffer.
    //
    afpReply.push_int16(afpBitmap);

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
            dwError = GetLastError();

            if ((dwError != ERROR_IO_PENDING) && (dwError != NO_ERROR))
            {
                DBGWRITE(dbg_level_warning, L"[VOL]GetFreeDiskSpaceEx() failed! (%d)\n", dwError);
                return afpMiscErr;
            }
        }
    }

    if (afpBitmap & kFPVolAttributeBit)
    {
        int16 attributes = (kFPVolSupportsUnicodeNames | kFPVolSupportsFileIDs | kNoExchangeFiles | kSupportsExtAttrs);
        WCHAR szVolumeName[32];
        WCHAR fileSystemName[32];
        WCHAR tempName[32];
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
// AFPMarkVolumeDirty()
//---------------------------------------------------------------------
// Mark a volume dirty using the volume ID.
//

void AFPMarkVolumeDirty(
    IN WORD wVolID
)
{
    AFP_VOLUME_INFO* pInfo;
    WIN32_FILE_ATTRIBUTE_DATA attributes;

    if (!AFPGetVolumeInfoBlock(nullptr, wVolID, &pInfo))
    {
        //
        //We failed to find a block corresponding to the
        //supplied volume ID.
        //
        DBGWRITE(dbg_level_warning, L"[VOL]Bad volume ID in AFPMarkVolumeDirty()\n");

        //
        //There's nothing we can do here and this is a "fire and forget"
        //function anyway.
        //
        return;
    }

    pInfo->fDirty = TRUE;

    //
    //Update the volumes modification date which is cached in the 
    //volume info block.
    //
    if (GetFileAttributesEx(pInfo->szPath, GetFileExInfoStandard, &attributes))
    {
        //
        //If the time did not change (possible with NTFS), we need to manually
        //update the mod date for the folder.
        //
        if (!memcmp(&pInfo->ftModDate, &attributes.ftLastWriteTime, sizeof(FILETIME)))
        {
            HANDLE hDir;
            FILETIME time;

            hDir = CreateFile(
                pInfo->szPath,
                FILE_LIST_DIRECTORY,
                FILE_SHARE_READ | FILE_SHARE_DELETE,
                nullptr,
                OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS,
                nullptr);

            if (hDir != INVALID_HANDLE_VALUE)
            {
                GetSystemTimeAsFileTime(&time);
                SetFileTime(hDir, nullptr, nullptr, &time);

                CloseHandle(hDir);
            }
        }
        else
        {
            pInfo->ftModDate = attributes.ftLastWriteTime;
        }
    }
}


//---------------------------------------------------------------------
// AFPIndexBuild()
//---------------------------------------------------------------------
// Build the AFP index for this volume.
//

AFPERROR AFPIndexBuild(
    IN PWCHAR pPath,
    IN DWORD dwParID,
    IN AFP_VOLUME_INFO* pVInfo
)
{
    afp_index* pIndex;
    constexpr auto cbPath = MAX_PATH * sizeof(WCHAR);
    WIN32_FIND_DATA findData;

#if defined(DBG) || defined(_DEBUG)
    if (dwParID == kRootDirID)
    {
        DBGWRITE(dbg_level_info, L"[VOL]Begin indexing share path %s\n", pPath);
    }
#endif

    auto path = std::unique_ptr<WCHAR>(new WCHAR[cbPath]);

    StringCbCopy(path.get(), cbPath, pPath);
    StringCbCat(path.get(), cbPath, TEXT("\\*"));

    //
    //Now the fun stuff. We have to recursively go through
    //all directories in the volume and add them to our list.
    //
    auto hFind = FindFirstFile(path.get(), &findData);

    if (hFind == INVALID_HANDLE_VALUE)
    {
        DBGWRITE(dbg_level_info, L"[VOL]Bad directory: %s\n", path.get());
    }
    else
    {
        UINT32 dwNewDirID;

        auto_lock lock(&pVInfo->csNotifyQueue);

        do
        {
            //
            //I hate these damn dots! How do I not see them to
            //begin with?
            //
            if ((!lstrcmp(findData.cFileName, TEXT("."))) ||
                (!lstrcmp(findData.cFileName, TEXT(".."))))
            {
                continue;
            }

            auto isDirectory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

            //
            //Create the path to the new file or directory by adding the name.
            //
            StringCbPrintf(path.get(), cbPath, TEXT("%s\\%s"), pPath, findData.cFileName);

            //DBGWRITE(DbgLevelInfo, L"[VOL]Indexing %s\n", path));

            pIndex = AFPIndexAddEntry(
                pVInfo,
                0,
                dwParID,
                findData.cFileName,
                path.get(),
                isDirectory,
                &dwNewDirID
            );

            if (pIndex != nullptr)
            {
                //
                //Add the resource streams if necessary on non-directories.
                //				
                if (!isDirectory)
                {
                    AFPCheckForAndCreateFinderRsrcStream(pIndex);
                }

                //
                //Now that we have our index entry, we need to get the char mapping
                //table for mac characters that didn't map to windows chars. Only
                //copy the mapping if the stream existed before we created it.
                //
                if (AFPCheckForAndAddFinderInfoStream(pIndex))
                {
                    AFPCheckForCharMap(pIndex);
                }

                if (isDirectory)
                {
                    //
                    //Make a recursive call to add dirs and files in the new directory
                    //to the index. So we don't run out of stack, what we do it post a new
                    //worker element to a worker thread that will call this function again.
                    //
                    afp_add_directory_to_index_worker_queue(pVInfo, path.get(), dwNewDirID);
                }
            }
            else
            {
                DBGWRITE(dbg_level_info, L"[VOL]Failed to add entry to index!\n");
                ASSERT(0);
            }
        }
        while (FindNextFile(hFind, &findData));

        FindClose(hFind);
    }

    //
    //If we're indexing the root dir, then we send_ a worker cmd that
    //will signal that indexing is complete.
    //
    if (dwParID == kRootDirID)
    {
        auto completeWorker = static_cast<PAFP_WORKER_ELEMENT>(DBG_ALLOC(sizeof(AFP_WORKER_ELEMENT)));

        if (completeWorker)
        {
            completeWorker->dwWorkerFunction = AFP_WORKER_INDEX_COMPLETE;
            completeWorker->pParam1 = pVInfo;

            AFPWorkerAddElementToWorkerQueue(completeWorker);
        }
    }

    return NO_ERROR;
}



//---------------------------------------------------------------------
// AFPIndexAddEntry()
//---------------------------------------------------------------------
// Add a new directory or file to the afp index.
//

afp_index* AFPIndexAddEntry(
    IN AFP_VOLUME_INFO* pPassedVolInfo OPTIONAL,
    IN WORD wVolID OPTIONAL,
    IN DWORD dwParID,
    IN PWCHAR pObjectName,
    IN PWCHAR pObjectPath,
    IN bool isDirectory,
    OUT UINT32* pdwNewDirID
)
{
    AFP_VOLUME_INFO* pVolInfo = nullptr;

    //
    //If pAssedVolInfo is NULL, then we have to do a lot of work here. We have to
    //get the volume info block ptr.
    //
    if (pPassedVolInfo == nullptr)
    {
        if (AFPGetVolumeInfoBlock(nullptr, wVolID, &pVolInfo) == FALSE)
        {
            DBGWRITE(dbg_level_info, L"[VOL]Failed to get vol info block!\n");
            return nullptr;
        }
    }
    else
    {
        pVolInfo = pPassedVolInfo;
    }

    auto pEntry = pVolInfo->index->add_entry(
        dwParID,
        pObjectName,
        pObjectPath,
        isDirectory,
        pdwNewDirID
    );

    return pEntry;
}


//---------------------------------------------------------------------
// AFPIndexUpdateEntry()
//---------------------------------------------------------------------
// A directory has moved. Update it's parent ID and possibly it's name.
//

AFPERROR AFPIndexUpdateEntry(
    IN afp_volume* pVolInfo,
    IN WORD wVolID,
    IN afp_index* pEntry,
    IN afp_index* pNewParentEntry,
    IN PWCHAR pNewName
)
{
    auto pVInfo = pVolInfo;

    //
    //If pVolInfo is NULL, then use wVolID to get a pointer to the 
    //volume entry in the volume list.
    //
    if (pVInfo == nullptr)
    {
        if (afp_get_volume_info_block(nullptr, wVolID, &pVInfo) == FALSE)
        {
            DBGWRITE(dbg_level_info, L"[VOL]Bad volume ID!\n");
            return afpParmErr;
        }
    }

    pVInfo->index->update_entry(pEntry, pNewParentEntry, pNewName);

    return NO_ERROR;
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
// AFPIndexRemoveEntry()
//---------------------------------------------------------------------
// Removes an entry from the index.
//

void AFPIndexRemoveEntry(
    IN AFP_VOLUME_INFO* pInfo,
    IN afp_index* pEntry,
    IN bool deleteDirectorySubTree
)
{
    pInfo->index->remove_entry(pEntry->object_id, deleteDirectorySubTree);
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
    AFP_VOLUME_INFO* pVolInfo;

    //
    //Find the volume info block that we need to access the index.
    //
    if (AFPGetVolumeInfoBlock(nullptr, wVolID, &pVolInfo) == FALSE)
    {
        DBGWRITE(dbg_level_info, L"[VOL]Bad volume ID!\n");
        return afpParmErr;
    }

    //
    //Now, find the index entry using the supplied dirID and path.
    //
    auto dwError = AFPIndexFindEntryWithPath(
        pVolInfo,
        dwDirID,
        pPath,
        ppEntry);

    return dwError;
}


//---------------------------------------------------------------------
// AFPIndexFindEntryWithPath()
//---------------------------------------------------------------------
// Given a dirID and path, return the entry for the AFP object.
//

AFPERROR AFPIndexFindEntryWithPath(
    IN AFP_VOLUME_INFO* pVolInfo,
    IN DWORD dwSearchID,
    IN PWCHAR pPath,
    OUT afp_index** ppEntry
)
{
    auto pFileIndex = pVolInfo->index;
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
// AFPIndexFindEntry()
//---------------------------------------------------------------------
// Returns a ptr to an entry in the volume index pertaining to the
// given directory or file id.
//

AFPERROR AFPIndexFindEntry(
    IN AFP_VOLUME_INFO* pVolInfo,
    IN DWORD dwSearchID,
    OUT afp_index** ppEntry
)
{
    ASSERT(pVolInfo != nullptr);

    auto pEntry = pVolInfo->index->get_entry(dwSearchID);

    if (pEntry == nullptr)
    {
        //
        //We didn't find the directory ID in our volume index.
        //
        DBGWRITE(dbg_level_all,
                 L"[VOL]Directory not found in AFPIndexFindEntry() (dirid = %d)\n",
                 dwSearchID);

        return afpObjectNotFound;
    }

    *ppEntry = pEntry;

    return AFP_OK;
}


//---------------------------------------------------------------------
// AFPIndexGetFullPath()
//---------------------------------------------------------------------
// Returns the full path of an object in the index.
//

AFPERROR AFPIndexGetFullPath(
    IN AFP_VOLUME_INFO* pVolInfo,
    IN DWORD dwDirID,
    IN PWCHAR pPath,
    IN size_t cbPath
)
{
    afp_index* pEntry;

    ASSERT(pPath != NULL);

    if (AFPIndexFindEntry(pVolInfo, dwDirID, &pEntry) == NO_ERROR)
    {
        StringCbCopy(pPath, cbPath, pEntry->sz_path.c_str());
        return NO_ERROR;
    }

    return afpObjectNotFound;
}


//---------------------------------------------------------------------
// AFPVolumeWriteAccessCheck()
//---------------------------------------------------------------------
// Returns TRUE if the caller has write access to the volume.
//

AFPERROR
AFPVolumeWriteAccessCheck(
    IN WORD wVolID
)
{
    AFP_VOLUME_INFO* afpVolume = nullptr;

    if (AFPGetVolumeInfoBlock(nullptr, wVolID, &afpVolume))
    {
        if (afpVolume->wVolumeFlags & FLAG_VOLUME_READONLY)
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
    IN WORD wVolID,
    IN PassWord* inPass
)
{
    afp_registry theRegistry;
    PassWord passWord;
    AFP_VOLUME_INFO* afpVolume = nullptr;

    if (AFPGetVolumeInfoBlock(nullptr, wVolID, &afpVolume))
    {
        if (theRegistry.get_volume_password(afpVolume->szShareName, sizeof(PassWord), &passWord) == NO_ERROR)
        {
            if (memcmp(inPass, passWord, sizeof(PassWord)))
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
