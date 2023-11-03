/*
 *	aboutdlg.cpp
 *
 *	Copyright © 2005, Michael J. Conrad. All Rights Reserved.
 *
 *	About dialog routines.
 */

#include "stdafx.h"
#include "aboutdlg.h"

extern HINSTANCE	ghInstance;
extern BOOL			gMainWindowVisible;
extern WCHAR		g_server_message[message_max_length+1];



/*
 *	AboutDlgProc()
 *
 *	Description
 *		Dialog message proc for our about box.
 *
 *	Returns
 *		None
 */

LRESULT CALLBACK AboutDlgProc(
        IN HWND 	hDlg,
        IN DWORD 	message,
        IN WPARAM 	wParam,
        IN LPARAM 	lParam
)
{
    UNREFERENCED_PARAMETER(lParam);

    switch (message)
    {
        case WM_INITDIALOG:
            break;

        case WM_COMMAND:
            switch(LOWORD(wParam))
            {
                case IDOK:
                    EndDialog(hDlg, LOWORD(wParam));
                    return( true );
            }

        default:
            break;
    }

    return( false );
}


/*
 *	AboutDialog()
 *
 *	Description
 *		Puts up the about dialog.
 *
 *	Returns
 *		None
 */

void AboutDialog(HWND hParent)
{
    //
    //No way we should get here without the main window, but
    //just in case...
    //
    if (gMainWindowVisible) 
    {
        DialogBox(
                ghInstance,
                MAKEINTRESOURCE(IDD_ABOUT),
                hParent,
                (DLGPROC)AboutDlgProc);
    }
}
