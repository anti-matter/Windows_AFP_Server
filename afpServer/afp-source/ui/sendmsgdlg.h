#pragma once

LRESULT CALLBACK send_message_dlg_proc(
		IN HWND 	h_dlg,
		IN DWORD 	message,
		IN WPARAM 	w_param,
		IN LPARAM 	l_param
);

void send_message_from_ui(HWND h_parent);
