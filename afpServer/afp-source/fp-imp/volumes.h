//===================================================================
// volumes.h
//===================================================================

#pragma once

#include "afphelp.h"
#include "afp.h"

#include "AfpBuffer.h"
#include "LArray.h"
#include "LFileIndex.h"

#define MAX_LEVELS_DEEP				128
#define MAX_VOLUMES					256

//
//Flags used in the wVolumeFlags field in the AFP_VOLUME_INFO structure
//
enum
{
	FLAG_VOLUME_MARKEDFORDELETION		= 0x01,
	FLAG_VOLUME_INDEXINGCOMPLETE		= 0x02,
	FLAG_VOLUME_READONLY				= 0x04,
	FLAG_VOLUME_HASPASSWORD				= 0x08,
	FLAG_VOLUME_SHARED					= 0x10		//Volume is shared out
};

//
//This struct is used to hold info for our volumes
//that are shared. Each instance will be part of a
//linked list.
//
typedef struct _AFP_VOLUME_INFO
{
	WCHAR				szShareName[MAX_VOLNAME_LEN+1];
	WCHAR				szPath[MAX_PATH];
	BOOL				fDirty;
	WORD				wVolID;
	BYTE				bAFPFlags;			//Has password, has config info
	DWORD				dwRefCount;
	DWORD				wVolumeFlags;
	FILETIME			ftCreationDate;
	FILETIME			ftModDate;
	FILETIME			ftBackupDate;

	CRITICAL_SECTION	csNotifyQueue;
		
	LFileIndex*			index;
	
	CRITICAL_SECTION	csDesktopDB;
	LArray*				dtArray;
	
	HANDLE				notifyEvent;
}AFP_VOLUME_INFO, *PAFP_VOLUME_INFO;

typedef struct
{
	PAFP_VOLUME_INFO	pVolInfo;
	LPAFPINDEX			pEntryToRemove;
}AFP_INDEX_REMOVE_ENTRY_WORKER_ELEMENT, *PAFP_INDEX_REMOVE_ENTRY_WORKER_ELEMENT;

DWORD
WINAPI InitializeVolumesThread(
		IN LPVOID	pParam
);

void 
AFPInitializeVolumes(void);

DWORD 
AFPAddNewVolumeFromUI(
	IN	TCHAR*	szSharename,
	IN	TCHAR*	szPath,
	IN	TCHAR*	szPassword
);

DWORD AFPRemoveVolumeFromUI(
	IN	TCHAR*	szShareName
);

PAFP_VOLUME_INFO
AFPAddVolume(
	IN PWCHAR	pszShareName,
	IN PWCHAR	pszPath,
	IN DWORD	dwFlags
);

AFPERROR AFPRemoveVolume(
	IN	PAFP_VOLUME_INFO		pVolInfo
);

AFPERROR
AFPRemoveAllVolumes(void);

AFPERROR
AFPGetVolumeInfoAtIndex(
	IN	DWORD				dwIndex,
	OUT PAFP_VOLUME_INFO* 	pVolInfo
);

BOOL
AFPGetVolumeInfoBlock(
	IN	PWCHAR				szVolName,
	IN 	WORD				wVolID,
	OUT PAFP_VOLUME_INFO*	ppVolInfo
);

BOOL
AFPMapVolumeNameToID(
	IN 	PWCHAR	szVolumeName,
	OUT	PWORD	wVolID
);

BYTE
AFPNumVolumesShared(void);

BYTE
AFPNumVolumesActive(void);

AFPERROR AFPGetVolumeParameters(
	IN  WORD		afpVolumeID,
	IN  WORD		afpBitmap,
	IN	AfpBuffer&	afpReply
);

void
AFPMarkVolumeDirty(
	IN 	WORD 	wVolID
);

AFPERROR
AFPIndexBuild(
	IN	PWCHAR				pPath,
	IN	DWORD				dwParID,
	IN	PAFP_VOLUME_INFO	pVInfo
);

