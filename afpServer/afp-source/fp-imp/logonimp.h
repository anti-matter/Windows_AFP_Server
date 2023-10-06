/*
 * 	logonimp.h
 *	Copyright (c) 2005, Some-guy Software.
 *
 */

#ifndef __logonimp__
#define __logonimp__

#include "afp_net_buffer.h"

//
//BLOODSHED: This is not in the ming headers for some stupid reason
//
#define LOGON32_LOGON_NETWORK       3


AFPERROR afpImpUserLogon(
	LPSESSION	afpSession,
	afp_net_buffer&	afpReply,
	WCHAR*		afpPasswordW,
	WORD		cbAfpPasswordW
);

#endif //__logonimp__
