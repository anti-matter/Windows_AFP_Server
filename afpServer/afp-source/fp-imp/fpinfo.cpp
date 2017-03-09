/*
 * fpinfo.cpp
 *
 * Copyright(c) 2000-2004, Michael J. Conrad.
 *
 * Finder info and related char map routines.
 *
 */

#include "StdAfx.h"
#include "fpinfo.h"
#include "afpxattr.h"


/*
 * AFPCheckForAndAddFinderInfoStream()
 *
 * Description:
 *		See if the file object has a file info stream and add
 *		it if not.
 *
 * Returns: TRUE if stream already existed
 */

BOOL AFPCheckForAndAddFinderInfoStream(
	IN LPAFPINDEX pEntry,
	   IN AFPCHARMAPARRAY pCharmap,
	   IN BOOL fMakeInvisible
)
{
	EXTENDED_FINDER_INFO finderInfo;

	if (!AFP_SUCCESS(AFPGetFinderInfoEx(pEntry, &finderInfo)))
	{
		memset(&finderInfo, 0, sizeof(finderInfo));

		auto dwAttributes = GetFileAttributes(pEntry->szPath);

		if (((dwAttributes != 0xFFFFFFFF) &&
				(dwAttributes & FILE_ATTRIBUTE_HIDDEN)) ||
			(fMakeInvisible))
		{
			finderInfo.fInfo.fdFlags |= kFDInvisible;
		}

		finderInfo.fInfo.fdLocation.x = 20;
		finderInfo.fInfo.fdLocation.y = 20;

		//
		//Copy the charMap into the extended finder info structure.
		//
		if (pCharmap != nullptr)
		{
			RtlCopyMemory(&finderInfo.charMap, pCharmap, sizeof(AFPCHARMAPARRAY));
		}

		//
		//If an item didn't have a finder info stream before, set the
		//defaults and add one now.
		//
		AFPSetFinderInfoEx(pEntry, &finderInfo, true);

		return (false);
	}

	return (true);
}


/*
 * AFPGetFinderInfo()
 *
 * Description:
 *		Returns the FINDER_INFO from the file stream.
 *
 * Returns: AFPERROR
 */

AFPERROR AFPGetFinderInfo(
	IN LPAFPINDEX pEntry,
	   OUT PFINDER_INFO pInfo
)
{
	EXTENDED_FINDER_INFO infoEx;

	auto afpError = AFPGetFinderInfoEx(pEntry, &infoEx);

	if (AFP_SUCCESS(afpError))
	{
		RtlCopyMemory(pInfo, &infoEx, sizeof(FINDER_INFO));
	}

	return (afpError);
}


/*
 * AFPGetFinderInfoEx()
 *
 * Description:
 *		Returns the FINDER_INFO from the file stream plus the
 *		extra data we store at the end of the block. We use
 *		the finfo stream to also store character map data which
 *		is used to map non-windows characters.
 *
 * Returns: AFPERROR
 */

AFPERROR AFPGetFinderInfoEx(
	IN LPAFPINDEX pEntry,
	   OUT LPEXTENDED_FINDER_INFO pInfo
)
{
	DWORD dwError = NO_ERROR;
	DWORD dwBytesRead;
	TCHAR szPath[MAX_PATH];

	//
	//First, build the path to the finder info stream.
	//
	ASSERT(lstrlen(pEntry->szPath) > 0);
	StringCbPrintf(szPath, sizeof(szPath), TEXT("%s:%s"), pEntry->szPath, AFP_INFO_STREAM_NAME);

	//
	//Now open the AFP_Info stream to write to it.
	//
	auto hFile = CreateFile(
		szPath,
		GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr
	);

	if (hFile == INVALID_HANDLE_VALUE)
	{
		dwError = GetLastError();
		DBGWRITE(DbgLevelAll, L"[fpinfo]CreatFile() failed in AFPGetFinderInfo! (%d) (%s)\n", dwError, szPath);

		return AFPMapWinErrorToAFP(dwError, static_cast<DWORD>(afpMiscErr));
	}

	//
	//Write out the finder info.
	//
	if (!ReadFile(hFile, pInfo, sizeof(EXTENDED_FINDER_INFO), &dwBytesRead, nullptr))
	{
		//
		//Failure, get the error code and get out of here.
		//
		dwError = GetLastError();
		DBGWRITE(DbgLevelInfo, L"[fpinfo]ReadFile() failed! (%d)\n", dwError);

		dwError = AFPMapWinErrorToAFP(dwError, static_cast<DWORD>(afpMiscErr));
	}

	CloseHandle(hFile);

	return dwError;
}


