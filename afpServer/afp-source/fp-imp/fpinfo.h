#pragma once

#include "afp.h"
#include "volumes.h"

struct afp_index;

inline const WCHAR* afp_info_stream_name(L"AFP_AfpInfo");


void create_finder_info_stream(
    IN const afp_index* entry,
    IN bool make_invisible = false
);

AFPERROR get_finder_info(
    IN const afp_index* entry,
    OUT afp_finder_info* finder_info
);

AFPERROR set_finder_info(
    IN const afp_index* entry,
    IN const afp_finder_info* finder_info,
    IN bool create_always = false
);
