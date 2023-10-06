/*
 *	afpextattr.h
 *	Created 09/18/2005
 *
 *	Copyright (c) 2003-05, Michael J. Conrad. All Rights Reserved.
 */

#pragma once

#include "afp.h"
#include "afp_session.h"
#include <afpfileio.h>

#define BAD_LENGTH_64	0xFFFFFFFFFFFFFFFF
#define BAD_LENGTH_32	0xFFFFFFFF

enum
{
    kXAttrNoFollow = 0x1,
    kXAttrCreate = 0x2,
    kXAttrReplace = 0x4
};

AFPERROR FPGetExtAttribute(
    IN AfpBuffer& afpRequest,
       IN AfpBuffer& afpReply,
       IN LPSESSION afpSession
);

AFPERROR FPSetExtAttribute(
    IN AfpBuffer& afpRequest,
       IN AfpBuffer& afpReply,
       IN LPSESSION afpSession
);

AFPERROR FPListExtAttribute(
    IN AfpBuffer& afpRequest,
       IN AfpBuffer& afpReply,
       IN LPSESSION afpSession
);

AFPERROR FPRemoveExtAttribute(
    IN AfpBuffer& afpRequest,
       IN AfpBuffer& afpReply,
       IN LPSESSION afpSession
);

uint64 GetStreamLen(
    IN PWCHAR pathname,
       IN PWCHAR streamName
);

AFPERROR GetStreams(
    IN WCHAR* pathname,
       IN std::vector<afp_attr_info>& streams
);
