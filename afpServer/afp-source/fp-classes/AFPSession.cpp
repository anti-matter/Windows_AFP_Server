/*
 *	AFPSession.cpp
 *
 *	Copyright © Michael J. Conrad. All Rights Reserved.
 *
 *	This class is used to track an AFP session.
 */

#include "StdAfx.h"
#include "AutoLock.h"
#include "AFPSession.h"
#include "LOpenFile.h"
#include <algorithm>

extern LPSESSION gSessionsList;
extern CRITICAL_SECTION gcsSessionList;


/*
 * AFPSession()
 *
 * Description:
 *		Initializer for the AFPSession class. 
 *
 * Returns: None
 */

AFPSession::AFPSession(LPSESSION dsiSession)
    : mToken(GetTickCount())
      , mAFPID(nullptr)
      , mAFPIDSize(0)
      , mAFPTimeStamp(0)
      , mDSISession(dsiSession)
{
    InitializeCriticalSection(&mMutex);
}


/*
 * ~AFPSession()
 *
 * Description:
 *		Destructor for the AFPSession class. 
 *
 * Returns: None
 */

AFPSession::~AFPSession()
{
    EnterCriticalSection(&mMutex);

    std::for_each(mOpenDesks.begin(), mOpenDesks.end(), [=](const std::unique_ptr<OPEN_DESKTOP>& dt)
    {
        SessionCloseDesk(dt.get()->dtRefNum);
    });

    std::for_each(mOpenFiles.begin(), mOpenFiles.end(), [=](const std::unique_ptr<LOpenFile>& of)
    {
        SessionCloseFile(of.get()->GetFileRefNum());
    });

    std::for_each(mOpenVolumes.begin(), mOpenVolumes.end(), [=](const AFP_VOLUME_INFO* ov)
    {
        SessionCloseFile(ov->wVolID);
    });

    LeaveCriticalSection(&mMutex);

    if (mAFPID != nullptr)
    {
        delete [] mAFPID;
    }

    DeleteCriticalSection(&mMutex);
}


/*
 * SessionAssociateOpenFile()
 *
 * Description:
 *		A file has been opened, keep track of it in this session. 
 *
 * Returns: None
 */

void AFPSession::SessionAssociateOpenFile(LOpenFile* inOpenFileObject)
{
    AutoLock lock(&mMutex);

    ASSERT(inOpenFileObject != nullptr);
    DBGWRITE(DbgLevelAll, L"[AFPSession:SessionAssociateOpenFile]opening fileobject %x\n", inOpenFileObject);

    std::unique_ptr<LOpenFile> openFileObject(inOpenFileObject);
    mOpenFiles.push_back(std::move(openFileObject));
}


/*
 * SessionCloseFile()
 *
 * Description:
 *		Close and open file. 
 *
 * Returns: None
 */

