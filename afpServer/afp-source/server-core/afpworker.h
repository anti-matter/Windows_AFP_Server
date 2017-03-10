//===================================================================
// afpworker.h
//===================================================================

#pragma once

#include "afp.h"
#include "volumes.h"
#include "afpServer.h"

void AFPInitializeWorkerThread(
	IN HWND hMainWindow
);
void AFPShutdownWorkerThread();

DWORD WINAPI AFPWorkerThread(
	IN LPVOID	pParam
);

void AFPWorkerAddElementToWorkerQueue(
	IN	PAFP_WORKER_ELEMENT		pNewWorkerElement
);

DWORD AFPGetIndexingWorkerQueueCount(
	IN	AFP_VOLUME_INFO*	pVolInfo
);
