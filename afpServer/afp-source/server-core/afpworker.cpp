//===================================================================
// afpworker.c
//
// This unit contains a worker thread that performs many queued
// operations.
//===================================================================

#include "stdafx.h"
#include <strsafe.h>

#include "afpworker.h"
#include "afp.h"
#include "volumes.h"
#include "AFPFileIndex.h"
#include "afpEvents.h"

extern BOOL		g_running;
extern SHORT	g_worker_thread_ref_count; 

HANDLE					gWorkerEvent		= INVALID_HANDLE_VALUE;
PAFP_WORKER_ELEMENT		gWorkerElements		= NULL;
HWND					gMainWindow			= NULL;
CRITICAL_SECTION		gWorkerCS;

//---------------------------------------------------------------------
// AFPInitializeWorkerThread()
//---------------------------------------------------------------------
// Initialize the worker thread.
//

void AFPInitializeWorkerThread(IN HWND hMainWindow)
{
    DWORD	dwThreadID;
    HANDLE	hThread;

    g_running	= TRUE;
    gMainWindow	= hMainWindow;

    InitializeCriticalSection(&gWorkerCS);

    gWorkerEvent = CreateEvent(NULL, TRUE, TRUE, NULL);
    ResetEvent(gWorkerEvent);

    hThread = CreateThread(
                    NULL,
                    0,
                    AFPWorkerThread,
                    NULL,
                    0,
                    &dwThreadID
    );

    CloseHandle(hThread);
}


//---------------------------------------------------------------------
// AFPShutdownWorkerThread()
//---------------------------------------------------------------------
// Close the worker thread.
//

void AFPShutdownWorkerThread()
{
    ASSERT(g_running == FALSE);

    EnterCriticalSection(&gWorkerCS);
    SetEvent(gWorkerEvent);
    LeaveCriticalSection(&gWorkerCS);
}


//---------------------------------------------------------------------
// AFPWorkerThread()
//---------------------------------------------------------------------
// A worker thread that is event driven and can perform a number
// of operations.
//

