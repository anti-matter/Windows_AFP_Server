/*
 *	afpuser.h
 *	Created 3/5/2005
 *
 *	Copyright © 2005, Michael J. Conrad. All Rights Reserved.
 */

#ifndef __afpuser__
#define __afpuser__

#include "logonimp.h"
#include "AfpBuffer.h"
#include "LAFPSession.h"
#include "LRegistry.h"


AFPERROR FPGetSessionToken(
	IN	AfpBuffer&	afpRequest,
	IN	AfpBuffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPDisconnectOldSession(
	IN	AfpBuffer&	afpRequest,
	IN	AfpBuffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPLogin(
	IN	AfpBuffer&	afpRequest,
	IN	AfpBuffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPContLogin(
	IN	AfpBuffer&	afpRequest,
	IN	AfpBuffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPChangePassword(
	IN	AfpBuffer&	afpRequest,
	IN	AfpBuffer&	afpReply,
	IN	LPSESSION	dsiSession
	);

AFPERROR FPLogout(
	IN	AfpBuffer&	afpRequest,
	IN	AfpBuffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPMapID(
	IN	AfpBuffer&	afpRequest,
	IN	AfpBuffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPGetSrvrMsg(
	IN	AfpBuffer&	afpRequest,
	IN	AfpBuffer&	afpReply,
	IN	LPSESSION	afpSession
	);

#endif //__afpuser__
