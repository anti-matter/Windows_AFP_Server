/*
 *	LOpenFile.cpp
 *
 *	Copyright © Michael J. Conrad. All Rights Reserved.
 *
 *	Maintains data about an open AFP file.
 */

#include "StdAfx.h"
#include <strsafe.h>
#include "LOpenFile.h"
#include "afpxattr.h"

/*
 * LOpenFile()
 *
 * Description:
 *		Initializer for the LOpenFile class. 
 *
 * Returns: None
 */

LOpenFile::LOpenFile(
	int16 inVolID,
	int32 inDirID,
	int16 inAccessMode,
	uint8 inFork,
	LPAFPINDEX inEntry,
	WCHAR* inWinPath
)
{
	mFileHandle = INVALID_HANDLE_VALUE;
	mVolumeID = inVolID;
	mDirID = inDirID;
	mAccessMode = inAccessMode;
	mFork = inFork;
	mEntry = inEntry;
	mFileRefNum = 0;

	StringCbCopy(mWindowsPath, sizeof(mWindowsPath), inWinPath);
}


/*
 * ~LOpenFile()
 *
 * Description:
 *		Destructor for the LOpenFile class. The destructor closes the
 *		opened file fork.
 *
 * Returns: None
 */

LOpenFile::~LOpenFile()
{
	if (isForkOpen())
	{
		CloseFileFork();
	}
}


/*
 * OpenFileFork()
 *
 * Description:
 *		Open the file fork for access.
 *
 *		streamName can optionally contain the name of a particular stream
 *		in the file to open.
 *
 * Returns: afpError code
 */

AFPERROR LOpenFile::OpenFileFork(PWCHAR streamName, DWORD afpCreationDisposition)
{
	WCHAR szFilePath[MAX_PATH];
	uint32 desiredAccess = 0;
	int16 afpFlags = FLAG_INDEX_SETITEM;
	DWORD dwFlagsAndAttr = FILE_ATTRIBUTE_NORMAL;

	if (mAccessMode & kReadMode) desiredAccess |= GENERIC_READ;
	if (mAccessMode & kWriteMode) desiredAccess |= GENERIC_WRITE;

	StringCbCopy(szFilePath, sizeof(szFilePath), mWindowsPath);

	switch (mFork)
	{
	case kDataFork:
		afpFlags |= FLAG_INDEX_DATAOPEN;
		break;

	case kRsrcFork:
		StringCbCat(szFilePath, sizeof(szFilePath), AFP_RESOURCE_FORK_NAME);
		afpFlags |= FLAG_INDEX_RESOPEN;
		break;

	case kAttrFork:
		DBGWRITE(DbgLevelInfo, L"[LOpenFile:OpenFileFork]Opening stream %s\n", streamName);
		ASSERT(streamName != nullptr);

		if (streamName != nullptr)
		{
			StringCbCat(szFilePath, sizeof(szFilePath), TEXT(":"));
			StringCbCat(szFilePath, sizeof(szFilePath), streamName);

			afpFlags |= FLAG_INDEX_ATTROPEN;
			dwFlagsAndAttr = FILE_FLAG_BACKUP_SEMANTICS;
		}
		break;

	default:
		ASSERT(0);
		break;
	}

	DBGWRITE(DbgLevelAll, L"[LOpenFile:OpenFileFork]Opening file %s\n", szFilePath);

	auto hFile = CreateFile(
		szFilePath,
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr,
		afpCreationDisposition,
		dwFlagsAndAttr,
		nullptr
	);

	if (hFile == INVALID_HANDLE_VALUE)
	{
		auto dwError = GetLastError();
		DBGWRITE(DbgLevelInfo, L"[LOpenFile:OpenFileFork]CreateFile() failed! (%d) (%s)\n", dwError, szFilePath);

		return AFPMapWinErrorToAFP(dwError, static_cast<DWORD>(afpParmErr));
	}

	//
	//Get the next available file ref num.
	//
	static int16 nextFileRefNum = 0;
	mFileRefNum = nextFileRefNum++;
	mFileHandle = hFile;

	//
	//Now set the appropriate flags to indicate the state of the forks.
	//
	AFPIndexUpdateEntryFlags(mEntry, afpFlags);

	return AFP_OK;
}


/*
 * CloseFileFork()
 *
 * Description:
 *		Close the file fork from access.
 *
 * Returns: afpError code
 */

