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

AFPERROR AFPUpdateEntryCharMap(
    IN afp_index* pEntry,
    IN AFPCHARMAPARRAY pCharmap
);

AFPERROR AFPAddCharMapToIndexEntry(
    IN afp_index* pEntry,
    IN AFPCHARMAPARRAY pCharmap
);

AFPERROR AFPGetCharMapForIndexEntry(
    IN afp_index* pEntry,
    OUT AFPCHARMAPARRAY pCharmap
);

BOOL AFPCheckForCharMap(
    IN afp_index* pEntry
);
