/*
 * counters.cpp
 *
 * Copyright(c) 2000-2009, Michael J. Conrad.
 *
 * Performance counter routines live here.
 */

#include <winerror.h>

#include "stdafx.h"
#include "afpdebug.h"
#include "counters.h"

//
//These are the counter id's, we have to provide ourselves since
//ctrpp doesn't auto-generate them for us in the header (why??)
//
#define ID_CONNECTIONS			1
#define ID_BYTESRCVDPERSEC		2
#define ID_BYTESSENTPERSEC		3
#define ID_AFPREQUESTSPROCESSED	4

//
//A couple needed globals to keep the thread running, etc.
//
extern BOOL		g_running;				//from dsi.cpp, true while app is running
extern SHORT	g_worker_thread_ref_count;	//Thread reference counter so we wait while cleaining up

//
//These globals are needed to track some of the perf
//numbers we collect from various parts of the server
//
DWORD	gCurrentReceivedByteCount		= 0;
DWORD	gCurrentSentByteCount			= 0;
DWORD	gCurrentAFPRequestsProcessed	= 0;


/*
 * AFPInitializePerfCounters()
 *
 * Description:
 *		Initializes counters and install the perf worker thread.
 *
 * Returns: status
 */

ULONG AFPInitializePerfCounters()
{
    DWORD	dwThreadID	= 0;
    HANDLE	hThread		= INVALID_HANDLE_VALUE;
    ULONG	status		= ERROR_SUCCESS;

    status = CounterInitialize(NULL, NULL, NULL, NULL);

    if (status == ERROR_SUCCESS)
    {
        hThread = CreateThread(
                        NULL,
                        0,
                        AFPCounterThread,
                        NULL,
                        0,
                        &dwThreadID
        );

        if (hThread != INVALID_HANDLE_VALUE) {
            CloseHandle(hThread);
        }
    }
    else
    {
        DBGWRITE(dbg_level_warning,
            L"[counters]CounterInitialize() returned failure: %d\n", status
            );
    }

    return(status);
}


/*
 * AFPUpdateBytesPerSecond()
 *
 * Description:
 *		Update the number of bytes the server is process
 *		per second. If you don't want to update one parameter,
 *		pass NULL.
 *
 * Returns: status
 */

void AFPUpdateBytesPerSecond(
    DWORD	*dwReceivedBytes,
    DWORD	*dwSentBytes
    )
{
    if (dwReceivedBytes != NULL)
    {
        gCurrentReceivedByteCount += *dwReceivedBytes;
    }

    if (dwSentBytes != NULL)
    {
        gCurrentSentByteCount += *dwSentBytes;
    }
}


/*
 * AFPUpdateAFPRequestsProcessed()
 *
 * Description:
 *		Updates the AFP requests processed counter.
 *
 * Returns: void
 */

void AFPUpdateAFPRequestsProcessed(void)
{
    gCurrentAFPRequestsProcessed++;
}


/*
 * AFPCreateCounterInstance()
 *
 * Description:
 *		Create instances for our performance counters.
 *
 * Returns: status
 */

ULONG AFPCreateCounterInstance(
    PPERF_COUNTERSET_INSTANCE *connectionInstance
)
{
    *connectionInstance	= NULL;

    *connectionInstance = PerfCreateInstance(AFPServerCounters, &ServerDetailsGuid, L"details", 0);
    if (*connectionInstance == NULL) {
        return GetLastError();
    }

    return( ERROR_SUCCESS );
}


/*
 * AFPCounterThread()
 *
 * Description:
 *		Thread which updates counter data periodically.
 *
 * Returns: nothing
 */

DWORD WINAPI AFPCounterThread(
        IN LPVOID	pParam
)
{
    UNREFERENCED_PARAMETER(pParam);

    PPERF_COUNTERSET_INSTANCE	connectionsInstance	= NULL;
    ULONG						status = ERROR_SUCCESS;

    //
    //Increment thread reference count so the app will wait until thread is
    //shutdown and cleanup complete before quitting.
    //
    g_worker_thread_ref_count++;

    status = AFPCreateCounterInstance(&connectionsInstance);
    if (status != ERROR_SUCCESS)
    {
        DBGWRITE(dbg_level_warning,
            L"[**counters**]AFPCreateCounterInstance() returned failure: %d\n", status
            );

        goto cleanup;
    }

    while(g_running)
    {
        //
        //Update the number of DSI connections active for this server.
        //
        status = PerfSetULongCounterValue(
                            AFPServerCounters,
                            connectionsInstance,
                            ID_CONNECTIONS,
                            (ULONG)DSICountSessions(true)
                            );

        if (status != ERROR_SUCCESS)
        {
            DBGWRITE(dbg_level_warning,
                L"[counters]PerfSetULongCounterValue(ID_CONNECTIONS) returned failure: %d\n", status
                );

            //
            //What should I do here? We bail just because we don't
            //know what else to do.
            //
            break;
        }

        //
        //Update the bytes received per second. We just use the timing of this
        //thread as the timer. Not super accurate, but good enough.
        //
        status = PerfSetULongCounterValue(
                            AFPServerCounters,
                            connectionsInstance,
                            ID_BYTESRCVDPERSEC,
                            gCurrentReceivedByteCount
                            );

        if (status != ERROR_SUCCESS)
        {
            DBGWRITE(dbg_level_warning,
                L"[counters]PerfSetULongCounterValue(ID_BYTESRCVDPERSEC) returned failure: %d\n", status
                );

            //
            //What should I do here? We bail just because we don't
            //know what else to do.
            //
            break;
        }

        //
        //We need to reset the count back to zero for the next time around.
        //
        gCurrentReceivedByteCount = 0;

        //
        //Update the bytes sent by the server per second. Same as above
        //for received bytes.
        //
        status = PerfSetULongCounterValue(
                            AFPServerCounters,
                            connectionsInstance,
                            ID_BYTESSENTPERSEC,
                            gCurrentSentByteCount
                            );

        if (status != ERROR_SUCCESS)
        {
            DBGWRITE(dbg_level_warning,
                L"[counters]PerfSetULongCounterValue(ID_BYTESSENTPERSEC) returned failure: %d\n", status
                );

            //
            //What should I do here? We bail just because we don't
            //know what else to do.
            //
            break;
        }

        //
        //We need to reset the count back to zero for the next time around.
        //
        gCurrentSentByteCount = 0;

        //
        //Update the AFP requests processed in the DSI engine.
        //
        status = PerfSetULongCounterValue(
                            AFPServerCounters,
                            connectionsInstance,
                            ID_AFPREQUESTSPROCESSED,
                            gCurrentAFPRequestsProcessed
                            );

        if (status != ERROR_SUCCESS)
        {
            DBGWRITE(dbg_level_warning,
                L"[counters]PerfSetULongCounterValue(ID_AFPREQUESTSPROCESSED) returned failure: %d\n", status
                );

            //
            //What should I do here? We bail just because we don't
            //know what else to do.
            //
            break;
        }

        //
        //We need to reset the count back to zero for the next time around.
        //
        gCurrentAFPRequestsProcessed = 0;

        Sleep(SLEEP_INTERVAL);
    }

cleanup:

    g_worker_thread_ref_count--;

    //
    //This is from afpCounters.h, the auto-generated file from
    //the perf counter mainfest.
    //
    CounterCleanup();

    return(0);
}