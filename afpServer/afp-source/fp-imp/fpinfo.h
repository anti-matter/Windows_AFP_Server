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
	IN	LPAFPINDEX		pEntry,
	IN	AFPCHARMAPARRAY	pCharmap		= NULL,
	IN	BOOL			fMakeInvisible	= false
);

AFPERROR AFPGetFinderInfo(
	IN	LPAFPINDEX		pEntry,
	OUT	PFINDER_INFO	pInfo
);

AFPERROR AFPGetFinderInfoEx(
	IN	LPAFPINDEX				pEntry,
	OUT	LPEXTENDED_FINDER_INFO	pInfo
);

AFPERROR AFPSetFinderInfo(
	IN	LPAFPINDEX		pEntry,
	IN	PFINDER_INFO	pInfo,
	IN	BOOL			fCreateAlways = false
);

AFPERROR AFPSetFinderInfoEx(
	IN	LPAFPINDEX				pEntry,
	IN	LPEXTENDED_FINDER_INFO	pInfo,
	IN	BOOL					fCreateAlways = false
);

AFPERROR AFPUpdateEntryCharMap(
	IN	LPAFPINDEX		pEntry,
	IN	AFPCHARMAPARRAY	pCharmap
);

AFPERROR AFPAddCharMapToIndexEntry(
	IN	LPAFPINDEX		pEntry,
	IN	AFPCHARMAPARRAY	pCharmap
);

AFPERROR AFPGetCharMapForIndexEntry(
	IN	LPAFPINDEX		pEntry,
	OUT	AFPCHARMAPARRAY	pCharmap
);

BOOL AFPCheckForCharMap(
	IN	LPAFPINDEX	pEntry
);

#endif //__fpinfo__
