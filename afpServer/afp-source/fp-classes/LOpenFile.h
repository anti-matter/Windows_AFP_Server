/*
 *	LOpenFile.h
 *
 *	Copyright © Michael J. Conrad. All Rights Reserved.
 */

#pragma once

#include "volumes.h"
#include "afphelp.h"

//
//Struct to keep info about a particular stream. All we care
//about from the afp perspective it the size and the name.
//
typedef struct
{
	LARGE_INTEGER streamSize;
	WCHAR wszStreamName[MAX_PATH];
} AFP_ATTR_INFO, *LPAFP_ATTR_INFO;

class LOpenFile
{
public:
	LOpenFile(
		int16 inVolID,
		int32 inDirID,
		int16 inAccessMode,
		uint8 inFork,
		LPAFPINDEX inEntry,
		WCHAR* inWinPath
	);

	~LOpenFile();

	AFPERROR OpenFileFork(PWCHAR streamName = nullptr, DWORD afpCreationDisposition = OPEN_EXISTING);
	AFPERROR CloseFileFork();
	AFPERROR ReadFork(
		AfpBuffer& afpReply,
		int64 afpOffset,
		int64 afpReqCount,
		uint32* afpActualRead
	);
	AFPERROR WriteFork(
		AfpBuffer& afpRequest,
		int64 afpOffset,
		int64 afpReqCount,
		BOOL afpFromStart,
		int64* afpLastByteWritten
	) const;

	AFPERROR FileSeek(
		int64 inDistance,
		uint32 inMethod
	) const;

	bool isForkOpen() const { return mFileHandle != INVALID_HANDLE_VALUE; }

	uint64 GetStreamLength(PWCHAR streamName = nullptr);
	AFPERROR GetStreamList(std::vector<AFP_ATTR_INFO>& streams);

	HANDLE GetFileHandle() const { return mFileHandle; }
	int16 GetFileRefNum() const { return mFileRefNum; }
	int8 GetFork() const { return mFork; }
	int16 GetVolID() const { return mVolumeID; }
	int32 GetDirID() const { return mDirID; }
	WCHAR* GetWinPath() { return mWindowsPath; }
	LPAFPINDEX GetEntry() const { return mEntry; }

private:

	HANDLE mFileHandle;

	int16 mFileRefNum;
	int16 mVolumeID;
	int32 mDirID;
	int16 mAccessMode;
	uint8 mFork;

	LPAFPINDEX mEntry;

	WCHAR mWindowsPath[MAX_PATH];
};
