/*
 * fpinfo.h
 *
 * Copyright(c) 2000-2004, Michael J. Conrad.
 *
 */

#ifndef __fpinfo__
#define __fpinfo__

#include "afp.h"
#include "volumes.h"

#define AFP_INFO_STREAM_NAME	TEXT("AFP_AfpInfo")


BOOL AFPCheckForAndAddFinderInfoStream(
	IN	p_afp_index		pEntry,
	IN	AFPCHARMAPARRAY	pCharmap		= NULL,
	IN	BOOL			fMakeInvisible	= false
);

AFPERROR AFPGetFinderInfo(
	IN	p_afp_index		pEntry,
	OUT	PFINDER_INFO	pInfo
);

AFPERROR AFPGetFinderInfoEx(
	IN	p_afp_index				pEntry,
	OUT	LPEXTENDED_FINDER_INFO	pInfo
);

AFPERROR AFPSetFinderInfo(
	IN	p_afp_index		pEntry,
	IN	PFINDER_INFO	pInfo,
	IN	BOOL			fCreateAlways = false
);

AFPERROR AFPSetFinderInfoEx(
	IN	p_afp_index				pEntry,
	IN	LPEXTENDED_FINDER_INFO	pInfo,
	IN	BOOL					fCreateAlways = false
);

AFPERROR AFPUpdateEntryCharMap(
	IN	p_afp_index		pEntry,
	IN	AFPCHARMAPARRAY	pCharmap
);

AFPERROR AFPAddCharMapToIndexEntry(
	IN	p_afp_index		pEntry,
	IN	AFPCHARMAPARRAY	pCharmap
);

AFPERROR AFPGetCharMapForIndexEntry(
	IN	p_afp_index		pEntry,
	OUT	AFPCHARMAPARRAY	pCharmap
);

BOOL AFPCheckForCharMap(
	IN	p_afp_index	pEntry
);

#endif //__fpinfo__
