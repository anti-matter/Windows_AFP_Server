/*
 * desktop.cpp
 *
 * Copyright (c) Michael J. Conrad.
 *
 * Core AFP desktop database routines live here. This is by no
 * means the best way to implement this.
 */

#include "StdAfx.h"
#include "afpdebug.h"
#include "afphelp.h"
#include "desktop.h"
#include "volumes.h"
#include "fdparms.h"
#include "fpinfo.h"
#include "afpnotify.h"
#include "LArray.h"


/*
 * AFPInitDesktop()
 *
 * Description:
 *		Initialize the desktop database access functions.
 *
 * Returns: AFP error code.
 */

AFPERROR AFPInitDesktop(void)
{	
	return( AFP_OK );
}


/*
 * AFPDeskTopEntrySearchProc()
 *
 * Description:
 *		LArray-style search proc for finding an item.
 *
 * Returns: AFP error code.
 */

bool AFPDeskTopEntrySearchProc(void* currentItem, void* compareData)
{
	DESKTOP_ENTRY*	searchEntry		= ((DT_SEARCH_BLOB*)compareData)->compareEntry;
	WORD			wSearchIndex	= ((DT_SEARCH_BLOB*)compareData)->searchIndex;
	DESKTOP_ENTRY*	dtEntry			= (DESKTOP_ENTRY*)currentItem;
	WORD			wFoundIndex		= 0;
	bool			fFound			= false;
		
	if (searchEntry->entryType == dtEntry->entryType)
	{
		switch(dtEntry->entryType)
		{
			case ENTRY_TYPE_APPL:				
				if (searchEntry->fileCreator == dtEntry->fileCreator)
				{
					wFoundIndex++;
					
					if (wFoundIndex >= wSearchIndex)
					{
						DBGWRITE(DbgLevelInfo, L"[desk]Found APPL entry!\n");
						fFound = TRUE;
					}
				}
				break;

			case ENTRY_TYPE_CMNT:				
				if ((searchEntry->dirID == dtEntry->dirID)				&&
					(!lstrcmp(searchEntry->pathname, dtEntry->pathname))	)
				{
					wFoundIndex++;
					
					if (wFoundIndex >= wSearchIndex)
					{
						DBGWRITE(DbgLevelInfo, L"[desk]Found CMNT entry!\n");
						fFound = TRUE;
					}
				}
				break;

			case ENTRY_TYPE_ICON:				
				if (searchEntry->fileCreator == dtEntry->fileCreator)
				{
					//
					//This means we're in a search for FPGetIconInfo
					//
					if (wSearchIndex != 0)
					{
						wFoundIndex++;
						
						if (wFoundIndex >= wSearchIndex)
						{
							DBGWRITE(DbgLevelInfo, L"[desk]Found ICON entry!\n");
							fFound = TRUE;
						}
					}
					//
					//Nope, just a normal search for an existing icon.
					//
					else
					{
						if ((searchEntry->fileType 	 == dtEntry->fileType) 	&&
							(searchEntry->iconType 	 == dtEntry->iconType)	)
						{
							wFoundIndex++;
							
							if (wFoundIndex >= wSearchIndex)
							{
								DBGWRITE(DbgLevelInfo, L"[desk]Found ICON entry!\n");
								fFound = TRUE;
							}
						}
					}
				}
				break;

			default:
				break;
		}
	}
	
	return( fFound );
}


/*
 * AFPFindEntry()
 *
 * Description:
 *		Find an entry in the database associated with the supplied criteria.
 *
 * Returns: AFP error code.
 */