DWORD WINAPI AFPWorkerThread(
        IN LPVOID	pParam
)
{
    UNREFERENCED_PARAMETER(pParam);

    PAFP_WORKER_ELEMENT		workerElement, nextElement;

    g_worker_thread_ref_count++;
    
    while(g_running)
    {
        //
        //The event will be signaled when there is data queued up to be sent.
        //
        WaitForSingleObject(gWorkerEvent, INFINITE);

        //
        //Check to see if we're shutting down or not. If so, then we close
        //our event handle and break out.
        //
        if (!g_running)
        {
            CloseHandle(gWorkerEvent);
            break;
        }

        EnterCriticalSection(&gWorkerCS);

        workerElement		= gWorkerElements;
        gWorkerElements		= NULL;

        ResetEvent(gWorkerEvent);

        LeaveCriticalSection(&gWorkerCS);

        while(workerElement != NULL)
        {
            nextElement = (PAFP_WORKER_ELEMENT)workerElement->next;

            switch(workerElement->dwWorkerFunction)
            {
                case AFP_WORKER_DELETE_INDEX_ENTRY:
                    ((AFPFileIndex*)workerElement->pParam1)->RemoveEntry(
                            ((LPAFPINDEX)workerElement->pParam2)->objectID,
                            true
                            );
                    
                    //
                    //Decrement the ref counter preventing the class from going away.
                    //
                    ((AFPFileIndex*)workerElement->pParam1)->DecRef();
                    break;

                case AFP_WORKER_INDEX_DIRECTORY:
                    AFPIndexBuild(
                        (PWCHAR)workerElement->pParam2,
                        workerElement->dwParam,
                        (AFP_VOLUME_INFO*)workerElement->pParam1
                        );

                    //
                    //Free the allocated path buffer so we don't leak.
                    //
                    DBG_FREE(workerElement->pParam2);
                    break;

                case AFP_WORKER_INDEX_COMPLETE:
                    //
                    //When the root dir has finished indexing, there still may
                    //be other elements in the queue being added to recurse through
                    //other embedded directories. If more indexing is still happening
                    //postpone the index complete message till later by putting it
                    //at the end of the queue.
                    //
                    if (AFPGetIndexingWorkerQueueCount((AFP_VOLUME_INFO*)workerElement->pParam1) != 0)
                    {
                        DBGWRITE(dbg_level_info,
                                L"[worker]Postponed index complete message for %s!\n",
                                ((AFP_VOLUME_INFO*)workerElement->pParam1)->szShareName
                                );
                        
                        workerElement->next = NULL;
                        AFPWorkerAddElementToWorkerQueue(workerElement);
                        
                        workerElement = nextElement;					
                        continue;
                    }
                    else
                    {
                        //
                        //Now that indexing is done for this volume, we make it available
                        //to the clients.
                        //
                        ((AFP_VOLUME_INFO*)workerElement->pParam1)->wVolumeFlags |= FLAG_VOLUME_INDEXINGCOMPLETE;

                        //
                        //Log that the specific volume has completed indexing.
                        //
                        EventWriteAFPServerEvt_VolIndexingComplete(((AFP_VOLUME_INFO*)workerElement->pParam1)->szShareName);
                        
                        DBGWRITE(dbg_level_info,
                                L"[worker]Indexing for volume (%s) complete!\n",
                                ((AFP_VOLUME_INFO*)workerElement->pParam1)->szShareName
                                );

                        //
                        //Send a message to the main window so that we can update UI to
                        //indicate indexing is complete (if necessary).
                        //
                        SendMessage(gMainWindow, AFPWM_INDEXING_COMPLETE, (WPARAM)NULL, (LPARAM)NULL);

                        //AFPDumpAFPIndex(((AFP_VOLUME_INFO*)workerElement->pParam1)->wVolID);
                    }
                    break;

                default:
                    break;
            }
    
            DBG_FREE(workerElement);
            workerElement = nextElement;
        }
    }

    g_worker_thread_ref_count--;
    return(0);
}


//---------------------------------------------------------------------
// AFPWorkerAddElementToWorkerQueue()
//---------------------------------------------------------------------
// Add a new worker element to the worker queue.
//

void AFPWorkerAddElementToWorkerQueue(
    IN	PAFP_WORKER_ELEMENT		pNewWorkerElement
)
{
    PAFP_WORKER_ELEMENT		pElementList = NULL;

    EnterCriticalSection(&gWorkerCS);

    if (gWorkerElements == NULL)
    {
        gWorkerElements = pNewWorkerElement;
    }
    else
    {
        pElementList = gWorkerElements;

        while(pElementList->next)
        {
            pElementList = (PAFP_WORKER_ELEMENT)pElementList->next;
        }

        pElementList->next = pNewWorkerElement;
    }

    LeaveCriticalSection(&gWorkerCS);
    SetEvent(gWorkerEvent);
}


//---------------------------------------------------------------------
// AFPGetWorkerQueueCount()
//---------------------------------------------------------------------
// Returns how many requests are in the queue for a specific task.
//

DWORD AFPGetIndexingWorkerQueueCount(
    IN	AFP_VOLUME_INFO*	pVolInfo
    )
{
    PAFP_WORKER_ELEMENT		workerElement;
    DWORD					dwCount = 0;
    
    EnterCriticalSection(&gWorkerCS);
    
    workerElement = gWorkerElements;
    
    while(workerElement)
    {
        if ((workerElement->dwWorkerFunction == AFP_WORKER_INDEX_DIRECTORY) 	&&
            (pVolInfo == (AFP_VOLUME_INFO*)workerElement->pParam1)				)
        {
            dwCount++;
        }
        
        workerElement = (PAFP_WORKER_ELEMENT)workerElement->next;
    }
    
    LeaveCriticalSection(&gWorkerCS);
    return( dwCount );
}

