#pragma once

#include "volumes.h"
#include "afp_net_buffer.h"
#include "dsi/dsi_connection.h"

size_t AFPGetParmsBufferSizeRequired(
    IN PFILEDIRPARMS pFdp
);

AFPERROR AFPMapAFPToWindowsPath(
    IN WORD wVolID,
    IN DWORD dwDirID,
    IN PTCHAR pAFPPath,
    OUT PTCHAR pWindowsPath,
    IN size_t cbWindowsPath
);

void AFPCheckForAndCreateFinderRsrcStream(
    IN const afp_index* index_entry
);

AFPERROR AFPGetObjectParms(
    IN afp_net_buffer& afpReply,
    IN uint16 afpBitmap,
    IN uint16 afpVolumeID,
    IN UINT32 afpDirID,
    IN const dsi::dsi_connection* afpSession,
    IN afp_index* afpEntry,
    IN WIN32_FILE_ATTRIBUTE_DATA winAttributes
);

AFPERROR AFPSetFileDirParms(
    IN WORD wBitmap,
    IN afp_index* pEntry,
    IN PCHAR pParms,
    IN DWORD dwFileAttributes
);

AFPERROR AFPSetFileTime(
    IN PTCHAR pWindowspath,
    IN BOOL fIsDirectory,
    IN PULONG plwCreation,
    IN PULONG plwModification,
    IN PULONG plwBackup
);

WORD AFPWinToAFPAttributes(
    IN DWORD dwWinAttributes,
    OUT PWORD pwAFPAttributes
);

#ifdef DBG
void AFPExtractDirParms(
		IN PBYTE		pParms,		//Points to actual start of data
		IN WORD			wBitMap
);
#endif
