/*
 *	afpfileio.h
 *	Created 3/5/2005
 *
 *	Copyright © 2005, Michael J. Conrad. All Rights Reserved.
 */

#ifndef __afpfileio__
#define __afpfileio__

#include "afphelp.h"
#include "fpinfo.h"
#include "fdparms.h"
#include "afpnotify.h"
#include "afp_net_buffer.h"
#include "afp_session.h"
#include "afp_open_file.h"


AFPERROR FPFlushFork(
	IN	afp_net_buffer&	afpRequest,
	IN	afp_net_buffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPCloseFork(
	IN	afp_net_buffer&	afpRequest,
	IN	afp_net_buffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPOpenFork(
	IN	afp_net_buffer&	afpRequest,
	IN	afp_net_buffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPSetForkParms(
	IN	afp_net_buffer&	afpRequest,
	IN	afp_net_buffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPGetForkParms(
	IN	afp_net_buffer&	afpRequest,
	IN	afp_net_buffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPCreateFile(
	IN	afp_net_buffer&	afpRequest,
	IN	afp_net_buffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPByteRangeLock(
	IN	afp_net_buffer&	afpRequest,
	IN	afp_net_buffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPRead(
	IN	afp_net_buffer&	afpRequest,
	IN	afp_net_buffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPWrite(
	IN	afp_net_buffer&	afpRequest,
	IN	afp_net_buffer&	afpReply,
	IN	LPSESSION	afpSession
	);


#endif //__afpfileio__