AFPERROR AFPSession::SessionCloseFile(int16 inFileRefNum)
{
    AutoLock lock(&mMutex);

    std::vector<std::unique_ptr<LOpenFile>>::iterator it =
        std::find_if(mOpenFiles.begin(), mOpenFiles.end(), [=](const std::unique_ptr<LOpenFile>& f)
    {
        return f->GetFileRefNum() == inFileRefNum;
    });

    if (it != mOpenFiles.end())
    {
        mOpenFiles.erase(it);
        return AFP_OK;
    }

    return afpParmErr;
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

LOpenFile* AFPSession::FindOpenFileObject(int16 inFileRef)
{
    AutoLock lock(&mMutex);

    std::vector<std::unique_ptr<LOpenFile>>::iterator it =
        std::find_if(mOpenFiles.begin(), mOpenFiles.end(), [=](const std::unique_ptr<LOpenFile>& f)
    {
        return f->GetFileRefNum() == inFileRef;
    });

    if (it != mOpenFiles.end())
    {
        return it->get();
    }

    DBGWRITE(DbgLevelWarning, L"[AFPSession:FindOpenFileObject]ERROR: Can't find open file!\n");

    return nullptr;
}


/*
 * NewOpenFileObject()
 *
 * Description:
 *		Create a new open file object. 
 *
 * Returns: The new file object or nullptr.
 */

LOpenFile* AFPSession::NewOpenFileObject(
    AFPSession* inSession,
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
        DBGWRITE(DbgLevelWarning, L"[AFPSession:NewOpenFileObject]****Failed to allocate new object!\n");
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

AFPERROR AFPSession::SessionOpenVolume(uint16 volumeID)
{
    AutoLock lock(&mMutex);
    AFP_VOLUME_INFO* volumeInfo;

    if (!AFPGetVolumeInfoBlock(nullptr, volumeID, &volumeInfo))
    {
        //
        //We failed to find a block corresponding to the
        //supplied volume ID.
        //

        DBGWRITE(DbgLevelInfo, L"[AFPSession::SessionOpenVolume]Bad volume ID\n");
        return afpParmErr;
    }

    mOpenVolumes.push_back(volumeInfo);

    //
    //Incrementing the ref count tells our routines that a
    //user has the volume open and is accessing it's contents.
    //
    volumeInfo->dwRefCount++;

    DBGWRITE(
        DbgLevelAll,
        (L"[AFPSession::SessionOpenVolume]Volume %s opened\n",
            volumeInfo->szShareName
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

AFPERROR AFPSession::SessionCloseVolume(uint16 volumeID)
{
    AutoLock lock(&mMutex);

    std::vector<AFP_VOLUME_INFO*>::iterator it =
        std::find_if(mOpenVolumes.begin(), mOpenVolumes.end(), [=](AFP_VOLUME_INFO* v)
        {
            auto result = v->wVolID == volumeID;
            if (result)
            {
                v->dwRefCount--;
            }

            return result;
        });

    if (it != mOpenVolumes.end())
    {
        DBGWRITE(DbgLevelAll, L"[AFPSession::SessionCloseVolume]Volume %s was closed\n", (*it)->szShareName);

        mOpenVolumes.erase(it);
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

AFP_VOLUME_INFO* AFPSession::SessionFindOpenVolumeBlock(uint16 volumeID)
{
    AutoLock lock(&mMutex);

    std::vector<AFP_VOLUME_INFO*>::iterator it =
        std::find_if(mOpenVolumes.begin(), mOpenVolumes.end(), [=](const AFP_VOLUME_INFO* v)
    {
        return v->wVolID == volumeID;
    });

    if (it != mOpenVolumes.end())
    {
        return *it;
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

AFPERROR AFPSession::SessionOpenDesk(uint16 inVolumeID, uint16* outDTRefNum)
{
    auto newOpenDesk = std::make_unique<OPEN_DESKTOP>();
    auto afpError = AFPOpenDeskTopDB(inVolumeID, newOpenDesk.get());

    if (AFP_SUCCESS(afpError))
    {
        AutoLock lock(&mMutex);

        *outDTRefNum = newOpenDesk->dtRefNum;
        mOpenDesks.push_back(std::move(newOpenDesk));
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

AFPERROR AFPSession::SessionCloseDesk(uint16 inDeskRefNum)
{
    AutoLock lock(&mMutex);

    std::vector<std::unique_ptr<OPEN_DESKTOP>>::iterator it =
        std::find_if(mOpenDesks.begin(), mOpenDesks.end(), [=](const std::unique_ptr<OPEN_DESKTOP>& odt)
        {
            return odt->dtRefNum == inDeskRefNum;
        });

    if (it != mOpenDesks.end())
    {
        AFPCloseDeskTopDB(it->get());

        mOpenDesks.erase(it);
        return AFP_OK;
    }

    return afpParmErr;
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

OPEN_DESKTOP* AFPSession::SessionFindDeskFile(uint16 inDeskRefNum)
{
    AutoLock lock(&mMutex);

    std::vector<std::unique_ptr<OPEN_DESKTOP>>::iterator it =
        std::find_if(mOpenDesks.begin(), mOpenDesks.end(), [=](const std::unique_ptr<OPEN_DESKTOP>& odt)
        {
            return odt->dtRefNum == inDeskRefNum;
        });

    if (it != mOpenDesks.end())
    {
        return it->get();
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

void AFPSession::SetAFPID(int8* inID, uint32 inIDSize)
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

AFPSession* AFPSession::FindSessionWithID(int8* afpID, uint32 inIDSize)
{
    auto pSession = gSessionsList;

    AutoLock lock(&gcsSessionList);

    while (pSession)
    {
        auto afpObject = static_cast<AFPSession*>(pSession->afpSessionObject);

        if (afpObject != nullptr)
        {
            auto afpObjectID = afpObject->GetAFPID();

            if (afpObjectID != nullptr &&
                inIDSize == afpObject->GetAFPIDSize() &&
                !memcmp(afpID, afpObjectID, inIDSize))
            {
                return afpObject;
            }
        }

        pSession = static_cast<LPSESSION>(pSession->next);
    }

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

AFPSession* AFPSession::FindSessionByToken(int32 inToken)
{
    auto pSession = gSessionsList;

    AutoLock lock(&gcsSessionList);

    while (pSession)
    {
        auto afpSession = static_cast<AFPSession*>(pSession->afpSessionObject);

        if (afpSession != nullptr)
        {
            if (afpSession->GetToken() == static_cast<uint32>(inToken))
            {
                return afpSession;
            }
        }

        pSession = static_cast<LPSESSION>(pSession->next);
    }

    return (nullptr);
}