void
AFPAddDirectoryToIndexWorkerQueue(
	IN	PAFP_VOLUME_INFO	pVolInfo,
	IN	PWCHAR				pPath,
	IN	DWORD				dwParentID
);

LPAFPINDEX
AFPIndexAddEntry(
	IN	PAFP_VOLUME_INFO	pPassedVolInfo	OPTIONAL,
	IN	WORD				wVolID			OPTIONAL,
	IN	DWORD				dwParID,
	IN	PWCHAR				pObjectName,
	IN	PWCHAR				pObjectPath,
	IN	bool				isDirectory,
	OUT	PDWORD				pdwNewDirID
);

AFPERROR AFPIndexUpdateEntryFlags(
	IN	LPAFPINDEX			pEntry,
	IN	WORD				wFlags
);

AFPERROR
AFPIndexUpdateEntry(
	IN	PAFP_VOLUME_INFO	pVolInfo,
	IN	WORD				wVolID,
	IN	LPAFPINDEX			pEntry,
	IN	LPAFPINDEX			pNewParentEntry,
	IN	PWCHAR				pNewName
);

void AFPIndexRemoveEntry(
	IN	PAFP_VOLUME_INFO	pInfo,
	IN	LPAFPINDEX			pEntry,
	IN	bool				deleteDirectorySubTree
);

AFPERROR
AFPIndexFindEntryWithVolIDAndPath(
	IN	WORD		wVolID,
	IN	DWORD		dwDirID,
	IN	PWCHAR		pPath,
	OUT	LPAFPINDEX*	ppEntry
);

AFPERROR
AFPIndexFindEntryWithPath(
	IN	PAFP_VOLUME_INFO	pVolInfo,
	IN	DWORD				dwSearchID,
	IN	PWCHAR				pPath,
	OUT	LPAFPINDEX*			ppEntry
);

AFPERROR
AFPIndexFindEntry(
	IN	PAFP_VOLUME_INFO	pVolInfo,
	IN	DWORD				dwDirID,
	OUT LPAFPINDEX*			ppEntry
);

AFPERROR
AFPIndexGetFullPath(
	IN	PAFP_VOLUME_INFO	pVolInfo,
	IN	DWORD				dwDirID,
	IN	PWCHAR				pPath,
	IN	size_t				cbPath
);

AFPERROR
AFPVolumeWriteAccessCheck(
   IN	WORD	wVolID
);

AFPERROR
AFPCheckVolumePassword(
   IN	WORD		wVolID,
   IN	PassWord*	inPass
);

inline void AQUIRE_NOTIFY_LOCK_VOLINFO(PAFP_VOLUME_INFO volumeInfo)
{
    assert(volumeInfo != nullptr);
    EnterCriticalSection(&volumeInfo->csNotifyQueue);
}

inline void RELEASE_NOTIFY_LOCK_VOLINFO(PAFP_VOLUME_INFO volumeInfo)
{
    assert(volumeInfo != nullptr);
    LeaveCriticalSection(&volumeInfo->csNotifyQueue);
}

inline void AQUIRE_NOTIFY_LOCK_ID(const WORD volumeID)
{
    PAFP_VOLUME_INFO volumeInfo = nullptr;
    if (AFPGetVolumeInfoBlock(nullptr, volumeID, &volumeInfo))
    {
        AQUIRE_NOTIFY_LOCK_VOLINFO(volumeInfo);
    }
}

inline void RELEASE_NOTIFY_LOCK_ID(const WORD volumeID)
{
    PAFP_VOLUME_INFO volumeInfo = nullptr;
    if (AFPGetVolumeInfoBlock(nullptr, volumeID, &volumeInfo))
    {
        RELEASE_NOTIFY_LOCK_VOLINFO(volumeInfo);
    }
}

#ifdef _DEBUG

void AFPDumpAFPIndex(
	LFileIndex* index
);

#endif