AFPERROR AFPFindEntry(
	IN				LPDESKTOP_ENTRY	searchEntry,
	IN				WORD			wSearchIndex,
	IN				OPEN_DESKTOP*	deskFile,
	OUT	OPTIONAL	DWORD*			dwEntryPosition,
	OUT				LPDESKTOP_ENTRY	foundEntry
)
{	
	PAFP_VOLUME_INFO	pInfo 		= NULL;
	array_index			foundIndex	= 0;
	DT_SEARCH_BLOB		searchBlob;

	if (!AFPGetVolumeInfoBlock(NULL, deskFile->volID, &pInfo))
	{
		//
		//We failed to find a block corresponding to the
		//supplied volume ID.
		//
		DBGWRITE(DbgLevelInfo, L"[desk]Bad volume ID in AFPFindEntry()\n");	
		return(afpParmErr);
	}
	
	searchBlob.compareEntry	= searchEntry;
	searchBlob.searchIndex	= wSearchIndex;
	
	EnterCriticalSection(&pInfo->csDesktopDB);
	
	foundIndex = pInfo->dtArray->Search(&searchBlob, AFPDeskTopEntrySearchProc);
	
	if (foundIndex != LArray::array_index_invalid)
	{
		DBGWRITE(DbgLevelInfo, L"[desk]Found position = %d\n", foundIndex);
		
		pInfo->dtArray->GetItem(foundIndex, foundEntry);
		
		if (dwEntryPosition != NULL)
		{
			*dwEntryPosition = (DWORD)foundIndex;
		}

		LeaveCriticalSection(&pInfo->csDesktopDB);
		
		return( AFP_OK );
	}

	LeaveCriticalSection(&pInfo->csDesktopDB);
	
#ifdef _DEBUG
	auto const itemType = searchEntry->entryType == ENTRY_TYPE_ICON ? L"ICON" : L"APPLorCMNT";
	DBGWRITE(DbgLevelInfo, L"[desk]Failed to find item in desktop DB! (type = %s)\n", itemType);
#endif

	return( afpItemNotFound );
}


/*
 * AFPAddEntry()
 *
 * Description:
 *		Add a new entry to the desktop database.
 *
 * Returns: AFP error code.
 */

AFPERROR AFPAddEntry(
	IN	LPDESKTOP_ENTRY	newEntry,
	IN	OPEN_DESKTOP*	deskFile
	)
{
	DESKTOP_ENTRY		foundEntry;
	PAFP_VOLUME_INFO	pInfo			= NULL;
	AFPERROR			afpError		= AFP_OK;
	uint32				foundPosition	= 0;


	ASSERT(deskFile != NULL);

	if (!AFPGetVolumeInfoBlock(NULL, deskFile->volID, &pInfo))
	{
		//
		//We failed to find a block corresponding to the
		//supplied volume ID.
		//
		DBGWRITE(DbgLevelInfo, L"[desk]Bad volume ID in AFPAddEntry()\n");		
		return(afpParmErr);
	}

	//
	//Search to see if the data we want to add already exists.
	//
	afpError = AFPFindEntry(newEntry, DONT_USE_INDEX, deskFile, &foundPosition, &foundEntry);

	EnterCriticalSection(&pInfo->csDesktopDB);
	
	//
	//If we find an entry in the data base the matches the new entry, we
	//replace the existing data with the new.
	//
	if (AFP_SUCCESS(afpError))
	{
		//
		//For icon entries, we only replace existing entries if the icon
		//bitmap is the same size.
		//
		if (newEntry->entryType == ENTRY_TYPE_ICON)
		{
			if (newEntry->dataSize != foundEntry.dataSize)
			{
				LeaveCriticalSection(&pInfo->csDesktopDB);
				return( afpIconTypeError );
			}
		}
		
		DBGWRITE(DbgLevelInfo, L"[desk]Removing and replacing icon...\n");
		
		pInfo->dtArray->RemoveItem(foundPosition);
	}
	else if (afpError != afpItemNotFound)
	{
		//
		//Any other failure means we couldn't access the db at all.
		//
		ASSERT(0);
		
		LeaveCriticalSection(&pInfo->csDesktopDB);
		return( afpParmErr );
	}
	
	pInfo->dtArray->AddItem(newEntry);
	
	DBGWRITE(DbgLevelInfo, L"[desk]Added new item to array!\n");
	
	LeaveCriticalSection(&pInfo->csDesktopDB);

	return( AFP_OK );
}


/*
 * AFPRemoveEntry()
 *
 * Description:
 *		Remove an entry from the desktop database.
 *
 * Returns: AFP error code.
 */

