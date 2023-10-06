/*
 *	afpdirectory.cpp
 *	Created 3/5/2005
 *
 *	Copyright © 2005, Michael J. Conrad. All Rights Reserved.
 *
 *	Core FP directory routines.
 */

#ifndef __afpdirectory__
#define __afpdirectory__

#include "afpnotify.h"
#include "fpinfo.h"
#include "fdparms.h"
#include "AfpBuffer.h"
#include "afp_session.h"


AFPERROR FPResolveID(
	IN	AfpBuffer&	afpRequest,
	IN	AfpBuffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPGetFileDirParms(
	IN	AfpBuffer&	afpRequest,
	IN	AfpBuffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPEnumerate(
	IN	AfpBuffer&	afpRequest,
	IN	AfpBuffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPSetFileDirParms(
	IN	AfpBuffer&	afpRequest,
	IN	AfpBuffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPCreateDir(
	IN	AfpBuffer&	afpRequest,
	IN	AfpBuffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPDelete(
	IN	AfpBuffer&	afpRequest,
	IN	AfpBuffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPCopyFile(
	IN	AfpBuffer&	afpRequest,
	IN	AfpBuffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPMoveAndRename(
	IN	AfpBuffer&	afpRequest,
	IN	AfpBuffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPRename(
	IN	AfpBuffer&	afpRequest,
	IN	AfpBuffer&	afpReply,
	IN	LPSESSION	afpSession
	);

#endif //__afpdirectory__
