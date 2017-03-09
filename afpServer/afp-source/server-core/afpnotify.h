//===================================================================
// afpnotify.c
//===================================================================

#include "dsi.h"
#include "volumes.h"

DWORD WINAPI AFPNotifyThread(
		IN LPVOID	pParam
);

void AFPReadDirChanges(
	IN	PAFP_VOLUME_INFO	pVolInfo,
	IN	HANDLE				hDirectory,
	IN	PBYTE				pResultBuffer
);

void AFPChngNotifyAdd(
	IN	FILE_NOTIFY_INFORMATION*	pInfo,
	IN	PAFP_VOLUME_INFO			pVolInfo
);

void AFPChngNotifyRemove(
	IN	FILE_NOTIFY_INFORMATION*	pInfo,
	IN	PAFP_VOLUME_INFO			pVolInfo
);