/*
 * AFPSetFinderInfo()
 *
 * Description:
 *		Store FINDER_INFO data in the finfo stream.
 *
 * Returns: AFPERROR
 */

AFPERROR AFPSetFinderInfo(
	IN LPAFPINDEX pEntry,
	   IN PFINDER_INFO pInfo,
	   IN BOOL fCreateAlways
)
{
	EXTENDED_FINDER_INFO infoEx;

	if (fCreateAlways)
	{
		memset(&infoEx, 0, sizeof(EXTENDED_FINDER_INFO));
		RtlCopyMemory(&infoEx, pInfo, sizeof(FINDER_INFO));
	}
	else
	{
		auto afpError = AFPGetFinderInfoEx(pEntry, &infoEx);

		if (AFP_SUCCESS(afpError))
		{
			RtlCopyMemory(&infoEx, pInfo, sizeof(FINDER_INFO));
		}
	}

	return AFPSetFinderInfoEx(pEntry, &infoEx, fCreateAlways);
}


/*
 * AFPSetFinderInfoEx()
 *
 * Description:
 *		Store FINDER_INFO data in the finfo stream plus extended
 *		data used by the file server.
 *
 * Returns: AFPERROR
 */

AFPERROR AFPSetFinderInfoEx(
	IN LPAFPINDEX pEntry,
	   IN LPEXTENDED_FINDER_INFO pInfo,
	   IN BOOL fCreateAlways
)
{
	HANDLE hFile;
	DWORD dwError = NO_ERROR;
	DWORD dwBytesWritten = 0;
	TCHAR szPath[MAX_PATH];

	ASSERT(pEntry != NULL);
	ASSERT(lstrlen(pEntry->szPath) > 0);

	//
	//First, build the path to the finder info stream.
	//
	StringCbPrintf(szPath, sizeof(szPath), TEXT("%s:%s"), pEntry->szPath, AFP_INFO_STREAM_NAME);

	for (;;)
	{
		//
		//Now open the AFP_Info stream to write to it.
		//
		hFile = CreateFile(
			szPath,
			GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			nullptr,
			fCreateAlways ? OPEN_ALWAYS : OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			nullptr
		);

		if (hFile == INVALID_HANDLE_VALUE)
		{
			dwError = GetLastError();

			//
			//Sometimes we have to wait for a copy operation to complete before we can
			//open and muck with the file. This ocurrs mainly during copy operations performed
			//on the windows machine.
			//
			if (dwError == ERROR_SHARING_VIOLATION)
			{
				DBGWRITE(DbgLevelInfo, L"[fpinfo:AFPSetFinderInfoEx]Sleeping for 100ns for notify wait!\n");

				Sleep(500);

				//
				//Reset the error code so we won't fail out below.
				//
				dwError = NO_ERROR;
				continue;
			}
			else
			{
				DBGWRITE(DbgLevelInfo,
					L"[fpinfo:AFPSetFinderInfoEx]CreatFile() failed! (%d) (%s)\n",
					dwError,
					szPath);
			}

			dwError = AFPMapWinErrorToAFP(dwError, static_cast<DWORD>(afpMiscErr));
		}

		break;
	}

	if (dwError == NO_ERROR)
	{
		//
		//Write out the finder info.
		//
		if (!WriteFile(hFile, pInfo, sizeof(EXTENDED_FINDER_INFO), &dwBytesWritten, nullptr))
		{
			//
			//Failure, get the error code and get out of here.
			//
			dwError = GetLastError();
			DBGWRITE(DbgLevelInfo, L"[fpinfo:AFPSetFinderInfoEx]WriteFile() failed! (%d)\n", dwError);

			dwError = AFPMapWinErrorToAFP(dwError, static_cast<DWORD>(afpMiscErr));
		}

		ASSERT(dwBytesWritten == sizeof(EXTENDED_FINDER_INFO));
		DBGWRITE(DbgLevelAll, L"[fpinfo:AFPSetFinderInfoEx]FinderInfo updated successfully for %s.\n", szPath);

		CloseHandle(hFile);
	}

	return dwError;
}


