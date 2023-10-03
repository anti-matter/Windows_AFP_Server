/*
 *	AFPSession.h
 *	Created 07/12/2003
 *
 *	Copyright © 2003, Michael J. Conrad. All Rights Reserved.
 */

#pragma once

#include "volumes.h"
#include "afphelp.h"
#include "desktop.h"

class AFPOpenFile;

class AFPSession
{
public:
    explicit AFPSession(LPSESSION dsiSession);
	~AFPSession();

	void SessionAssociateAFPOpenFile(AFPOpenFile* inAFPOpenFileObject);
	AFPERROR SessionCloseFile(int16 inFileRefNum);
	AFPOpenFile* FindAFPOpenFileObject(int16 inFileRef);
	static AFPOpenFile* NewAFPOpenFileObject(
		AFPSession* inSession,
		uint16 inVolID,
		uint32 inDirID,
		int16 inAccessMode,
		int8 inFork,
		LPAFPINDEX inEntry,
		WCHAR* inWinPath
	);

	AFPERROR SessionOpenVolume(uint16 inVolumeID);
	AFPERROR SessionCloseVolume(uint16 inVolumeID);
	AFP_VOLUME_INFO* SessionFindOpenVolumeBlock(uint16 inVolumeID);
	BOOL SessionHasVolumeOpen(uint16 inVolumeID) { return SessionFindOpenVolumeBlock(inVolumeID) != nullptr; }

	AFPERROR SessionOpenDesk(uint16 inVolumeID, uint16* outDTRefNum);
	AFPERROR SessionCloseDesk(uint16 inDeskRefNum);
	OPEN_DESKTOP* SessionFindDeskFile(uint16 inDeskRefNum);

	uint32 GetToken() const { return mToken; }

	int16 GetNumFilesOpen() const { return static_cast<int16>(mAFPOpenFiles.size()); }
	int16 GetNumVolsOpen() const { return static_cast<int16>(mOpenVolumes.size()); }
	int16 GetNumDesksOpen() const { return static_cast<int16>(mOpenDesks.size()); }

	void SetAFPID(int8* inID, uint32 inIDSize);
	int8* GetAFPID() const { return mAFPID; }
	uint32 GetAFPIDSize() const { return mAFPIDSize; }
	void SetAFPTimeStamp(uint32 inTime) { mAFPTimeStamp = inTime; }
	uint32 GetAFPTimeStamp() const { return mAFPTimeStamp; }

	LPSESSION GetDSISession() const { return mDSISession; }

	static AFPSession* FindSessionWithID(int8* afpID, uint32 inIDSize);
	static AFPSession* FindSessionByToken(int32 inToken);

private:

    CRITICAL_SECTION mMutex;

    std::vector<std::unique_ptr<AFPOpenFile>> mAFPOpenFiles;
	std::vector<AFP_VOLUME_INFO*> mOpenVolumes;
	std::vector<std::unique_ptr<OPEN_DESKTOP>> mOpenDesks;

	uint32 mToken;
	int8* mAFPID;
	uint32 mAFPIDSize;
	uint32 mAFPTimeStamp;

	LPSESSION mDSISession;
};
