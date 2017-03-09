/*
 * counters.h
 *
 * Copyright(c) 2000-2009, Michael J. Conrad.
 *
 * Performance counter routines live here.
 */

#include <perflib.h>
#include <winperf.h>

#include "dsi.h"
#include "afpCounters.h"

#pragma once

//
//This is how long the counter thread will sleep between sampling
//
#define	SLEEP_INTERVAL	1000

ULONG AFPInitializePerfCounters();

void AFPUpdateBytesPerSecond(
	 DWORD	*dwReceivedBytes,
	 DWORD	*dwSentBytes
	 );

void AFPUpdateAFPRequestsProcessed(void);

ULONG AFPCreateCounterInstance(
    PPERF_COUNTERSET_INSTANCE *connectionInstance
	);

DWORD WINAPI AFPCounterThread(
		IN LPVOID	pParam
	);