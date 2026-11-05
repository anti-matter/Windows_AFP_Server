// ReSharper disable CppInconsistentNaming
#pragma once

#include "afp.h"
#include "afp_session.h"
#include "afpfileio.h"
#include "afp_open_file.h"

#define BAD_LENGTH_64	MAXLONGLONG // 0xFFFFFFFFFFFFFFFF  // NOLINT(modernize-macro-to-enum)
#define BAD_LENGTH_32	0xFFFFFFFF  // NOLINT(modernize-macro-to-enum)

enum
{
    kXAttrNoFollow = 0x1,
    kXAttrCreate = 0x2,
    kXAttrReplace = 0x4
};

AFPERROR FPGetExtAttribute(
    IN afp_net_buffer& afpRequest,
    IN afp_net_buffer& afpReply,
    IN dsi::dsi_connection* connection
);

AFPERROR FPSetExtAttribute(
    IN afp_net_buffer& afpRequest,
    IN afp_net_buffer& afpReply,
    IN dsi::dsi_connection* connection
);

AFPERROR FPListExtAttribute(
    IN afp_net_buffer& afpRequest,
    IN afp_net_buffer& afpReply,
    IN dsi::dsi_connection* connection
);

AFPERROR FPRemoveExtAttribute(
    IN afp_net_buffer& afpRequest,
    IN afp_net_buffer& afpReply,
    IN dsi::dsi_connection* connection
);

LONGLONG GetStreamLen(
    IN const WCHAR* pathname,
    IN const WCHAR* streamName
);

AFPERROR GetStreams(
    IN const WCHAR* pathname,
    IN std::vector<afp_attr_info>& streams
);
