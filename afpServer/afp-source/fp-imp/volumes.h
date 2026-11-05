#pragma once

#include "afp.h"
#include "afp_net_buffer.h"

DWORD AFPAddNewVolumeFromUI(
    IN WCHAR* share_name,
    IN WCHAR* share_path,
    IN WCHAR* share_password
);

DWORD AFPRemoveVolumeFromUI(
    IN WCHAR* share_name
);

AFPERROR AFPGetVolumeParameters(
    IN INT16 afp_volume_id,
    IN INT16 afp_bitmap,
    IN UINT8 afp_version,
    IN afp_net_buffer& afp_reply
);
