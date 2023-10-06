#pragma once

#include "afp.h"
#include "afp_net_buffer.h"

AFPERROR fp_unimplemented(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN LPSESSION dsi_session
);

AFPERROR fp_dispatch_command(
	IN LPPER_IO_DATA received_data,
	IN LPSEND_QUEUE reply_queue
);
