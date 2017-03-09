/*
 *	LAFPSession.cpp
 *
 *	Copyright © Michael J. Conrad. All Rights Reserved.
 *
 *	This class is used to track an AFP session.
 */

#include "StdAfx.h"
#include "LAFPSession.h"

extern LPSESSION gSessionsList;
extern CRITICAL_SECTION gcsSessionList;


/*
 * LAFPSession()
 *
 * Description:
 *		Initializer for the LAFPSession class. 
 *
 * Returns: None
 */

LAFPSession::LAFPSession(LPSESSION dsiSession)
	: mNumOpenFiles(0)
	  , mNumOpenVolumes(0)
	  , mNumOpenDesks(0)
	  , mToken(GetTickCount())
	  , mAFPID(nullptr)
	  , mAFPIDSize(0)
	  , mAFPTimeStamp(0)
	  , mDSISession(dsiSession)
{
}


/*
 * ~LAFPSession()
 *
 * Description:
 *		Destructor for the LAFPSession class. 
 *
 * Returns: None
 */

LAFPSession::~LAFPSession()
{
	LOpenFile* openFile = nullptr;
	PAFP_VOLUME_INFO pInfo = nullptr;
	OPEN_DESKTOP* desk = nullptr;

	mOpenFiles.Rewind();

	while (mOpenFiles.Next(&openFile))
	{
		SessionCloseFile(openFile->GetFileRefNum());
	}

	mOpenVolumes.Rewind();

	while (mOpenVolumes.Next(&pInfo))
	{
		DBGWRITE(DbgLevelAll, L"[LAFPSession:~LAFPSession]Closing volume %d\n", pInfo->wVolID);
		SessionCloseVolume(pInfo->wVolID);
	}

	mOpenDesks.Rewind();

	while (mOpenDesks.Next(&desk))
	{
		SessionCloseDesk(desk->dtRefNum);
	}

	if (mAFPID != nullptr)
	{
		delete [] mAFPID;
	}
}


/*
 * SessionAssociateOpenFile()
 *
 * Description:
 *		A file has been opened, keep track of it in this session. 
 *
 * Returns: None
 */

void LAFPSession::SessionAssociateOpenFile(LOpenFile* inOpenFileObject)
{
	ASSERT(inOpenFileObject != nullptr);

	DBGWRITE(DbgLevelAll, L"[LAFPSession:SessionAssociateOpenFile]opening fileobject %x\n", inOpenFileObject);

	mOpenFiles.AddItem(&inOpenFileObject);
	mNumOpenFiles++;
}


/*
 * SessionCloseFile()
 *
 * Description:
 *		Close and open file. 
 *
 * Returns: None
 */

AFPERROR LAFPSession::SessionCloseFile(int16 inFileRefNum)
{
	return SessionCloseFile(FindOpenFileObject(inFileRefNum));
}


/*
 * SessionCloseFile()
 *
 * Description:
 *		Close and open file. 
 *
 * Returns: None
 */

AFPERROR LAFPSession::SessionCloseFile(LOpenFile* inFileObject)
{
	DBGWRITE(DbgLevelAll, L"[LAFPSession:SessionCloseFile]closing fileref %d\n", inFileObject->GetFileRefNum());

	if (inFileObject != nullptr)
	{
		mOpenFiles.RemoveItem(&inFileObject);
		delete inFileObject;

		mNumOpenFiles--;

		DBGWRITE(DbgLevelAll, L"[LAFPSession:SessionCloseFile]Closed file\n");
	}
	else
	{
		ASSERT(0);
		return afpParmErr;
	}

	return AFP_OK;
}


/*
 * FindOpenFileObject()
 *
 * Description:
 *		Find an open file object within the global list tracking all
 *		open files. 
 *
 * Returns: The found file object or nullptr.
 */

