/*
 *	afpvolumes.h
 *	Created 3/5/2005
 *
 *	Copyright © 2005, Michael J. Conrad. All Rights Reserved.
 */

#ifndef __afpvolumes__
#define __afpvolumes__

#include "afp_net_buffer.h"
#include "afp_session.h"


AFPERROR FPOpenVol(
	IN	afp_net_buffer&	afpRequest,
	IN	afp_net_buffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPCloseVol(
	IN	afp_net_buffer&	afpRequest,
	IN	afp_net_buffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPFlush(
	IN	afp_net_buffer&	afpRequest,
	IN	afp_net_buffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPGetVolParms(
	IN	afp_net_buffer&	afpRequest,
	IN	afp_net_buffer&	afpReply,
	IN	LPSESSION	afpSession
	);

#endif //__afpvolumes__
