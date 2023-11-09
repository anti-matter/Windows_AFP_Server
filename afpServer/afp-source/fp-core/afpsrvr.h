// ReSharper disable CppInconsistentNaming
#pragma once

#include "afp_net_buffer.h"

namespace dsi
{
    class dsi_connection;
}

AFPERROR FPGetSrvrParms(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi::dsi_connection* connection
);

AFPERROR FPGetSrvrInfo(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi::dsi_connection* connection
);

AFPERROR FPZzzz(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi::dsi_connection* connection
);