LOpenFile* LAFPSession::FindOpenFileObject(int16 inFileRef)
{
	LOpenFile* findObject = nullptr;
	LOpenFile* resultObject = nullptr;

	mOpenFiles.Rewind();

	while (mOpenFiles.Next(&findObject))
	{
		if (findObject->GetFileRefNum() == inFileRef)
		{
			resultObject = findObject;
			break;
		}
	}

	if (resultObject == nullptr)
	{
		DBGWRITE(DbgLevelWarning, L"[LAFPSession:FindOpenFileObject]****Can't find open file!\n");
	}

	return resultObject;
}


/*
 * NewOpenFileObject()
 *
 * Description:
 *		Create a new open file object. 
 *
 * Returns: The new file object or nullptr.
 */

LOpenFile* LAFPSession::NewOpenFileObject(
	LAFPSession* inSession,
	uint16 inVolID,
	uint32 inDirID,
	int16 inAccessMode,
	int8 inFork,
	LPAFPINDEX inEntry,
	WCHAR* inWinPath
)
{
	auto newFile = new LOpenFile(
		inVolID,
		inDirID,
		inAccessMode,
		inFork,
		inEntry,
		inWinPath
	);

	if (newFile == nullptr)
	{
		DBGWRITE(DbgLevelWarning, L"[LAFPSession:NewOpenFileObject]****Failed to allocate new object!\n");
		return (nullptr);
	}

	inSession->SessionAssociateOpenFile(newFile);

	return newFile;
}


/*
 * SessionOpenVolume()
 *
 * Description:
 *		Open a volume for access by this afp session. 
 *
 * Returns: AFP error code.
 */

AFPERROR LAFPSession::SessionOpenVolume(uint16 inVolumeID)
{
	PAFP_VOLUME_INFO pInfo;

	if (!AFPGetVolumeInfoBlock(nullptr, inVolumeID, &pInfo))
	{
		//
		//We failed to find a block corresponding to the
		//supplied volume ID.
		//

		DBGWRITE(DbgLevelInfo, L"[LAFPSession::SessionOpenVolume]Bad volume ID\n");
		return (afpParmErr);
	}

	mOpenVolumes.AddItem(&pInfo);
	mNumOpenVolumes++;

	//
	//Incrementing the ref count tells our routines that a
	//user has the volume open and is accessing it's contents.
	//
	pInfo->dwRefCount++;

	DBGWRITE(
		DbgLevelAll,
		(L"[LAFPSession::SessionOpenVolume]Volume %s opened\n",
			pInfo->szShareName
		));

	return AFP_OK;
}


/*
 * SessionCloseVolume()
 *
 * Description:
 *		Closes a volume from access. 
 *
 * Returns: AFP error code.
 */

AFPERROR LAFPSession::SessionCloseVolume(uint16 inVolumeID)
{
	PAFP_VOLUME_INFO pInfo;

	pInfo = SessionFindOpenVolumeBlock(inVolumeID);

	if (pInfo != nullptr)
	{
		mOpenVolumes.RemoveItem(&pInfo);

		mNumOpenVolumes--;
		pInfo->dwRefCount--;

		DBGWRITE(
			DbgLevelAll,
			(L"[LAFPSession::SessionCloseVolume]Volume %s was closed\n",
				pInfo->szShareName
			));

		return AFP_OK;
	}

	return afpParmErr;
}


/*
 * SessionFindOpenVolumeBlock()
 *
 * Description:
 *		If this session has the volume open, it will return a pointer
 *		to the volume info block. 
 *
 * Returns: Ptr to the block or nullptr
 */

PAFP_VOLUME_INFO LAFPSession::SessionFindOpenVolumeBlock(uint16 inVolumeID)
{
	PAFP_VOLUME_INFO pInfo = nullptr;

	mOpenVolumes.Rewind();

	while (mOpenVolumes.Next(&pInfo))
	{
		if (pInfo->wVolID == inVolumeID)
		{
			return pInfo;
		}
	}

	return nullptr;
}


/*
 * SessionOpenDesk()
 *
 * Description:
 *		Open a desktop database for access.
 *
 * Returns: AFP error code.
 */

