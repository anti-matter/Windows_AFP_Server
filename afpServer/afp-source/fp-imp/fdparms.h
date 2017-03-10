//===================================================================
// fdparms.h
//===================================================================

#pragma once

#include "volumes.h"

size_t AFPGetParmsBufferSizeRequired(
	IN	PFILEDIRPARMS	pFdp
);

AFPERROR AFPMapAFPToWindowsPath(
	IN	WORD	wVolID,
	IN	DWORD	dwDirID,
	IN	PTCHAR	pAFPPath,
	OUT PTCHAR	pWindowsPath,
	IN	size_t	cbWindowsPath
);

void AFPCheckForAndCreateFinderRsrcStream(
	IN	LPAFPINDEX		pEntry
);

AFPERROR AFPGetObjectParms(
	IN	AfpBuffer&					afpReply,
	IN	uint16						afpBitmap,
	IN	uint16						afpVolumeID,
	IN	int32						afpDirID,
	IN	LPSESSION					afpSession,
	IN	LPAFPINDEX					afpEntry,
	IN	WIN32_FILE_ATTRIBUTE_DATA	winAttributes
	);

WORD AFPGetDirOffspringCount(
	IN	WORD		wVolID,
	IN	LPAFPINDEX	pDirEntry
);

AFPERROR AFPSetFileDirParms(
	IN	WORD		wBitmap,
	IN	LPAFPINDEX	pEntry,
	IN	PCHAR		pParms,
	IN	DWORD		dwFileAttributes
);

AFPERROR AFPSetFileTime(
	IN	PTCHAR	pWindowspath,
	IN	BOOL	fIsDirectory,
	IN	PULONG	plwCreation,
	IN	PULONG	plwModification,
	IN	PULONG	plwBackup
);

WORD AFPWinToAFPAttributes(
	IN	DWORD	dwWinAttributes,
	OUT	PWORD	pwAFPAttributes
);

#ifdef DBG
void AFPExtractDirParms(
		IN PBYTE		pParms,		//Points to actual start of data
		IN WORD			wBitMap
);
#endif


