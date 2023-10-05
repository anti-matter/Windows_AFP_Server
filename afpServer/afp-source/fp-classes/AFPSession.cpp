/*
 *	AFPSession.cpp
 *
 *	Copyright © Michael J. Conrad. All Rights Reserved.
 *
 *	This class is used to track an AFP session.
 */

#include "StdAfx.h"
#include "auto_lock.h"
#include "AFPSession.h"
#include "AFPOpenFile.h"
#include <algorithm>

extern LPSESSION g_sessions_list;
extern CRITICAL_SECTION g_cs_session_list;


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

    std::for_each(mAFPOpenFiles.begin(), mAFPOpenFiles.end(), [=](const std::unique_ptr<AFPOpenFile>& of)
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
 * SessionAssociateAFPOpenFile()
 *
 * Description:
 *		A file has been opened, keep track of it in this session. 
 *
 * Returns: None
 */

void AFPSession::SessionAssociateAFPOpenFile(AFPOpenFile* inAFPOpenFileObject)
{
    auto_lock lock(&mMutex);

    ASSERT(inAFPOpenFileObject != nullptr);
    DBGWRITE(dbg_level_all, L"[AFPSession:SessionAssociateAFPOpenFile]opening fileobject %x\n", inAFPOpenFileObject);

    std::unique_ptr<AFPOpenFile> AFPOpenFileObject(inAFPOpenFileObject);
    mAFPOpenFiles.push_back(std::move(AFPOpenFileObject));
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
    auto_lock lock(&mMutex);

    std::vector<std::unique_ptr<AFPOpenFile>>::iterator it =
        std::find_if(mAFPOpenFiles.begin(), mAFPOpenFiles.end(), [=](const std::unique_ptr<AFPOpenFile>& f)
    {
        return f->GetFileRefNum() == inFileRefNum;
    });

    if (it != mAFPOpenFiles.end())
    {
        mAFPOpenFiles.erase(it);
        return AFP_OK;
    }

    return afpParmErr;
}


/*
 * FindAFPOpenFileObject()
 *
 * Description:
 *		Find an open file object within the global list tracking all
 *		open files. 
 *
 * Returns: The found file object or nullptr.
 */

AFPOpenFile* AFPSession::FindAFPOpenFileObject(int16 inFileRef)
{
    auto_lock lock(&mMutex);

    std::vector<std::unique_ptr<AFPOpenFile>>::iterator it =
        std::find_if(mAFPOpenFiles.begin(), mAFPOpenFiles.end(), [=](const std::unique_ptr<AFPOpenFile>& f)
    {
        return f->GetFileRefNum() == inFileRef;
    });

    if (it != mAFPOpenFiles.end())
    {
        return it->get();
    }

    DBGWRITE(dbg_level_warning, L"[AFPSession:FindAFPOpenFileObject]ERROR: Can't find open file!\n");

    return nullptr;
}


/*
 * NewAFPOpenFileObject()
 *
 * Description:
 *		Create a new open file object. 
 *
 * Returns: The new file object or nullptr.
 */

AFPOpenFile* AFPSession::NewAFPOpenFileObject(
    AFPSession* inSession,
    uint16 inVolID,
    uint32 inDirID,
    int16 inAccessMode,
    int8 inFork,
    LPAFPINDEX inEntry,
    WCHAR* inWinPath
)
{
    auto newFile = new AFPOpenFile(
        inVolID,
        inDirID,
        inAccessMode,
        inFork,
        inEntry,
        inWinPath
    );

    if (newFile == nullptr)
    {
        DBGWRITE(dbg_level_warning, L"[AFPSession:NewAFPOpenFileObject]****Failed to allocate new object!\n");
        return (nullptr);
    }

    inSession->SessionAssociateAFPOpenFile(newFile);

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
    auto_lock lock(&mMutex);
    AFP_VOLUME_INFO* volumeInfo;

    if (!AFPGetVolumeInfoBlock(nullptr, volumeID, &volumeInfo))
    {
        //
        //We failed to find a block corresponding to the
        //supplied volume ID.
        //

        DBGWRITE(dbg_level_info, L"[AFPSession::SessionOpenVolume]Bad volume ID\n");
        return afpParmErr;
    }

    mOpenVolumes.push_back(volumeInfo);

    //
    //Incrementing the ref count tells our routines that a
    //user has the volume open and is accessing it's contents.
    //
    volumeInfo->dwRefCount++;

    DBGWRITE(
        dbg_level_all,
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
    auto_lock lock(&mMutex);

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
        DBGWRITE(dbg_level_all, L"[AFPSession::SessionCloseVolume]Volume %s was closed\n", (*it)->szShareName);

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
    auto_lock lock(&mMutex);

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
        auto_lock lock(&mMutex);

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
    auto_lock lock(&mMutex);

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
    auto_lock lock(&mMutex);

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
    auto pSession = g_sessions_list;

    auto_lock lock(&g_cs_session_list);

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
    auto pSession = g_sessions_list;

    auto_lock lock(&g_cs_session_list);

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