AFPERROR LAFPSession::SessionOpenDesk(uint16 inVolumeID, uint16* outDTRefNum)
{
	uint16 newRefNum = 0;
	AFPERROR afpError = AFP_OK;

	auto newOpenDesk = new OPEN_DESKTOP;

	if (newOpenDesk != nullptr)
	{
		afpError = AFPOpenDeskTopDB(inVolumeID, newOpenDesk, &newRefNum);

		if (AFP_SUCCESS(afpError))
		{
			*outDTRefNum = newRefNum;
			mNumOpenDesks++;

			mOpenDesks.AddItem(&newOpenDesk);
		}
		else
		{
			delete newOpenDesk;
		}
	}

	return afpError;
}


/*
 * SessionCloseDesk()
 *
 * Description:
 *		Close a desktop database from access.
 *
 * Returns: AFP error code.
 */

AFPERROR LAFPSession::SessionCloseDesk(uint16 inDeskRefNum)
{
	AFPERROR afpError;

	auto desk = SessionFindDeskFile(inDeskRefNum);

	if (desk != nullptr)
	{
		afpError = AFPCloseDeskTopDB(desk);
		mOpenDesks.RemoveItem(&desk);

		mNumOpenDesks--;

		delete desk;
	}
	else
	{
		afpError = afpParmErr;
	}

	return afpError;
}


/*
 * SessionFindDeskFile()
 *
 * Description:
 *		Returns a pointer to the OPEN_DESKTOP associated with the
 *		dtRefNum passed.
 *
 * Returns: AFP error code.
 */

OPEN_DESKTOP* LAFPSession::SessionFindDeskFile(uint16 inDeskRefNum)
{
	OPEN_DESKTOP* desk = nullptr;

	mOpenDesks.Rewind();

	while (mOpenDesks.Next(&desk))
	{
		if (desk->dtRefNum == inDeskRefNum)
		{
			return desk;
		}
	}

	return nullptr;
}


/*
 * SetAFPID()
 *
 * Description:
 *		Sets the AFP ID for this session.
 *
 * Returns: none
 */

void LAFPSession::SetAFPID(int8* inID, uint32 inIDSize)
{
	if (mAFPID != nullptr)
	{
		delete mAFPID;
	}

	mAFPID = inID;
	mAFPIDSize = inIDSize;
}


/*
 * FindSessionWithID()
 *
 * Description:
 *		Finds a session with the specified afp ID as set by the client.
 *
 * Returns: none
 */

LAFPSession* LAFPSession::FindSessionWithID(int8* afpID, uint32 inIDSize)
{
	auto pSession = gSessionsList;

	EnterCriticalSection(&gcsSessionList);

	while (pSession)
	{
		auto afpObject = static_cast<LAFPSession*>(pSession->afpSessionObject);

		if (afpObject != nullptr)
		{
			auto afpObjectID = afpObject->GetAFPID();

			if (afpObjectID != nullptr &&
				inIDSize == afpObject->GetAFPIDSize() &&
				!memcmp(afpID, afpObjectID, inIDSize))
			{
				LeaveCriticalSection(&gcsSessionList);
				return afpObject;
			}
		}

		pSession = static_cast<LPSESSION>(pSession->next);
	}

	LeaveCriticalSection(&gcsSessionList);

	return (nullptr);
}


/*
 * FindSessionByToken()
 *
 * Description:
 *		Finds a session with the specified afp token.
 *
 * Returns: none
 */

LAFPSession* LAFPSession::FindSessionByToken(int32 inToken)
{
	auto pSession = gSessionsList;

	EnterCriticalSection(&gcsSessionList);

	while (pSession)
	{
		auto afpSession = static_cast<LAFPSession*>(pSession->afpSessionObject);

		if (afpSession != nullptr)
		{
			if (afpSession->GetToken() == static_cast<uint32>(inToken))
			{
				LeaveCriticalSection(&gcsSessionList);
				return afpSession;
			}
		}

		pSession = static_cast<LPSESSION>(pSession->next);
	}

	LeaveCriticalSection(&gcsSessionList);

	return (nullptr);
}
