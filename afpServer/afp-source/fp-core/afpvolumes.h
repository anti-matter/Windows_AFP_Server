// ReSharper disable CppInconsistentNaming
#pragma once

#include "afp_net_buffer.h"
#include "afp_session.h"


AFPERROR FPOpenVol(
    IN afp_net_buffer& afp_request,
    IN afp_net_buffer& afp_reply,
    IN dsi::dsi_connection* connection
);

AFPERROR FPCloseVol(
    IN afp_net_buffer& afp_request,
    IN afp_net_buffer& afp_reply,
    IN dsi::dsi_connection* connection
);

AFPERROR FPFlush(
    IN afp_net_buffer& afp_request,
    IN afp_net_buffer& afp_reply,
    IN dsi::dsi_connection* connection
);

AFPERROR FPGetVolParms(
    IN afp_net_buffer& afp_request,
    IN afp_net_buffer& afp_reply,
    IN dsi::dsi_connection* connection
);
