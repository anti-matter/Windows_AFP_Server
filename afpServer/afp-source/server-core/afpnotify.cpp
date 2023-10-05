//===================================================================
// afpnotify.c
//
// This unit contains the change notfiction mechanism that updates
// the afp server when a windows client changes files/folders on
// the afp volume.
//===================================================================

#include "stdafx.h"

#include "afpnotify.h"
#include "afpdebug.h"
#include "afphelp.h"
#include "afp.h"
#include "volumes.h"
#include "desktop.h"
#include "fdparms.h"
#include "fpinfo.h"

#include <wchar.h>
#include <cstdlib>

#define CHNG_NOTIFY_RESULT_BUFFER_SIZE	(64*1024)

extern SHORT	g_worker_thread_ref_count;
extern BOOL		g_running;

//---------------------------------------------------------------------
// AFPNotifyThread()
//---------------------------------------------------------------------
// Thread that waits for change notifications on the volumes. You need
// to have one of these threads for each volume in the afp server.
//
// pParam	Contains a ptr to the AFP_VOLUME_INFO* of the volume to watch.
//

DWORD WINAPI AFPNotifyThread(
		IN LPVOID	pParam
)
{
	HANDLE				hDirectory		= INVALID_HANDLE_VALUE;
	PBYTE				pResultBuffer	= NULL;
	AFP_VOLUME_INFO*	pVolInfo		= (AFP_VOLUME_INFO*)pParam;

	//
	//For worker thread tracking...
	//
	g_worker_thread_ref_count++;

	//
	//Get a handle to the directory we'll be watch for changes.
	//
	hDirectory = CreateFile(
						pVolInfo->szPath,
						FILE_LIST_DIRECTORY,
						FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
						NULL,
						OPEN_EXISTING,
						FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
						NULL
	);

	if (hDirectory == INVALID_HANDLE_VALUE)
	{
		DBGWRITE(	dbg_level_info,
					L"[NOTIFY]Error opening directory at %s (%d)\n",
					pVolInfo->szPath,
					GetLastError()	);
		return(0);
	}

	//
	//Create the buffer that will hold the return data from the notify call.
	//
	pResultBuffer = (PBYTE)DBG_ALLOC(CHNG_NOTIFY_RESULT_BUFFER_SIZE);

	if (pResultBuffer == NULL)
	{
		DBGWRITE(	dbg_level_info,
					L"[NOTIFY]Error allocating result buffer for %s (%d)\n",
					pVolInfo->szPath,
					GetLastError()	);

		CloseHandle(hDirectory);
		return(0);
	}
	
	//
	//Increment volumes ref count so it doesn't go away while we're still
	//expecting it to be there.
	//
	pVolInfo->dwRefCount++;

	while((g_running) && (pVolInfo->wVolumeFlags & FLAG_VOLUME_SHARED))
	{
		AFPReadDirChanges(pVolInfo, hDirectory, pResultBuffer);
		ZeroMemory(pResultBuffer, CHNG_NOTIFY_RESULT_BUFFER_SIZE);
	}
	
	pVolInfo->dwRefCount--;

	DBG_FREE(pResultBuffer);
	CloseHandle(hDirectory);

	g_worker_thread_ref_count--;

	DBGWRITE(dbg_level_info, L"[NOTIFY]Notify thread gone for %s\n", pVolInfo->szShareName);
	return(0);
}


//---------------------------------------------------------------------
// AFPReadDirChanges()
//---------------------------------------------------------------------
// Read directory changes for a given directory.
//

