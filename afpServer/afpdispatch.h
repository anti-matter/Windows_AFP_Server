#pragma once

#include "afp.h"
#include "afp_net_buffer.h"
#include "afp_session.h"
#include "dsi/dsi_connection.h"

AFPERROR fp_unimplemented(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi::dsi_connection* connection
);

AFPERROR fp_dispatch_command(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi::dsi_connection* connection
);
