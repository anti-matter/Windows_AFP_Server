#pragma once

#include "volumes.h"
#include "afp_net_buffer.h"
#include "dsi/dsi_connection.h"


struct afp_index;

void afp_check_for_and_create_rsrc_stream(
    IN const afp_index* index_entry
);

AFPERROR afp_get_object_parms(
    IN afp_net_buffer& reply,
    IN UINT16 bitmap,
    IN INT16 volume_id,
    IN INT32 dir_id,
    IN const dsi::dsi_connection* dsi_session,
    IN afp_index* entry,
    IN WIN32_FILE_ATTRIBUTE_DATA win_attributes
);

AFPERROR afp_set_file_dir_parms(
    IN WORD bitmap,
    IN DWORD file_attributes,
    IN const afp_index* index_entry,
    IN afp_net_buffer& request
);

AFPERROR afp_set_file_time(
    IN const WCHAR* windows_path,
    IN BOOL is_directory,
    IN UINT32 creation,
    IN UINT32 modification
);

WORD win_to_afp_attributes(
    IN DWORD win_attributes
);
