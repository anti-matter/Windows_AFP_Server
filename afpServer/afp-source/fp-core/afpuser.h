/*
 *	afpuser.h
 *	Created 3/5/2005
 *
 *	Copyright © 2005, Michael J. Conrad. All Rights Reserved.
 */

#ifndef __afpuser__
#define __afpuser__

#include "logonimp.h"
#include "afp_net_buffer.h"
#include "afp_session.h"
#include "AFPRegistry.h"


AFPERROR FPGetSessionToken(
	IN	afp_net_buffer&	afpRequest,
	IN	afp_net_buffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPDisconnectOldSession(
	IN	afp_net_buffer&	afpRequest,
	IN	afp_net_buffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPLogin(
	IN	afp_net_buffer&	afpRequest,
	IN	afp_net_buffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPContLogin(
	IN	afp_net_buffer&	afpRequest,
	IN	afp_net_buffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPChangePassword(
	IN	afp_net_buffer&	afpRequest,
	IN	afp_net_buffer&	afpReply,
	IN	LPSESSION	dsiSession
	);

AFPERROR FPLogout(
	IN	afp_net_buffer&	afpRequest,
	IN	afp_net_buffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPMapID(
	IN	afp_net_buffer&	afpRequest,
	IN	afp_net_buffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPGetSrvrMsg(
	IN	afp_net_buffer&	afpRequest,
	IN	afp_net_buffer&	afpReply,
	IN	LPSESSION	afpSession
	);

#endif //__afpuser__
