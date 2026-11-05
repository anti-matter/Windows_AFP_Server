// ReSharper disable CppInconsistentNaming
#pragma once

#include "afp_net_buffer.h"
#include "dsi/dsi_connection.h"

AFPERROR FPOpenDT(
	afp_net_buffer& afp_request,
	afp_net_buffer& afp_reply,
	dsi::dsi_connection* connection
);

AFPERROR FPCloseDT(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi::dsi_connection* connection
);

AFPERROR FPAddIcon(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi::dsi_connection* connection
);

AFPERROR FPGetIcon(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi::dsi_connection* connection
);

AFPERROR FPGetIconInfo(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi::dsi_connection* connection
);

AFPERROR FPAddComment(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi::dsi_connection* connection
);

AFPERROR FPGetComment(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi::dsi_connection* connection
);

AFPERROR FPRemoveComment(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi::dsi_connection* connection
);

AFPERROR FPGetAPPL(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi::dsi_connection* connection
);

AFPERROR FPAddAPPL(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi::dsi_connection* connection
);

AFPERROR FPRemoveAPPL(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi::dsi_connection* connection
);
