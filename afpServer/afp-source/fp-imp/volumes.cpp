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
#include "AfpBuffer.h"
#include "AFPRegistry.h"
#include "volume_array.h"

volume_array gVolumeArray;
USHORT gNextVolumeID = 65535;


//---------------------------------------------------------------------
// InitializeVolumesThread()
//---------------------------------------------------------------------
// Since building the index of the volumes may take a long time, we
// do it in a separate thread so we don't stop access to the rest
// of the volumes or the UI while indexing is happening.

DWORD WINAPI InitializeVolumesThread(
    IN LPVOID pParam
)
{
    UNREFERENCED_PARAMETER(pParam);

    WCHAR szShareName[33];
    WCHAR szSharePath[MAX_PATH];
    PassWord szPassword;
    DWORD volFlags;
    AFPRegistry theRegistry;

    if (theRegistry.OpenVolumeListRegKey())
    {
        while (theRegistry.NextVolume(
            szShareName,
            sizeof(szShareName),
            szSharePath,
            sizeof(szSharePath),
            reinterpret_cast<PCHAR>(szPassword),
            &volFlags))
        {
            //
            //Now add the volume to our volume index and initialize it.
            //
            AFPAddVolume(szShareName, szSharePath, volFlags);
        }
    }

    return 0;
}


//---------------------------------------------------------------------
// AFPAddNewVolumeFromUI()
//---------------------------------------------------------------------
// Call this function when the user is adding a new AFP share from
// the UI. It adds the new share parms to the registry as well as
// publishes the volume to afp clients.

DWORD AFPAddNewVolumeFromUI(
    IN WCHAR* szSharename,
       IN WCHAR* szPath,
       IN WCHAR* szPassword
)
{
    AFPRegistry theRegistry;
    DWORD dwError;

    dwError = theRegistry.AddVolume(szSharename, szPath, szPassword);

    if (SUCCEEDED(dwError))
    {
        dwError = (AFPAddVolume(szSharename, szPath, 0) == nullptr) ? E_FAIL : ERROR_SUCCESS;
    }

    return dwError;
}


//---------------------------------------------------------------------
// AFPRemoveVolumeFromUI()
//---------------------------------------------------------------------
// Call this function when the user is removing an AFP share from
// the UI.
//

DWORD AFPRemoveVolumeFromUI(
    IN WCHAR* szShareName
)
{
    AFPRegistry registry;
    DWORD error = 2;

    auto volume = gVolumeArray.find_by_name(szShareName);
    if (volume != nullptr)
    {
        error = AFPRemoveVolume(volume);
    }

    if ((error == NO_ERROR) || (error == 2))
    {
        //
        //Lastly, remove the volume from the registry.
        //
        error = registry.RemoveVolume(szShareName);
    }

    return error;
}


//---------------------------------------------------------------------
// AFPInitializeVolumes()
//---------------------------------------------------------------------
// Initialize our AFP volumes and data structures needed to maintain
// the volume list.
//

void AFPInitializeVolumes()
{
    HANDLE hThread;
    DWORD dwThreadID;

    hThread = CreateThread(
        nullptr,
        0,
        InitializeVolumesThread,
        nullptr,
        0,
        &dwThreadID
    );

    CloseHandle(hThread);
}


//---------------------------------------------------------------------
// AFPAddVolume()
//---------------------------------------------------------------------
// Add a volume to our shared list.
//