/*
 * AFPUpdateEntryCharMap()
 *
 * Description:
 *		Update the charmap for a particular file and write it out
 *		to the info stream.
 *
 * Returns: AFPERROR
 */

AFPERROR AFPUpdateEntryCharMap(
	IN LPAFPINDEX pEntry,
	   IN AFPCHARMAPARRAY pCharmap
)
{
	EXTENDED_FINDER_INFO infoEx;

	auto afpError = AFPGetFinderInfoEx(pEntry, &infoEx);

	if (AFP_SUCCESS(afpError))
	{
		RtlCopyMemory(&infoEx.charMap, pCharmap, sizeof(AFPCHARMAPARRAY));

		afpError = AFPSetFinderInfoEx(pEntry, &infoEx);
	}

	return afpError;
}


/*
 * AFPAddCharMapToIndexEntry()
 *
 * Description:
 *		Adds a charmap to an index entry, note that if a charmap
 *		alrady exists, this will overwrite it.
 *
 * Returns: AFPERROR
 */

AFPERROR AFPAddCharMapToIndexEntry(
	IN LPAFPINDEX pEntry,
	   IN AFPCHARMAPARRAY pCharmap
)
{
	if (pCharmap[0].wKeyCode != 0)
	{
		pEntry->flags |= FLAG_INDEX_HASCHARMAP;
	}
	else
	{
		pEntry->flags &= ~FLAG_INDEX_HASCHARMAP;
	}

	return AFPUpdateEntryCharMap(pEntry, pCharmap);
}


/*
 * AFPGetCharMapForIndexEntry()
 *
 * Description:
 *		Looks up a charmap for an index entry and returns it
 *		it to the caller.
 *
 * Returns: AFPERROR
 */

AFPERROR AFPGetCharMapForIndexEntry(
	IN LPAFPINDEX pEntry,
	   OUT AFPCHARMAPARRAY pCharmap
)
{
	EXTENDED_FINDER_INFO infoEx;
	AFPERROR afpError = AFP_OK;

	if (pEntry->flags & FLAG_INDEX_HASCHARMAP)
	{
		memset(&infoEx, 0, sizeof(EXTENDED_FINDER_INFO));

		afpError = AFPGetFinderInfoEx(pEntry, &infoEx);

		if (AFP_SUCCESS(afpError))
		{
			memcpy(pCharmap, &infoEx.charMap, sizeof(AFPCHARMAPARRAY));
		}
	}

	return afpError;
}


/*
 * AFPCheckForCharMap()
 *
 * Description:
 *		Checks to see if a charmap is currently utilized for this
 *		this index entry. If so, it sets the entry's flag to signal
 *		that an entry exists.
 *
 * Returns: AFPERROR
 */

BOOL AFPCheckForCharMap(
	IN LPAFPINDEX pEntry
)
{
	EXTENDED_FINDER_INFO infoEx;

	if (AFP_SUCCESS(AFPGetFinderInfoEx(pEntry, &infoEx)))
	{
		if (infoEx.charMap[0].wKeyCode != 0)
		{
			pEntry->flags |= FLAG_INDEX_HASCHARMAP;
			return true;
		}
	}

	return false;
}
