/*
 *	afpdesktop.h
 *	Created 3/5/2005
 *
 *	Copyright © 2005, Michael J. Conrad. All Rights Reserved.
 */

#ifndef __afpdesktop__
#define __afpdesktop__

#include "fdparms.h"
#include "afpnotify.h"
#include "AfpBuffer.h"
#include "LAFPSession.h"


AFPERROR FPOpenDT(
	IN	AfpBuffer&	afpRequest,
	IN	AfpBuffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPCloseDT(
	IN	AfpBuffer&	afpRequest,
	IN	AfpBuffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPAddIcon(
	IN	AfpBuffer&	afpRequest,
	IN	AfpBuffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPGetIcon(
	IN	AfpBuffer&	afpRequest,
	IN	AfpBuffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPGetIconInfo(
	IN	AfpBuffer&	afpRequest,
	IN	AfpBuffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPAddComment(
	IN	AfpBuffer&	afpRequest,
	IN	AfpBuffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPGetComment(
	IN	AfpBuffer&	afpRequest,
	IN	AfpBuffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPRemoveComment(
	IN	AfpBuffer&	afpRequest,
	IN	AfpBuffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPGetAPPL(
	IN	AfpBuffer&	afpRequest,
	IN	AfpBuffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPAddAPPL(
	IN	AfpBuffer&	afpRequest,
	IN	AfpBuffer&	afpReply,
	IN	LPSESSION	afpSession
	);

AFPERROR FPRemoveAPPL(
	IN	AfpBuffer&	afpRequest,
	IN	AfpBuffer&	afpReply,
	IN	LPSESSION	afpSession
	);


#endif //__afpdesktop__