AFPERROR LOpenFile::CloseFileFork()
{
	int16 afpFlags = 0;

	//
	//First we need to update the flags in the afpindex as to the state of
	//of this file fork. Note that AFPIndexUpdateEntryFlags() takes care of
	//actually clearing the bits in the index entry.
	//
	switch (mFork)
	{
	case kDataFork:
		afpFlags |= FLAG_INDEX_DATAOPEN;
		break;

	case kRsrcFork:
		afpFlags |= FLAG_INDEX_RESOPEN;
		break;

	case kAttrFork:
		afpFlags |= FLAG_INDEX_ATTROPEN;
		break;

	default:
		ASSERT(0);
		break;
	}

	DBGWRITE(DbgLevelAll, L"[LOpenFile:CloseFileFork]Closing file %s, fork = %d\n", mWindowsPath, mFork);

	//
	//Now set the appropriate flags to indicate the state of the forks.
	//
	AFPIndexUpdateEntryFlags(mEntry, afpFlags);

	//
	//Now, perform the actual close operation on the file.
	//
	if (mFileHandle != INVALID_HANDLE_VALUE)
	{
		CloseHandle(mFileHandle);
		mFileHandle = INVALID_HANDLE_VALUE;
	}
	else
	{
		DBGWRITE(DbgLevelAll, L"[LOpenFile:CloseFileFork]Invalid file handle for %s\n", mWindowsPath);
	}

	return AFP_OK;
}


/*
 * ReadFork()
 *
 * Description:
 *		Read data from an open file fork.
 *
 * Returns: afpError code
 */

AFPERROR LOpenFile::ReadFork(
	AfpBuffer& afpReply,
	int64 afpOffset,
	int64 afpReqCount,
	uint32* afpActualRead
)
{
	OVERLAPPED overlapped;
	LARGE_INTEGER offset;

	//
	//Make sure we have a valid file handle before we do anything.
	//
	if (mFileHandle == INVALID_HANDLE_VALUE)
	{
		ASSERT(0);
		return afpParmErr;
	}

	DBGWRITE(DbgLevelAll, L"[LOpenFile::ReadFork]Reading %lld bytes from file %s from fork %d\n", afpReqCount, mWindowsPath, mFork);

	//
	//We have to make sure our buffer is big enough for
	//the read operation.
	//
	if (afpReply.GetBufferSize() < static_cast<size_t>(DSI_HEADER_SIZE + afpReqCount))
	{
		//
		//Our buffer is too small, adjust the amount to read.
		//
		afpReqCount = afpReply.GetBufferSize() - DSI_HEADER_SIZE;

		DBGWRITE(
			DbgLevelWarning,
			L"[LOpenFile::ReadFork]Buffer too small! (needed=%d, have=%d)\n",
			(DSI_HEADER_SIZE+afpReqCount),
			afpReply.GetBufferSize()
		);
	}

	//
	//Get the 64bit offset into a LI structure.
	//
	offset.QuadPart = afpOffset;

	//
	//Setup our offsets into the file to read.
	//
	overlapped.Offset = offset.LowPart;
	overlapped.OffsetHigh = offset.HighPart;
	overlapped.hEvent = nullptr;

	//
	//Call the Windows API to do all the work for us.
	//
	auto success = ReadFile(
		mFileHandle,
		afpReply.GetCurrentPosPtr(),
		static_cast<uint32>(afpReqCount),
		afpActualRead,
		&overlapped
	);

	AFPERROR afpError = AFP_OK;

	if (!success)
	{
		afpError = AFPMapWinErrorToAFP(GetLastError(), static_cast<DWORD>(afpParmErr));
		DBGWRITE(DbgLevelWarning, L"[LOpenFile::ReadFork]ReadFile() failed! (%d)\n", GetLastError());
	}
	else if (afpReqCount > 0 && *afpActualRead == 0)
	{
		afpError = afpEofError;
	}
	else if (*afpActualRead > 0)
	{
		afpReply.Advance(*afpActualRead);
	}

	return afpError;
}


/*
 * WriteFork()
 *
 * Description:
 *		Write data to an open file fork.
 *
 * Returns: afpError code
 */