AFP_VOLUME_INFO* AFPAddVolume(
    IN PWCHAR pszShareName,
       IN PWCHAR pszPath,
       IN DWORD dwFlags
)
{
    p_afp_index pIndex;
    WIN32_FILE_ATTRIBUTE_DATA winAttribData;
    DWORD dwError;
    WCHAR szWinPath[MAX_PATH];
    HANDLE hThread;
    DWORD dwThreadID = 0;
    HANDLE hFile;

    //
    //In addition to getting the creation and mod dates for the
    //volume, we use this to verify the location exists.
    //
    if (!GetFileAttributesEx(pszPath, GetFileExInfoStandard, &winAttribData))
    {
        DBGWRITE(dbg_level_warning, L"[VOL]ERROR: Share path not found! (%d)\n", GetLastError());
        return nullptr;
    }

    auto newVolume = std::make_unique<AFP_VOLUME_INFO>();

    StringCbCopy(newVolume->szShareName, sizeof(newVolume->szShareName), pszShareName);
    StringCbCopy(newVolume->szPath, sizeof(newVolume->szPath), pszPath);

    InitializeCriticalSection(&newVolume->csNotifyQueue);

    newVolume->wVolID = gNextVolumeID--;
    newVolume->dwRefCount = 0;
    newVolume->bAFPFlags = 0;
    newVolume->wVolumeFlags = dwFlags;
    newVolume->desktop = nullptr;
    newVolume->notifyEvent = INVALID_HANDLE_VALUE;

    //
    //Create the initial entries in the volume's afp index. This entry is
    //the "parent of root" and "root dir" entries.
    //
    lstrcpyn(szWinPath, pszPath, lstrlen(pszPath) - (lstrlen(pszShareName)));

    newVolume->index = new afp_file_index();

    newVolume->index->add_entry(
        static_cast<DWORD>(-1),
        TEXT("Parent"),
        szWinPath,
        true
    );

    pIndex = newVolume->index->add_entry(
        kParentOfRoot,
        pszShareName,
        pszPath,
        true
    );

    //
    //Create the Afp_AfpInfo stream that holds FinderInfo for the root dir.
    //
    AFPCheckForAndAddFinderInfoStream(pIndex);
    StringCbPrintf(szWinPath, sizeof(szWinPath), TEXT("%s:%s"), pIndex->sz_path, AFP_INFO_STREAM_NAME);

    hFile = CreateFile(
        szWinPath,
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hFile == INVALID_HANDLE_VALUE)
    {
        dwError = GetLastError();

        //
        //If the file already exists, that's okay, just ignore the error.
        //
        if (dwError != ERROR_FILE_EXISTS)
        {
            DBGWRITE(dbg_level_info, L"[AFP]CreateFile() for info fork failed in AddVolume! (%d)\n", dwError);
        }
    }
    else
    {
        CloseHandle(hFile);
    }

    newVolume->ftCreationDate = winAttribData.ftCreationTime;
    newVolume->ftModDate = winAttribData.ftLastWriteTime;

    //
    //Build the AFP index for this volume. This index keeps track
    //of dir id's and dir names. The 'root' of this volume is the
    //share point. We pass kRootDirID as that will be the starting
    //dir id for the root directory of our volume.
    //	
    if (AFPIndexBuild(pszPath, kRootDirID, newVolume.get()) != NO_ERROR)
    {
        //
        //If we get here, then we had a catostrophic failure during indexing.
        //
        ASSERT(0);

        return nullptr;
    }

    //
    //This needs to be set so the notify thread keeps looping.
    //
    newVolume->wVolumeFlags |= FLAG_VOLUME_SHARED;

    //
    //Add the new volume element to the volume list.
    //
    auto tempVolume = newVolume.get();
    gVolumeArray.add_item(newVolume);

    //
    //Now add the change notify thread.
    //
    hThread = CreateThread(
        nullptr,
        0,
        AFPNotifyThread,
        static_cast<LPVOID>(tempVolume),
        0,
        &dwThreadID
    );

    CloseHandle(hThread);

    return tempVolume;
}


//---------------------------------------------------------------------
// AFPRemoveVolume()
//---------------------------------------------------------------------
// Remove a volume from our shared list.
//

