//===================================================================
// afpnotify.c
//===================================================================

#pragma once

struct AFP_VOLUME_INFO;

DWORD WINAPI AFPNotifyThread(
		IN LPVOID	pParam
);

void AFPReadDirChanges(
	IN	AFP_VOLUME_INFO*	pVolInfo,
	IN	HANDLE				hDirectory,
	IN	PBYTE				pResultBuffer
);

void AFPChngNotifyAdd(
	IN	FILE_NOTIFY_INFORMATION*	pInfo,
	IN	AFP_VOLUME_INFO*			pVolInfo
);

void AFPChngNotifyRemove(
	IN	FILE_NOTIFY_INFORMATION*	pInfo,
	IN	AFP_VOLUME_INFO*			pVolInfo
);
