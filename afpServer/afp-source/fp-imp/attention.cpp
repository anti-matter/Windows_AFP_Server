//===================================================================
// attention.c
//===================================================================

#include "StdAfx.h"
#include "afpdebug.h"
#include "volumes.h"
#include "attention.h"
#include "LAFPSession.h"
#include "VolumeArray.h"

extern CRITICAL_SECTION gcsSessionList;
extern BOOL gRunning;
extern LPSESSION gSessionsList;
extern VolumeArray gVolumeArray;
extern SHORT gWorkerThreadRefCount;

//---------------------------------------------------------------------
// AFPAttentionThread()
//---------------------------------------------------------------------
// Watches for dirty volumes and sends attention messages as
// necessary to flag an update is required.
//

DWORD WINAPI AFPAttentionThread(
    IN LPVOID pParam
)
{
    UNREFERENCED_PARAMETER(pParam);

    LPSESSION pSession;

    //
    //This is how we keep track of all running "worker" threads.
    //
    gWorkerThreadRefCount++;

    while (gRunning)
    {
        {
            AutoLock lock(gVolumeArray.Mutex());

            for (auto it = gVolumeArray.begin(); it < gVolumeArray.end(); ++it)
            {
                AFP_VOLUME_INFO* pVolInfo = it->get();

                //
                //Check to see if the volume is marked dirty. If so, send the
                //attention code.
                //
                if (pVolInfo->fDirty)
                {
                    EnterCriticalSection(&gcsSessionList);

                    pSession = gSessionsList;

                    while (pSession)
                    {
                        EnterCriticalSection(&pSession->cs);

                        //
                        //It is possible the session pointer exists without the session object. This
                        //happens when we get here before a DSIOpenSession has been called for a
                        //particular DSI session.
                        //
                        if ((pSession->dwState & sessClosing) || (pSession->afpSessionObject == NULL))
                        {
                            LeaveCriticalSection(&pSession->cs);
                            pSession = static_cast<LPSESSION>(pSession->next);
                            continue;
                        }

                        LeaveCriticalSection(&pSession->cs);

                        if (static_cast<LAFPSession*>(pSession->afpSessionObject)->SessionHasVolumeOpen(pVolInfo->wVolID))
                        {
                            DSISendAttention(pSession, ATTN_SERVER_NOTIFY);
                        }

                        pSession = static_cast<LPSESSION>(pSession->next);
                    }

                    LeaveCriticalSection(&gcsSessionList);

                    //
                    //Take this opportunity to clear the dirty flag for the volume.
                    //
                    pVolInfo->fDirty = FALSE;
                }
            }
        }

        Sleep(ATTN_SLEEP_INTERVAL);
    }

    gWorkerThreadRefCount--;

    DBGWRITE(DbgLevelInfo, L"[ATTN]Attention thread gone!\n");

    return 0;
}
