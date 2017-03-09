/*
 *	LAFPSession.h
 *	Created 07/12/2003
 *
 *	Copyright © 2003, Michael J. Conrad. All Rights Reserved.
 */

#pragma once

#include "LArray.h"
#include "LOpenFile.h"
#include "volumes.h"
#include "afphelp.h"
#include "desktop.h"

class LAFPSession
{
public:

	explicit LAFPSession(LPSESSION dsiSession);
	~LAFPSession();

	void SessionAssociateOpenFile(LOpenFile* inOpenFileObject);
	AFPERROR SessionCloseFile(int16 inFileRefNum);
	AFPERROR SessionCloseFile(LOpenFile* inFileObject);
	LOpenFile* FindOpenFileObject(int16 inFileRef);
	static LOpenFile* NewOpenFileObject(
		LAFPSession* inSession,
		uint16 inVolID,
		uint32 inDirID,
		int16 inAccessMode,
		int8 inFork,
		LPAFPINDEX inEntry,
		WCHAR* inWinPath
	);

	AFPERROR SessionOpenVolume(uint16 inVolumeID);
	AFPERROR SessionCloseVolume(uint16 inVolumeID);
	PAFP_VOLUME_INFO SessionFindOpenVolumeBlock(uint16 inVolumeID);
	BOOL SessionHasVolumeOpen(uint16 inVolumeID) { return (SessionFindOpenVolumeBlock(inVolumeID) != nullptr); }

	AFPERROR SessionOpenDesk(uint16 inVolumeID, uint16* outDTRefNum);
	AFPERROR SessionCloseDesk(uint16 inDeskRefNum);
	OPEN_DESKTOP* SessionFindDeskFile(uint16 inDeskRefNum);

	uint32 GetToken() const { return mToken; }


	int16 GetNumFilesOpen() const { return mNumOpenFiles; }
	int16 GetNumVolsOpen() const { return mNumOpenVolumes; }
	int16 GetNumDesksOpen() const { return mNumOpenDesks; }

	void SetAFPID(int8* inID, uint32 inIDSize);
	int8* GetAFPID() const { return mAFPID; }
	uint32 GetAFPIDSize() const { return mAFPIDSize; }
	void SetAFPTimeStamp(uint32 inTime) { mAFPTimeStamp = inTime; }
	uint32 GetAFPTimeStamp() const { return mAFPTimeStamp; }

	LPSESSION GetDSISession() const { return mDSISession; }

	static LAFPSession* FindSessionWithID(int8* afpID, uint32 inIDSize);
	static LAFPSession* FindSessionByToken(int32 inToken);

private:

	int16 mNumOpenFiles;
	int16 mNumOpenVolumes;
	int16 mNumOpenDesks;

	LArray mOpenFiles;
	LArray mOpenVolumes;
	LArray mOpenDesks;

	uint32 mToken;
	int8* mAFPID;
	uint32 mAFPIDSize;
	uint32 mAFPTimeStamp;

	LPSESSION mDSISession;
};
