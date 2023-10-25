//===================================================================
// sendmsgdlg.h
//===================================================================

#include "resource.h"
#include "afp.h"
#include "afphelp.h"

LRESULT CALLBACK SendMessageDlgProc(
		IN HWND 	hDlg,
		IN DWORD 	message,
		IN WPARAM 	wParam,
		IN LPARAM 	lParam
);

void SendMessageFromUI(HWND hParent);