AFPERROR AFPRemoveVolume(
    IN AFP_VOLUME_INFO* pVolInfo
)
{
    //
    //IMPLEMENT: a thread which will watch for volumes marked
    //for deletion and delete them when the refcount reaches 0.
    //

    //
    //Signal to the change notify thread that we are no longer a 
    //shared volume resource.
    //
    pVolInfo->wVolumeFlags &= ~FLAG_VOLUME_SHARED;

    //
    //This releases the event that ReadDirectoryChangesW() is waiting on
    //in the change notify thread so it will exit.
    //
    if (pVolInfo->notifyEvent != INVALID_HANDLE_VALUE)
    {
        SetEvent(pVolInfo->notifyEvent);
    }

    while (pVolInfo->dwRefCount > 0)
    {
        Sleep(500);
    }

    DBGWRITE(dbg_level_info, L"[VOL]Removing volume %s\n", pVolInfo->szShareName);

    //
    //Now erase the entire index structure for this volume from memory.
    //NOTE: this is where we would save the index to disk if we were
    //going to do that.
    //
    delete pVolInfo->index;
    delete pVolInfo->desktop;

    //
    //First step is to remove the volume structure pointer
    //for the volume from the volume tracking array.
    //
    gVolumeArray.remove_item(pVolInfo);

    return AFP_OK;
}


//---------------------------------------------------------------------
// AFPRemoveAllVolumes()
//---------------------------------------------------------------------
// Called in response to us shutting down.
//

AFPERROR AFPRemoveAllVolumes()
{
    auto_lock lock(gVolumeArray.mutex());

    auto size = gVolumeArray.num_items();
    for (UINT32 i = 0; i < size; ++i)
    {
        AFPRemoveVolume(gVolumeArray.get_item(i));
    }

    return AFP_OK;
}


//---------------------------------------------------------------------
// AFPGetVolumeInfoBlock()
//---------------------------------------------------------------------
// Returns a volume info block pertaining to a volume name or ID. If
// szVolName is NULL, then the search is performed using the vol id.
//

BOOL AFPGetVolumeInfoBlock(
    IN PWCHAR szVolName,
       IN WORD wVolID,
       OUT AFP_VOLUME_INFO** ppVolInfo
)
{
    *ppVolInfo = szVolName != nullptr
                    ? gVolumeArray.find_by_name(szVolName)
                    : gVolumeArray.find_by_id(wVolID);

    return *ppVolInfo != nullptr;
}


//---------------------------------------------------------------------
// AFPMapVolumeNameToID()
//---------------------------------------------------------------------
// Maps a volume name to an AFP volume ID.
//

BOOL AFPMapVolumeNameToID(
    IN PWCHAR szVolumeName,
       OUT PWORD wVolID
)
{
    auto volumeInfo = gVolumeArray.find_by_name(szVolumeName);

    if (volumeInfo != nullptr)
    {
        *wVolID = volumeInfo->wVolID;
        return true;
    }

    return false;
}


//---------------------------------------------------------------------
// AFPNumVolumesActive()
//---------------------------------------------------------------------
// Returns the number of volumes that are available for clients to
// to access.
//

BYTE AFPNumVolumesActive()
{
    auto_lock lock(gVolumeArray.mutex());
    BYTE count = 0;

    for (auto it = gVolumeArray.begin(); it < gVolumeArray.end(); ++it)
    {
        //
        //First, we check to make sure the volume has been fully indexed. If not
        //we cannot offer it to the clients.
        //
        if ((it->get()->wVolumeFlags & FLAG_VOLUME_INDEXINGCOMPLETE) != 0)
        {
            count++;
        }
    }

    return count;
}


//---------------------------------------------------------------------
// AFPNumVolumesShared()
//---------------------------------------------------------------------
// Returns the number of volumes that are shared.
//

