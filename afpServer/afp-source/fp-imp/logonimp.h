#pragma once

#include "afp_net_buffer.h"


AFPERROR afp_imp_user_logon(
	dsi::dsi_connection* connection,
	const afp_net_buffer&	afp_reply,
	WCHAR*		afp_password_w,
	WORD		cb_afp_password_w
);
