#pragma once

LRESULT CALLBACK about_dlg_proc(
		IN HWND 	h_dlg,
		IN DWORD 	message,
		IN WPARAM 	w_param,
		IN LPARAM 	l_param
);

void about_dialog(HWND h_parent);