AFPERROR LOpenFile::WriteFork(
	AfpBuffer& afpRequest,
	int64 afpOffset,
	int64 afpReqCount,
	BOOL afpFromStart,
	int64* afpLastByteWritten
) const
{
	//
	//Make sure we have a valid file handle before we do anything.
	//
	if (mFileHandle == INVALID_HANDLE_VALUE)
	{
		ASSERT(0);
		return afpParmErr;
	}

	//
	//Get the ptr to the actual block of data we want to write out.
	//
	afpRequest.AdvanceTo(afpRequest.GetDSIDataOffset());

	//DBGWRITE(DbgLevelAll,
	//	(L"[LOpenFile:WriteFork]WRITE:\n\tfile: %s\n\toffset:%d\n\treq count:%d\n\tfromstart:%s\n\tafpDataOffset: %d\n",
	//	mWindowsPath,
	//	afpOffset,
	//	afpReqCount,
	//	(afpFromStart) ? L"YES" : L"NO",
	//	afpRequest.GetDSIDataOffset()
	//	));

	//
	//Set the file position to the desired offset before writing.
	//
	auto afpError = FileSeek(afpOffset, (afpFromStart) ? FILE_BEGIN : FILE_END);

	if (AFP_SUCCESS(afpError))
	{
		uint32 afpActual = 0;

		//
		//Call the Windows API to do all the work for us.
		//
		auto success = WriteFile(
			mFileHandle,
			afpRequest.GetCurrentPosPtr(),
			static_cast<uint32>(afpReqCount),
			&afpActual,
			nullptr
		);

		if (!success)
		{
			afpError = AFPMapWinErrorToAFP(GetLastError(), static_cast<DWORD>(afpParmErr));
			DBGWRITE(DbgLevelInfo, L"[LOpenFile:WriteFork]WriteFile() failed! (%d)\n", GetLastError());
		}
		else
		{
			ASSERT(afpActual >= afpReqCount);

			*afpLastByteWritten = afpOffset + afpActual;

			DBGWRITE(DbgLevelAll,
				L"[LOpenFile:WriteFork]WRITE:\n\tActual written: %d\n\tLast Byte:      %d\n",
				afpActual,
				*afpLastByteWritten
			);
		}
	}

	return afpError;
}


/*
 * FileSeek()
 *
 * Description:
 *		Seek to a place in the file.
 *
 * Returns: afpError code
 */

AFPERROR LOpenFile::FileSeek(
	int64 inDistance,
	uint32 inMethod
) const
{
	LARGE_INTEGER distance;
	AFPERROR afpError = AFP_OK;

	distance.QuadPart = inDistance;
	distance.LowPart = SetFilePointer(mFileHandle, distance.LowPart, &distance.HighPart, inMethod);

	if (distance.LowPart == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR)
	{
		DBGWRITE(DbgLevelInfo, L"[LOpenFile:FileSeek]SetFilePointer() failed %d\n", GetLastError());
		afpError = afpParmErr;
	}

	return afpError;
}


/*
 * GetStreamLength()
 *
 * Description:
 *		Returns the length in bytes of the currently open stream.
 *
 * Returns: size or 0xFFFFFFFF if failed
 */

uint64 LOpenFile::GetStreamLength(PWCHAR streamName)
{
	if (streamName != nullptr)
	{
		//
		//Call the extended attribute helper functions for the dirty work.
		//
		return GetStreamLen(mWindowsPath, streamName);
	}
	else
	{
		//
		//If no stream name, get the length of the main stream/file
		//

		if (mFileHandle != INVALID_HANDLE_VALUE)
		{
			LARGE_INTEGER fileSize;

			if (GetFileSizeEx(mFileHandle, &fileSize))
			{
				return fileSize.QuadPart;
			}
			else
			{
				DBGWRITE(
					DbgLevelInfo,
					L"[LOpenFile:GetStreamLength]GetFileSizeEx() failed (%d)\n",
					GetLastError()
				);
			}
		}
	}

	return BAD_LENGTH_64;
}


/*
 * GetStreamList()
 *
 * Description:
 *		Populate an AFPArray with the names and header info of all the streams
 *		for the file.
 *
 *		NOTE: Make sure the AFPArray is initialized to contain element sizes
 *		equal to sizeof(AFP_ATTR_INFO).
 *
 * Returns: afpError code
 */

AFPERROR LOpenFile::GetStreamList(std::vector<AFP_ATTR_INFO>& streams)
{
	//
	//Call the extended attribute helper functions for the dirty work.
	//
	return GetStreams(mWindowsPath, streams);
}
