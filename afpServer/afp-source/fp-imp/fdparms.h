#pragma once

#include "volumes.h"
#include "afp_net_buffer.h"
#include "dsi/dsi_connection.h"

size_t afp_get_parms_buffer_size_required(
    IN PFILEDIRPARMS file_dir_parms
);

AFPERROR AFPMapAFPToWindowsPath(
    IN WORD wVolID,
    IN DWORD dwDirID,
    IN PTCHAR pAFPPath,
    OUT PTCHAR pWindowsPath,
    IN size_t cbWindowsPath
);

void afp_check_for_and_create_rsrc_stream(
    IN const afp_index* index_entry
);

AFPERROR afp_get_object_parms(
    IN afp_net_buffer& reply,
    IN UINT16 bitmap,
    IN INT16 volume_id,
    IN UINT32 dir_id,
    IN const dsi::dsi_connection* dsi_session,
    IN afp_index* entry,
    IN WIN32_FILE_ATTRIBUTE_DATA win_attributes
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