void AFPReadDirChanges(
	IN	AFP_VOLUME_INFO*	pVolInfo,
	IN	HANDLE				hDirectory,
	IN	PBYTE				pResultBuffer
)
{
	DWORD		dwNumBytesReturned	= 0;
	DWORD		dwNotifyFilter		= FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME;
	OVERLAPPED	overlapped;

	overlapped.hEvent 		= CreateEvent(NULL, FALSE, TRUE, NULL);
	pVolInfo->notifyEvent	= overlapped.hEvent;

	//
	//This sucks, but we MUST do it each time or we'll get
	//bogus data.
	//
	ZeroMemory(pResultBuffer, CHNG_NOTIFY_RESULT_BUFFER_SIZE);

	//
	//This call will put the thread into a wait state until an change
	//happens to our directory.
	//
	if (ReadDirectoryChangesW(
				hDirectory,
				pResultBuffer,
				CHNG_NOTIFY_RESULT_BUFFER_SIZE,
				TRUE,
				dwNotifyFilter,
				&dwNumBytesReturned,
				&overlapped,
				NULL))
	{
		while (!GetOverlappedResult(hDirectory, &overlapped, &dwNumBytesReturned, FALSE))
		{
			if ((GetLastError() != ERROR_IO_INCOMPLETE) || (!g_running) || (!(pVolInfo->wVolumeFlags & FLAG_VOLUME_SHARED)))
			{
				DBGWRITE(	dbg_level_info, 
							L"[NOTIFY]ReadDirectoryChangesW() failed during async! (%d)\n",
							GetLastError()
							);

				CloseHandle(overlapped.hEvent);
				return;
			}
			else
			{
				Sleep(5);
			}
		}

		//
		//If the number of bytes transferred is 0, then the buffer we gave to
		//ReadDirectoryChangesW() was too small to hold the change data. We're
		//supposed to handle this now by enumerating the directory structure
		//manually.
		//
		if (dwNumBytesReturned == 0)
		{
			DBGWRITE(dbg_level_info, L"[NOTIFY]0 bytes transferred in change! (%s)\n", pVolInfo->szPath);
		}
		else
		{
			FILE_NOTIFY_INFORMATION*	pInfo	= (FILE_NOTIFY_INFORMATION*)pResultBuffer;
			LPAFPINDEX					pEntry	= NULL;
			WCHAR						szFileName[MAX_PATH];
			WCHAR						szNewName[64];

			//
			//We have a successful change notify. Flip through the array of
			//changes and take appropriate action depending on the change.
			//
			DBGWRITE(dbg_level_all, L"[NOTIFY]Change notify data received!! (%d bytes)\n", dwNumBytesReturned);
			DBGWRITE(dbg_level_all, L"[NOTIFY]NextEntryOffset = %d\n", pInfo->NextEntryOffset);

			do
			{
				switch(pInfo->Action)
				{
					case FILE_ACTION_ADDED:
						AFPChngNotifyAdd(pInfo, pVolInfo);
						break;

					case FILE_ACTION_REMOVED:
						AFPChngNotifyRemove(pInfo, pVolInfo);
						break;

					case FILE_ACTION_MODIFIED:
						break;

					//
					//An object is being renamed. Here is where we are given a chance
					//to get a grip on the object before the rename actually happend.
					//
					case FILE_ACTION_RENAMED_OLD_NAME:
						lstrcpyn(szFileName, pInfo->FileName, pInfo->FileNameLength);

						DBGWRITE(	dbg_level_all, 
									L"[NOTIFY]File renamed, old name = %s\n", 
									szFileName);

						//
						//Null this out so we'll know in the "new" phase below if we
						//succeeded in finding the file object that changed.
						//
						pEntry = NULL;

						EnterCriticalSection(&pVolInfo->csNotifyQueue);

						//
						//Find the index entry to the file object who's name is changing.
						//
						AFPIndexFindEntryWithPath(
								pVolInfo,
								kRootDirID,
								szFileName,
								&pEntry
						);

						LeaveCriticalSection(&pVolInfo->csNotifyQueue);
						break;

					//
					//The object is now renamed. Update the afp index with the
					//newly changed name.
					//
					case FILE_ACTION_RENAMED_NEW_NAME:
						lstrcpyn(szFileName, pInfo->FileName, pInfo->FileNameLength);

						DBGWRITE(	dbg_level_all, 
									L"[NOTIFY]File renamed, new name = %s\n", 
									szFileName);
						//
						//If pEntry is not NULL, then that means we succeeded above
						//in getting the index entry to modify above.
						//
						if (pEntry != NULL)
						{
							AFPCHARMAPARRAY	charmap;
							
							EnterCriticalSection(&pVolInfo->csNotifyQueue);

							ZeroMemory(szNewName, sizeof(szNewName));
							AFPGetObjectNameFromPath(szFileName, szNewName, sizeof(szNewName));

							AFPIndexUpdateEntry(
									pVolInfo,
									pVolInfo->wVolID,
									pEntry,
									(LPAFPINDEX)pEntry->parEntry,
									szFileName
							);
							
							//
							//If the object was renamed on the windows box then all bets are
							//off and there's no way to preserve the character mapping. So,
							//clear the charmap and hope for the best.
							//
							memset(charmap, 0, sizeof(AFPCHARMAPARRAY));
							
							AFPAddCharMapToIndexEntry(pEntry, charmap);
							AFPMarkVolumeDirty(pVolInfo->wVolID);

							LeaveCriticalSection(&pVolInfo->csNotifyQueue);
						}
						break;

					default:
						ASSERT( 0 );
						break;
				}

				if (pInfo->NextEntryOffset == 0)
				{
					break;
				}

				pInfo = (FILE_NOTIFY_INFORMATION*)((PBYTE)pInfo + pInfo->NextEntryOffset);

			}while(TRUE);
		}
	}
	else
	{
		DBGWRITE(dbg_level_info, L"[NOTIFY]ReadDirectoryChangesW() failed! (%d)\n", GetLastError());
	}

	CloseHandle(overlapped.hEvent);
}