BYTE AFPNumVolumesShared()
{
    return static_cast<BYTE>(gVolumeArray.num_items());
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
       IN AfpBuffer& afpReply
)
{
    DWORD dwError;
    uint16* volNameOffset = nullptr;
    AFP_VOLUME_INFO* afpVolume = nullptr;
    ULARGE_INTEGER i64FreeBytesAvailable = {0};
    ULARGE_INTEGER i64NumBytesInDisk = {0};
    ULARGE_INTEGER i64NumFreeBytes = {0};

    //
    //Put the bitmap into the buffer.
    //
    afpReply.AddInt16(afpBitmap);

    //
    //Get a pointer to our volume information block which contains
    //a lot of the data we need here.
    //
    if (AFPGetVolumeInfoBlock(nullptr, afpVolumeID, &afpVolume) == FALSE)
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

        if (!GetDiskFreeSpaceEx(
            afpVolume->szPath,
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

        StringCbPrintf(tempName, sizeof(tempName), TEXT("%c:\\"), afpVolume->szShareName[0]);

        auto success = GetVolumeInformation(
            tempName,
            szVolumeName,
            sizeof(szVolumeName),
            nullptr,
            &maxFileNameLen,
            &systemFlags,
            fileSystemName,
            sizeof(fileSystemName)
        );

        if (success)
        {
            //Fix non-support for FILE_READ_ONLY_VOLUME
#if 0
            if ((systemFlags & FILE_READ_ONLY_VOLUME) || (afpVolume->wVolumeFlags & FLAG_VOLUME_READONLY))
            {
                attributes |= kFPVolReadOnly;
            }
#endif
        }
        else if (afpVolume->wVolumeFlags & FLAG_VOLUME_READONLY)
        {
            attributes |= kFPVolReadOnly;
        }

        if (afpVolume->wVolumeFlags & FLAG_VOLUME_HASPASSWORD)
        {
            attributes |= kFPVolHasVolumePassword;
        }

        afpReply.AddInt16(attributes);
    }

    if (afpBitmap & kFPVolSignatureBit) afpReply.AddInt16(kFixedDirectoryID);

    if (afpBitmap & kFPVolCreateDateBit) afpReply.AddInt32(afp_windows_to_mac_time(&afpVolume->ftCreationDate));
    if (afpBitmap & kFPVolModDateBit) afpReply.AddInt32(afp_windows_to_mac_time(&afpVolume->ftModDate));
    if (afpBitmap & kFPVolBackupDateBit) afpReply.AddInt32(afp_windows_to_mac_time(&afpVolume->ftBackupDate));

    if (afpBitmap & kFPVolIDBit) afpReply.AddInt16(afpVolume->wVolID);

    if (afpBitmap & kFPVolBytesFreeBit)
    {
        //
        //The max allowable on AFP 2.2 clients is 2GB.
        //
        if (i64FreeBytesAvailable.QuadPart > MAXLONG)
        {
            afpReply.AddInt32(MAXLONG);
        }
        else
        {
            afpReply.AddInt32(i64FreeBytesAvailable.LowPart);
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
            afpReply.AddInt32(MAXLONG);
        }
        else
        {
            afpReply.AddInt32(i64NumBytesInDisk.LowPart);
        }
    }

    //
    //Save the pointer to where we neet to write the offset
    //to the volume name.
    //
    if (afpBitmap & kFPVolNameBit)
    {
        volNameOffset = reinterpret_cast<uint16*>(afpReply.GetCurrentPosPtr());
        afpReply.Advance(sizeof(int16));
    }

    //
    //The following 3 bits will only be set if the caller
    //is using AFP 2.2 or later.
    //
    if (afpBitmap & kFPVolExtBytesFree) afpReply.AddInt64(i64NumFreeBytes.QuadPart);
    if (afpBitmap & kFPVolExtBytesTotal) afpReply.AddInt64(i64NumBytesInDisk.QuadPart);

    if (afpBitmap & kFPVolBlockSize)
    {
        DWORD dwAllocationSize;
        auto i64Capacity = i64NumBytesInDisk.QuadPart;

        //
        //Since we only work on NTFS volumes, we use the NTFS
        //file allocation table to determine what the block
        //size is.
        //
        //BUGBUG: Find API that will get us the real number if it exists!
        //

        if (i64Capacity <= 512000000)
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

        afpReply.AddInt32(dwAllocationSize);

        DBGWRITE(dbg_level_all, L"[VOL]Allocation block size = %d\n", dwAllocationSize);
    }

    if (afpBitmap & kFPVolNameBit)
    {
        *volNameOffset = htons(static_cast<uint16>(afpReply.GetDataLength()) - sizeof(int16));
        afpReply.AddUniStringAsPascal(afpVolume->szShareName);
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
    p_afp_index pIndex;
    const auto cbPath = MAX_PATH * sizeof(WCHAR);
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
        DWORD dwNewDirID;

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
                    AFPAddDirectoryToIndexWorkerQueue(pVInfo, path.get(), dwNewDirID);
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
    //If we're indexing the root dir, then we send a worker cmd that
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
// AFPAddDirectoryToIndexWorkerQueue()
//---------------------------------------------------------------------
// Add a new directory to be indexed to the worker thread queue.
//

void AFPAddDirectoryToIndexWorkerQueue(
    IN AFP_VOLUME_INFO* pVolInfo,
       IN PWCHAR pPath,
       IN DWORD dwParentID
)
{
    const auto cbNewPath = MAX_PATH * sizeof(WCHAR);
    auto newWorker = static_cast<PAFP_WORKER_ELEMENT>(DBG_ALLOC(sizeof(AFP_WORKER_ELEMENT)));

    if (newWorker != nullptr)
    {
        //
        //We have to make a copy of the path, otherwise it may be freed before
        //the worker gets to it.
        //
        auto newPath = static_cast<PWCHAR>(DBG_ALLOC(cbNewPath));

        if (newPath != nullptr)
        {
            StringCbCopy(newPath, cbNewPath, pPath);

            newWorker->dwWorkerFunction = AFP_WORKER_INDEX_DIRECTORY;
            newWorker->pParam1 = pVolInfo;
            newWorker->pParam2 = newPath;
            newWorker->dwParam = dwParentID;

            AFPWorkerAddElementToWorkerQueue(newWorker);
        }
        else
        {
            DBG_FREE(newWorker);
        }
    }
}


//---------------------------------------------------------------------
// AFPIndexAddEntry()
//---------------------------------------------------------------------
// Add a new directory or file to the afp index.
//

p_afp_index AFPIndexAddEntry(
    IN AFP_VOLUME_INFO* pPassedVolInfo OPTIONAL,
       IN WORD wVolID OPTIONAL,
       IN DWORD dwParID,
       IN PWCHAR pObjectName,
       IN PWCHAR pObjectPath,
       IN bool isDirectory,
       OUT PDWORD pdwNewDirID
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
    IN AFP_VOLUME_INFO* pVolInfo,
       IN WORD wVolID,
       IN p_afp_index pEntry,
       IN p_afp_index pNewParentEntry,
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
        if (AFPGetVolumeInfoBlock(nullptr, wVolID, &pVInfo) == FALSE)
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
    IN p_afp_index pEntry,
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
       IN p_afp_index pEntry,
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
       OUT p_afp_index* ppEntry
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
       OUT p_afp_index* ppEntry
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
        StringCbPrintf(szTemp, sizeof(szTemp), TEXT("%s\\%s"), pEntry->sz_path, pPath);
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
    pEntry = pFileIndex->find_entry_with_full_path(szTemp);

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
       OUT p_afp_index* ppEntry
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
    p_afp_index pEntry;

    ASSERT(pPath != NULL);

    if (AFPIndexFindEntry(pVolInfo, dwDirID, &pEntry) == NO_ERROR)
    {
        StringCbCopy(pPath, cbPath, pEntry->sz_path);
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
    AFPRegistry theRegistry;
    PassWord passWord;
    AFP_VOLUME_INFO* afpVolume = nullptr;

    if (AFPGetVolumeInfoBlock(nullptr, wVolID, &afpVolume))
    {
        if (theRegistry.GetVolumePassword(afpVolume->szShareName, sizeof(PassWord), &passWord) == NO_ERROR)
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