AFPERROR AFPRemoveEntry(
	IN	LPDESKTOP_ENTRY	removeEntry,
	IN	OPEN_DESKTOP*	deskFile
	)
{
	DESKTOP_ENTRY		foundEntry;
	PAFP_VOLUME_INFO	pInfo			= NULL;
	AFPERROR			afpError		= AFP_OK;
	uint32				foundPosition	= 0;

	ASSERT(deskFile != NULL);

	if (!AFPGetVolumeInfoBlock(NULL, deskFile->volID, &pInfo))
	{
		//
		//We failed to find a block corresponding to the
		//supplied volume ID.
		//
		DBGWRITE(DbgLevelInfo, L"[desk]Bad volume ID in AFPAddEntry()\n");		
		return(afpParmErr);
	}

	//
	//Find where the entry resides in the database so we know where
	//to overwrite with empty data.
	//
	afpError = AFPFindEntry(removeEntry, DONT_USE_INDEX, deskFile, &foundPosition, &foundEntry);

	//
	//If we find the entry, we need to zero the data and set it's
	//entry type to "empty" so we can recycle the space in the file.
	//
	if (AFP_SUCCESS(afpError))
	{
		EnterCriticalSection(&pInfo->csDesktopDB);
		
		DBGWRITE(DbgLevelInfo,
				L"[desk]Removing item from DT array at position %d\n",
				foundPosition
				);
		
		pInfo->dtArray->RemoveItem(foundPosition);
		
		LeaveCriticalSection(&pInfo->csDesktopDB);
	}

	return( afpError );
}


/*
 * AFPOpenDeskTopDB()
 *
 * Description:
 *		Opens the desktop database.
 *
 * Returns: AFP error code.
 */

