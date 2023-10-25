#pragma once

#include "afp.h"
#include "afp_file_index.h"
#include "afp_net_buffer.h"
#include "afp_volume.h"
#include "desktop_database.h"

#define MAX_LEVELS_DEEP				128
#define MAX_VOLUMES					256


DWORD
AFPAddNewVolumeFromUI(
    IN WCHAR* share_name,
    IN WCHAR* share_path,
    IN WCHAR* share_password
);

DWORD AFPRemoveVolumeFromUI(
    IN WCHAR* share_name
);

AFPERROR AFPGetVolumeParameters(
    IN WORD afpVolumeID,
    IN WORD afpBitmap,
    IN afp_net_buffer& afpReply
);

void
AFPMarkVolumeDirty(
    IN WORD wVolID
);

AFPERROR
AFPIndexBuild(
    IN PWCHAR pPath,
    IN DWORD dwParID,
    IN AFP_VOLUME_INFO* pVInfo
);

afp_index*
AFPIndexAddEntry(
    IN AFP_VOLUME_INFO* pPassedVolInfo OPTIONAL,
    IN WORD wVolID OPTIONAL,
    IN DWORD dwParID,
    IN PWCHAR pObjectName,
    IN PWCHAR pObjectPath,
    IN bool isDirectory,
    OUT UINT32* pdwNewDirID
);

AFPERROR AFPIndexUpdateEntryFlags(
    IN afp_index* pEntry,
    IN WORD wFlags
);

AFPERROR
AFPIndexUpdateEntry(
    IN AFP_VOLUME_INFO* pVolInfo,
    IN WORD wVolID,
    IN afp_index* pEntry,
    IN afp_index* pNewParentEntry,
    IN PWCHAR pNewName
);

void AFPIndexRemoveEntry(
    IN AFP_VOLUME_INFO* pInfo,
    IN afp_index* pEntry,
    IN bool deleteDirectorySubTree
);

AFPERROR
AFPIndexFindEntryWithVolIDAndPath(
    IN WORD wVolID,
    IN DWORD dwDirID,
    IN PWCHAR pPath,
    OUT afp_index** ppEntry
);

AFPERROR
AFPIndexFindEntryWithPath(
    IN AFP_VOLUME_INFO* pVolInfo,
    IN DWORD dwSearchID,
    IN PWCHAR pPath,
    OUT afp_index** ppEntry
);

AFPERROR
AFPIndexFindEntry(
    IN AFP_VOLUME_INFO* pVolInfo,
    IN DWORD dwDirID,
    OUT afp_index** ppEntry
);

AFPERROR
AFPIndexGetFullPath(
    IN AFP_VOLUME_INFO* pVolInfo,
    IN DWORD dwDirID,
    IN PWCHAR pPath,
    IN size_t cbPath
);

AFPERROR
AFPVolumeWriteAccessCheck(
    IN WORD wVolID
);

AFPERROR
AFPCheckVolumePassword(
    IN WORD wVolID,
    IN PassWord* inPass
);

inline void AQUIRE_NOTIFY_LOCK_VOLINFO(AFP_VOLUME_INFO* volumeInfo)
{
    assert(volumeInfo != nullptr);
    EnterCriticalSection(&volumeInfo->csNotifyQueue);
}

inline void RELEASE_NOTIFY_LOCK_VOLINFO(AFP_VOLUME_INFO* volumeInfo)
{
    assert(volumeInfo != nullptr);
    LeaveCriticalSection(&volumeInfo->csNotifyQueue);
}

inline void AQUIRE_NOTIFY_LOCK_ID(const WORD volumeID)
{
    AFP_VOLUME_INFO* volumeInfo = nullptr;
    if (AFPGetVolumeInfoBlock(nullptr, volumeID, &volumeInfo))
    {
        AQUIRE_NOTIFY_LOCK_VOLINFO(volumeInfo);
    }
}

inline void RELEASE_NOTIFY_LOCK_ID(const WORD volumeID)
{
    AFP_VOLUME_INFO* volumeInfo = nullptr;
    if (AFPGetVolumeInfoBlock(nullptr, volumeID, &volumeInfo))
    {
        RELEASE_NOTIFY_LOCK_VOLINFO(volumeInfo);
    }
}

#ifdef _DEBUG

void AFPDumpAFPIndex(
    IN afp_index* index
);

#endif
