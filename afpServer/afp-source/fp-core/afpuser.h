// ReSharper disable CppInconsistentNaming
#pragma once

#include "afp_net_buffer.h"
#include "dsi/dsi_connection.h"


AFPERROR FPGetSessionToken(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	const dsi::dsi_connection* connection
);

AFPERROR FPDisconnectOldSession(
	IN afp_net_buffer& afp_request,
	IN const afp_net_buffer& afp_reply,
	const dsi::dsi_connection* connection
);

AFPERROR FPLogin(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	dsi::dsi_connection* connection
);

AFPERROR FPContLogin(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	dsi::dsi_connection* connection
);

AFPERROR FPChangePassword(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN LPSESSION dsiSession
);

AFPERROR FPLogout(
	IN const afp_net_buffer& afp_request,
	IN const afp_net_buffer& afp_reply,
	dsi::dsi_connection* connection
);

AFPERROR FPMapID(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	const dsi::dsi_connection* connection
);

AFPERROR FPGetSrvrMsg(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	const dsi::dsi_connection* connection
);