//---------------------------------------------------------------------
// AFPChngNotifyAdd()
//---------------------------------------------------------------------
// Respond to a add change notify event by adding the new file object
// to the afp index.
//

void AFPChngNotifyAdd(
	IN	FILE_NOTIFY_INFORMATION*	pInfo,
	IN	AFP_VOLUME_INFO*			pVolInfo
)
{
	TCHAR						szNewItemPathname[MAX_PATH];
	TCHAR						szTempPath[MAX_PATH];
	TCHAR						szNewItemName[MAX_PATH];
	DWORD						dwNewDirID;
	DWORD						dwError;
	LPAFPINDEX					pExisting	= NULL;
	LPAFPINDEX					pEntry		= NULL;
	WIN32_FILE_ATTRIBUTE_DATA	attribData;

	//
	//Initialize our string buffers that need it.
	//
	ZeroMemory(szTempPath, sizeof(szTempPath));
	ZeroMemory(szNewItemName, sizeof(szNewItemName));

	//
	//Copy the pathname (path plus object name) into a null terminated
	//string buffer for later use.
	//
	lstrcpyn(
		szNewItemPathname,
		pInfo->FileName,
		(pInfo->FileNameLength/sizeof(TCHAR))+1
		);

	//
	//Extract just the name of the new file object from the pathname.
	//
	AFPGetObjectNameFromPath(szNewItemPathname, szNewItemName, sizeof(szNewItemName));

	//
	//First step: we need to find the index entry that is the parent
	//directory of the new object we're adding.
	//
	AFPGetObjectPath(szNewItemPathname, szTempPath);

	DBGWRITE(dbg_level_all, L"[NOTIFY-ADD]New item pathname = %s\n", szNewItemPathname);
	DBGWRITE(dbg_level_all, L"[NOTIFY-ADD]Parent dir path = %s\n", szTempPath);

	//
	//We may have AFP modifying the index entry for the new object
	//already, wait around.
	//
	EnterCriticalSection(&pVolInfo->csNotifyQueue);

	//
	//Search the afp index for the entry corresponding to the parent
	//directory for the new item.
	//
	dwError = AFPIndexFindEntryWithPath(pVolInfo, kRootDirID, szTempPath, &pEntry);

	if (dwError != NO_ERROR)
	{
		//
		//If we fail to find the parent directory's index entry, there's no
		//reason to continue.
		//
		DBGWRITE(
			dbg_level_info,
			L"[NOTIFY-ADD]AFPIndexFindEntryWithPath() failed (%d) (%s)\n", dwError, szTempPath);

		LeaveCriticalSection(&pVolInfo->csNotifyQueue);
		return;
	}

	//
	//Build the real full path to the new file object. pEntry now contains
	//the entry in the index for the parent directory.
	//
	ZeroMemory(szTempPath, sizeof(szTempPath));
	StringCbPrintf(szTempPath, sizeof(szTempPath), TEXT("%s\\%s"), pEntry->szPath, szNewItemName);

	DBGWRITE(dbg_level_all, L"[NOTIFY-ADD]Full path to new item = %s\n", szTempPath);

	//
	//See if there is already an index entry for this object. If so, then we
	//don't need to do anything.
	//
	dwError = AFPIndexFindEntryWithPath(pVolInfo, kRootDirID, szNewItemPathname, &pExisting);

	DBGWRITE(dbg_level_all, L"[NOTIFY-ADD]AFPIndexFindEntryWithPath returned: %d\n", dwError);

	if (dwError != NO_ERROR)
	{
		//
		//Get the file attributes for the new file object. We use these to
		//determine if the object is a file or directory.
		//
		if (!GetFileAttributesEx(szTempPath, GetFileExInfoStandard, &attribData))
		{
			//
			//This would be really bad as we couldn't find the object
			//we're supposedly adding!
			//
			DBGWRITE(dbg_level_info, L"[NOTIFY-ADD]Item not found on disk!\n");
		}
		else
		{
			//
			//Finally, add the new file object into the afp index.
			//
			pEntry = AFPIndexAddEntry(
							pVolInfo,
							pVolInfo->wVolID,
							pEntry->objectID,
							szNewItemName,
							szTempPath,
							((attribData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0),
							&dwNewDirID
			);
			
			if (pEntry == NULL)
			{
				DBGWRITE(dbg_level_info, L"[NOTIFY-ADD]AFPIndexAddEntry() failed!\n");
			}
			else
			{
				DBGWRITE(dbg_level_all,
						(L"[NOTIFY-ADD]Added new item in notify id=%d name=%s\n",
						pEntry->objectID,
						szNewItemPathname
						));

				//
				//Add the finder info stream to the file or directory if
				//it doesn't already exist.
				//
				AFPCheckForAndAddFinderInfoStream(pEntry);

				if (attribData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				{
					//
					//If the item is a directory, we add all of its contents
					//to the index as well.
					//
					AFPAddDirectoryToIndexWorkerQueue(pVolInfo, szTempPath, pEntry->objectID);
				}
				else
				{
					//
					//Add the resource streams if necessary for files.
					//
					AFPCheckForAndCreateFinderRsrcStream(pEntry);
				}

				//
				//Mark the volume dirty so it will tell any connected macs
				//that the volume contents have changed.
				//
				AFPMarkVolumeDirty(pVolInfo->wVolID);
			}
		}
	}
	else
	{
		DBGWRITE(dbg_level_all, L"[NOTIFY-ADD]Object already exists in index\n");
	}

	LeaveCriticalSection(&pVolInfo->csNotifyQueue);
}


//---------------------------------------------------------------------
// AFPChngNotifyRemove()
//---------------------------------------------------------------------
// Respond to a remove change notify event by removing the file object
// from the afp index.
//

void AFPChngNotifyRemove(
	IN	FILE_NOTIFY_INFORMATION*	pInfo,
	IN	AFP_VOLUME_INFO*			pVolInfo
)
{
	TCHAR						szItemPathname[MAX_PATH];
	DWORD						dwError		= NO_ERROR;
	LPAFPINDEX					pEntry		= NULL;
	WIN32_FILE_ATTRIBUTE_DATA	attribData;

	//
	//Copy the pathname (path plus object name) into a null terminated
	//string buffer for later use.
	//
	lstrcpyn(szItemPathname, pInfo->FileName, pInfo->FileNameLength);

	DBGWRITE(dbg_level_all, L"[NOTIFY-RMV]Removing %s\n", szItemPathname);

	//
	//Check to see if we're supposed to ignore this change notify.
	//
	EnterCriticalSection(&pVolInfo->csNotifyQueue);

	//
	//Search the afp index for the entry corresponding to the removed object.
	//
	dwError = AFPIndexFindEntryWithPath(pVolInfo, kRootDirID, szItemPathname, &pEntry);

	if (dwError != NO_ERROR)
	{
		//
		//If we fail to find the parent directory's index entry, there's no
		//reason to continue.
		//
		DBGWRITE(
			dbg_level_all,
			L"[NOTIFY-RMV]AFPIndexFindEntryWithPath() failed (%d)\n", dwError
			);

		LeaveCriticalSection(&pVolInfo->csNotifyQueue);
		return;
	}

	//
	//Now, we have to make sure the object doesn't exist on the disk. We do this
	//in case a new object with same name was created immediately after the old
	//one was deleted. This normally can only happen by batch file or stress
	//applications.
	//
	if (GetFileAttributesEx(pEntry->szPath, GetFileExInfoStandard, &attribData))
	{
		//
		//This would be really bad as we couldn't find the object
		//we're supposedly adding!
		//
		DBGWRITE(dbg_level_info, L"[NOTIFY-RMV]Object still exists on disk! Leaving in index.\n");
	}
	else
	{
		//
		//Now, make the call into the index utility routines to actually remove
		//the item from the index list.
		//
		AFPIndexRemoveEntry(pVolInfo, pEntry, TRUE);

		//
		//Tell the attention mechanism to notify clients the volume has changed.
		//
		AFPMarkVolumeDirty(pVolInfo->wVolID);
	}

	LeaveCriticalSection(&pVolInfo->csNotifyQueue);
}
