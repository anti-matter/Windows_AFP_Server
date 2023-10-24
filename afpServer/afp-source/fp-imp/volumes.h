#pragma once

#include "afp.h"
#include "afp_file_index.h"
#include "afp_net_buffer.h"
#include "afp_volume.h"
#include "desktop_database.h"

#define MAX_LEVELS_DEEP				128
#define MAX_VOLUMES					256

class AfpBuffer;

//
//Flags used in the wVolumeFlags field in the AFP_VOLUME_INFO structure
//
enum
{
    FLAG_VOLUME_MARKEDFORDELETION = 0x01,
    FLAG_VOLUME_INDEXINGCOMPLETE = 0x02,
    FLAG_VOLUME_READONLY = 0x04,
    FLAG_VOLUME_HASPASSWORD = 0x08,
    FLAG_VOLUME_SHARED = 0x10 //Volume is shared out
};

//
//This struct is used to hold info for our volumes
//that are shared. Each instance will be part of a
//linked list.
//
struct AFP_VOLUME_INFO
{
    WCHAR szShareName[MAX_VOLNAME_LEN + 1];
    WCHAR szPath[MAX_PATH];
    BOOL fDirty;
    uint16 wVolID;
    BYTE bAFPFlags; //Has password, has config info
    DWORD dwRefCount;
    DWORD wVolumeFlags;
    FILETIME ftCreationDate;
    FILETIME ftModDate;
    FILETIME ftBackupDate;

    CRITICAL_SECTION csNotifyQueue;

    afp_file_index* index;
    desktop_database* desktop;

    HANDLE notifyEvent;
};

struct AFP_INDEX_REMOVE_ENTRY_WORKER_ELEMENT
{
    AFP_VOLUME_INFO* pVolInfo;
    afp_index* pEntryToRemove;
};

using PAFP_INDEX_REMOVE_ENTRY_WORKER_ELEMENT = AFP_INDEX_REMOVE_ENTRY_WORKER_ELEMENT*;

DWORD
WINAPI InitializeVolumesThread(
    IN LPVOID pParam
);

void
AFPInitializeVolumes();

DWORD
AFPAddNewVolumeFromUI(
    IN WCHAR* szSharename,
    IN WCHAR* szPath,
    IN WCHAR* szPassword
);

DWORD AFPRemoveVolumeFromUI(
    IN WCHAR* szShareName
);

AFP_VOLUME_INFO*
AFPAddVolume(
    IN PWCHAR pszShareName,
    IN PWCHAR pszPath,
    IN DWORD dwFlags
);

AFPERROR AFPRemoveVolume(
    IN AFP_VOLUME_INFO* pVolInfo
);

AFPERROR
AFPRemoveAllVolumes();

BOOL
afp_get_volume_info_block(
    IN PWCHAR volume_name,
    IN WORD volume_id,
    OUT afp_volume** volume
);

BOOL
AFPMapVolumeNameToID(
    IN PWCHAR szVolumeName,
    OUT PWORD wVolID
);

BYTE
AFPNumVolumesShared();

BYTE
AFPNumVolumesActive();

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

void
afp_add_directory_to_index_worker_queue(
    IN afp_volume* volume,
    IN const std::filesystem::path& dir_path,
    IN DWORD dir_parent_id
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
