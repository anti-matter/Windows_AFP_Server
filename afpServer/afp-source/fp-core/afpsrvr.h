/*
 *	afpsrvr.h
 *	Created 3/5/2005
 *
 *	Copyright © 2005, Michael J. Conrad. All Rights Reserved.
 */

#ifndef __afpsrvr__
#define __afpsrvr__

#include "afphelp.h"
#include "afp_net_buffer.h"
#include "afp_session.h"


AFPERROR FPGetSrvrParms(
	IN	afp_net_buffer&	afpRequest,
	IN	afp_net_buffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPGetSrvrInfo(
	IN	afp_net_buffer&	afpRequest,
	IN	afp_net_buffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPZzzz(
	IN	afp_net_buffer&	afpRequest,
	IN	afp_net_buffer&	afpReply,
	IN	LPSESSION	afpSession
	);

#endif //__afpsrvr__
