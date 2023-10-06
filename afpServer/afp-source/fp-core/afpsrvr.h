/*
 *	afpsrvr.h
 *	Created 3/5/2005
 *
 *	Copyright © 2005, Michael J. Conrad. All Rights Reserved.
 */

#ifndef __afpsrvr__
#define __afpsrvr__

#include "afphelp.h"
#include "AfpBuffer.h"
#include "afp_session.h"


AFPERROR FPGetSrvrParms(
	IN	AfpBuffer&	afpRequest,
	IN	AfpBuffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPGetSrvrInfo(
	IN	AfpBuffer&	afpRequest,
	IN	AfpBuffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPZzzz(
	IN	AfpBuffer&	afpRequest,
	IN	AfpBuffer&	afpReply,
	IN	LPSESSION	afpSession
	);

#endif //__afpsrvr__