AFPERROR AFPOpenDeskTopDB(
	IN	WORD			wVolID,
	IN	OPEN_DESKTOP*	deskFile,
	OUT	PWORD			pwDTRefNum
)
{
	PAFP_VOLUME_INFO	pInfo;
	TCHAR				szPath[MAX_PATH];
	AFPERROR			afpError	= AFP_OK;
	bool				fileExists	= false;
	HANDLE				hFile		= INVALID_HANDLE_VALUE;
	
	if (!AFPGetVolumeInfoBlock(NULL, wVolID, &pInfo))
	{
		//
		//We failed to find a block corresponding to the
		//supplied volume ID.
		//
		DBGWRITE(DbgLevelInfo, L"[VOL]Bad volume ID in AFPOpenDeskTopDB()\n");		
		return(afpParmErr);
	}
	
	//
	//Build the path to our desktop file for this volume.
	//
	StringCbPrintf(szPath, sizeof(szPath), TEXT("%s\\%s"), pInfo->szPath, DESKTOP_FILE_NAME);
	
	fileExists = (GetFileAttributes(szPath) == MAXDWORD) ? false : true;
	
	AQUIRE_NOTIFY_LOCK_VOLINFO(pInfo);

	hFile = CreateFile(
				szPath,
				GENERIC_READ | GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE,
				NULL,
				OPEN_ALWAYS,
				FILE_ATTRIBUTE_HIDDEN,
				NULL
	);
	
	if (hFile == INVALID_HANDLE_VALUE)
	{
		DBGWRITE(DbgLevelInfo, L"[DESK]CreateFile() failed opening DT! (%d)\n", GetLastError());
		afpError = AFPMapWinErrorToAFP(GetLastError(), static_cast<DWORD>(afpParmErr));
	}

	if ((AFP_SUCCESS(afpError)) && (!fileExists))
	{
		LPAFPINDEX	pEntry = NULL;

		//
		//If we create a new file above, then we also need to add the
		//desktop file to the index.
		//
		pEntry = AFPIndexAddEntry(
						NULL,
						wVolID,
						kRootDirID,
						DESKTOP_FILE_NAME,
						szPath,
						FALSE,
						NULL
						);
		
		if (pEntry != NULL)
		{
			AFPCheckForAndAddFinderInfoStream(pEntry, NULL, true);
			AFPCheckForAndCreateFinderRsrcStream(pEntry);
		}
		else
		{
			afpError = afpParmErr;
		}
	}
	
	RELEASE_NOTIFY_LOCK_VOLINFO(pInfo);

	if (AFP_SUCCESS(afpError))
	{
		DWORD	dwFileSize 	= 0;
		DWORD	dwBytesRead	= 0;
		
		
		deskFile->volID		= wVolID;
		deskFile->dtRefNum	= wVolID;
		
		if (pInfo->dtArray == nullptr)
		{
			afpError = afpMiscErr;
			
			//
			//Initialize the array object that will hold the desktop data.
			//
			dwFileSize = GetCompressedFileSize(szPath, nullptr);
			
			if (dwFileSize == 0xFFFFFFFF)
			{
				dwFileSize = 0;
				DBGWRITE(DbgLevelInfo, L"[desk]GetCompressedFileSize() failed! (%d)\n", GetLastError());
			}
			else
			{
				if (dwFileSize > 0)
				{
					auto readBuf = std::unique_ptr<uint8>(new uint8[dwFileSize]);
					
					if (ReadFile(hFile, readBuf.get(), dwFileSize, &dwBytesRead, nullptr))
					{
						//
						//Initialize the array the *is* the desktop db. Note that this
						//LArray constructor automatically calls Deserialize() on the 
						//buffer.
						//
						pInfo->dtArray	= new LArray(readBuf);
						afpError		= AFP_OK;
							
						DBGWRITE(DbgLevelInfo,
								L"[desk]Reading in array complete! (num entries = %d)\n",
								pInfo->dtArray->NumItems()
								);
					}
				}
				else
				{
					//
					//The file was just created and is 0 bytes, create an
					//empty array.
					//
					DBGWRITE(DbgLevelInfo,
							L"[desk]Must be new file, creating empty array...\n"
							);
					
					pInfo->dtArray 	= new LArray(LARRAY_DEFAULT_GROW_SIZE, sizeof(DESKTOP_ENTRY));
					afpError		= AFP_OK;
				}
			}
		}
		
		if (!AFP_SUCCESS(afpError))
		{
			DBGWRITE(DbgLevelInfo,
					L"[desk]Failed to read in data or initialize array! (%d)\n",
					GetLastError()
					);
		}
		else
		{
			*pwDTRefNum = deskFile->dtRefNum;
		}
	}

	if (hFile != INVALID_HANDLE_VALUE) 
	{
		CloseHandle(hFile);
	}

	return( afpError );
}


/*
 * AFPSynchronizeDesktopFile()
 *
 * Description:
 *		Synchronize the desktop file on the disk to the one in memory.
 *		This is a fairly expensive operation, you don't want to do it
 *		too frequentyly. However, you don't want to not do it too often
 *		as data loss may result if the server crashes and the lastest data
 *		hasn't been synched up yet.
 *
 * Returns: none
 */

void AFPSynchronizeDesktopFile(
	IN	WORD	wVolID
)
{
	PAFP_VOLUME_INFO	pInfo = nullptr;
	WCHAR				szPath[MAX_PATH];
	
	if (!AFPGetVolumeInfoBlock(nullptr, wVolID, &pInfo))
	{
		//
		//We failed to find a block corresponding to the
		//supplied volume ID.
		//
		DBGWRITE(DbgLevelInfo, L"[desk]Bad volume ID in AFPSynchronizeDesktopFile()\n");		
		return;
	}
	
	StringCbPrintf(szPath, sizeof(szPath), TEXT("%s\\%s"), pInfo->szPath, DESKTOP_FILE_NAME);

	pInfo->dtArray->ToFile(szPath);
}


/*
 * AFPCloseDeskTopDB()
 *
 * Description:
 *		Closes the desktop database.
 *
 * Returns: AFP error code.
 */

AFPERROR AFPCloseDeskTopDB(
	IN	OPEN_DESKTOP*	deskFile
)
{
	AFPSynchronizeDesktopFile(deskFile->volID);
	return(NO_ERROR);
}



