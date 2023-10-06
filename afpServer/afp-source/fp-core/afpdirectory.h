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
#include "afp_net_buffer.h"
#include "afp_session.h"


AFPERROR FPResolveID(
	IN	afp_net_buffer&	afpRequest,
	IN	afp_net_buffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPGetFileDirParms(
	IN	afp_net_buffer&	afpRequest,
	IN	afp_net_buffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPEnumerate(
	IN	afp_net_buffer&	afpRequest,
	IN	afp_net_buffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPSetFileDirParms(
	IN	afp_net_buffer&	afpRequest,
	IN	afp_net_buffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPCreateDir(
	IN	afp_net_buffer&	afpRequest,
	IN	afp_net_buffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPDelete(
	IN	afp_net_buffer&	afpRequest,
	IN	afp_net_buffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPCopyFile(
	IN	afp_net_buffer&	afpRequest,
	IN	afp_net_buffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPMoveAndRename(
	IN	afp_net_buffer&	afpRequest,
	IN	afp_net_buffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPRename(
	IN	afp_net_buffer&	afpRequest,
	IN	afp_net_buffer&	afpReply,
	IN	LPSESSION	afpSession
	);

#endif //__afpdirectory__
