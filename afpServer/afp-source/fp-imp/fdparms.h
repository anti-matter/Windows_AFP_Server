#pragma once

#include "volumes.h"
#include "afp_net_buffer.h"

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
	IN	p_afp_index		pEntry
);

AFPERROR AFPGetObjectParms(
	IN	afp_net_buffer&				afpReply,
	IN	uint16						afpBitmap,
	IN	uint16						afpVolumeID,
	IN	int32						afpDirID,
	IN	LPSESSION					afpSession,
	IN	p_afp_index					afpEntry,
	IN	WIN32_FILE_ATTRIBUTE_DATA	winAttributes
	);

WORD AFPGetDirOffspringCount(
	IN	WORD		wVolID,
	IN	p_afp_index	pDirEntry
);

AFPERROR AFPSetFileDirParms(
	IN	WORD		wBitmap,
	IN	p_afp_index	pEntry,
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


