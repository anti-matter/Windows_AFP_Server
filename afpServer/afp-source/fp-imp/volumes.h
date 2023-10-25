#pragma once

#include "afp.h"
#include "afp_file_index.h"
#include "afp_net_buffer.h"
#include "afp_volume.h"
#include "desktop_database.h"

DWORD
AFPAddNewVolumeFromUI(
    IN WCHAR* share_name,
    IN WCHAR* share_path,
    IN WCHAR* share_password
);

DWORD AFPRemoveVolumeFromUI(
    IN WCHAR* share_name
);

AFPERROR AFPGetVolumeParameters(
    IN WORD afpVolumeID,
    IN WORD afpBitmap,
    IN afp_net_buffer& afpReply
);

AFPERROR AFPIndexUpdateEntryFlags(
    IN afp_index* pEntry,
    IN WORD wFlags
);

AFPERROR
AFPIndexFindEntryWithVolIDAndPath(
    IN WORD wVolID,
    IN DWORD dwDirID,
    IN PWCHAR pPath,
    OUT afp_index** ppEntry
);

AFPERROR
AFPIndexFindEntryWithPath(
    IN const std::shared_ptr<afp_volume>& pVolInfo,
    IN DWORD dwSearchID,
    IN PWCHAR pPath,
    OUT afp_index** ppEntry
);

AFPERROR
AFPVolumeWriteAccessCheck(
    IN WORD volume_id
);

AFPERROR
AFPCheckVolumePassword(
    IN WORD volume_id,
    IN const PassWord* password
);

#ifdef _DEBUG

void AFPDumpAFPIndex(
    IN afp_index* index
);

#endif
