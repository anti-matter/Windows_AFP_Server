#pragma once

#include "afp.h"
#include "volumes.h"

#define AFP_INFO_STREAM_NAME	TEXT("AFP_AfpInfo")


BOOL AFPCheckForAndAddFinderInfoStream(
    IN afp_index* pEntry,
    IN AFPCHARMAPARRAY pCharmap = nullptr,
    IN BOOL fMakeInvisible = false
);

AFPERROR AFPGetFinderInfo(
    IN afp_index* pEntry,
    OUT afp_finder_info* pInfo
);

AFPERROR AFPGetFinderInfoEx(
    IN afp_index* pEntry,
    OUT afp_extended_finder_info* pInfo
);

AFPERROR AFPSetFinderInfo(
    IN afp_index* pEntry,
    IN afp_finder_info* pInfo,
    IN BOOL fCreateAlways = false
);

AFPERROR AFPSetFinderInfoEx(
    IN afp_index* pEntry,
    IN afp_extended_finder_info* pInfo,
    IN BOOL fCreateAlways = false
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
