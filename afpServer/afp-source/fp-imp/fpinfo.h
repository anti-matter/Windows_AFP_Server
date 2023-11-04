#pragma once

#include "afp.h"
#include "volumes.h"

inline const WCHAR* afp_info_stream_name(L"AFP_AfpInfo");


BOOL create_finder_info_stream(
    IN afp_index* entry,
    IN AFPCHARMAPARRAY char_map = nullptr,
    IN bool make_invisible = false
);

AFPERROR get_finder_info(
    IN afp_index* entry,
    OUT afp_finder_info* finder_info
);

AFPERROR get_finder_info_ex(
    IN afp_index* entry,
    OUT afp_extended_finder_info* extended_finder_info
);

AFPERROR set_finder_info(
    IN afp_index* entry,
    IN const afp_finder_info* finder_info,
    IN bool create_always = false
);

AFPERROR set_finder_info_ex(
    IN afp_index* entry,
    IN const afp_extended_finder_info* extended_finder_info,
    IN bool create_always = false
);

AFPERROR update_entry_char_map(
    IN afp_index* entry,
    IN AFPCHARMAPARRAY char_map
);

AFPERROR add_char_map_to_index_entry(
    IN afp_index* entry,
    IN AFPCHARMAPARRAY char_map
);

AFPERROR get_char_map_for_index_entry(
    IN afp_index* entry,
    OUT AFPCHARMAPARRAY char_map
);

bool check_for_char_map(
    IN afp_index* entry
);
