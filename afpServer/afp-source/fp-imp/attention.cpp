//===================================================================
// attention.c
//===================================================================

#include "StdAfx.h"
#include "afpdebug.h"
#include "volumes.h"
#include "attention.h"
#include "LAFPSession.h"

extern CRITICAL_SECTION	gcsSessionList;
extern BOOL				gRunning;
extern LPSESSION		gSessionsList;
extern SHORT			gWorkerThreadRefCount;

//---------------------------------------------------------------------
// AFPAttentionThread()
//---------------------------------------------------------------------
// Watches for dirty volumes and sends attention messages as
// necessary to flag an update is required.
//

DWORD WINAPI AFPAttentionThread(
    IN LPVOID	pParam
)
{
    UNREFERENCED_PARAMETER(pParam);

    PAFP_VOLUME_INFO	pVolInfo;
    LPSESSION			pSession;
    WORD				i, wNumVolumes;
    
    //
    //This is how we keep track of all running "worker" threads.
    //
    gWorkerThreadRefCount++;
    
    while(gRunning)
    {	
        wNumVolumes = AFPNumVolumesShared();
        
        for (i = 1; i <= wNumVolumes; i++)
        {
            if (AFPGetVolumeInfoAtIndex(i, &pVolInfo) == ERROR_INVALID_DRIVE)
            {
                //
                //We're at the end of the volume list, get outta here.
                //
                break;
            }
            
            //
            //Check to see if the volume is marked dirty. If so, send the
            //attention code.
            //
            if (pVolInfo->fDirty)
            {
                EnterCriticalSection(&gcsSessionList);
                
                pSession = gSessionsList;
                
                while(pSession)
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
                        pSession = (LPSESSION)pSession->next;
                        continue;
                    }
                    
                    LeaveCriticalSection(&pSession->cs);

                    if (((LAFPSession*)pSession->afpSessionObject)->SessionHasVolumeOpen(pVolInfo->wVolID))
                    {						
                        DSISendAttention(pSession, ATTN_SERVER_NOTIFY);
                    }
                    
                    pSession = (LPSESSION)pSession->next;
                }
                
                LeaveCriticalSection(&gcsSessionList);
                
                //
                //Take this opportunity to clear the dirty flag for the volume.
                //
                pVolInfo->fDirty = FALSE;
            }
        }
                
        Sleep(ATTN_SLEEP_INTERVAL);
    }
    
    gWorkerThreadRefCount--;
    
    DBGWRITE(DbgLevelInfo, L"[ATTN]Attention thread gone!\n");
    
    return(0);
}




