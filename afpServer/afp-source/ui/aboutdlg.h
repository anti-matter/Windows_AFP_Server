/*
 *	aboutdlg.h
 *
 *	Copyright © 2005, Michael J. Conrad. All Rights Reserved.
 */

#include "resource.h"
#include "afp.h"
#include "afphelp.h"

LRESULT CALLBACK AboutDlgProc(
		IN HWND 	hDlg,
		IN DWORD 	message,
		IN WPARAM 	wParam,
		IN LPARAM 	lParam
);

void AboutDialog(HWND hParent);
