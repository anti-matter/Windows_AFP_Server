// ReSharper disable CppInconsistentNaming
#pragma once

#include "afp_net_buffer.h"
#include "afp_session.h"


AFPERROR FPFlushFork(
    IN const afp_net_buffer& afp_request,
    IN const afp_net_buffer& afp_reply,
    IN const dsi::dsi_connection* connection
);

AFPERROR FPCloseFork(
    IN afp_net_buffer& afp_request,
    IN const afp_net_buffer& afp_reply,
    IN const dsi::dsi_connection* connection
);

AFPERROR FPOpenFork(
    IN afp_net_buffer& afp_request,
    IN afp_net_buffer& afp_reply,
    IN const dsi::dsi_connection* connection
);

AFPERROR FPSetForkParms(
    IN afp_net_buffer& afp_request,
    IN const afp_net_buffer& afp_reply,
    IN const dsi::dsi_connection* connection
);

AFPERROR FPGetForkParms(
    IN afp_net_buffer& afp_request,
    IN afp_net_buffer& afp_reply,
    IN const dsi::dsi_connection* connection
);

AFPERROR FPCreateFile(
    IN afp_net_buffer& afp_request,
    IN const afp_net_buffer& afp_reply,
    IN const dsi::dsi_connection* connection
);

AFPERROR FPByteRangeLock(
    IN afp_net_buffer& afpRequest,
    IN afp_net_buffer& afpReply,
    IN dsi::dsi_connection* connection
);

AFPERROR FPRead(
    IN afp_net_buffer& afpRequest,
    IN afp_net_buffer& afpReply,
    IN dsi::dsi_connection* connection
);

AFPERROR FPWrite(
    IN afp_net_buffer& afpRequest,
    IN afp_net_buffer& afpReply,
    IN dsi::dsi_connection* connection
);
