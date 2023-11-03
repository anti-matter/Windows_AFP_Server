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
    OUT PFINDER_INFO pInfo
);

AFPERROR AFPGetFinderInfoEx(
    IN afp_index* pEntry,
    OUT LPEXTENDED_FINDER_INFO pInfo
);

AFPERROR AFPSetFinderInfo(
    IN afp_index* pEntry,
    IN PFINDER_INFO pInfo,
    IN BOOL fCreateAlways = false
);

AFPERROR AFPSetFinderInfoEx(
    IN afp_index* pEntry,
    IN LPEXTENDED_FINDER_INFO pInfo,
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
