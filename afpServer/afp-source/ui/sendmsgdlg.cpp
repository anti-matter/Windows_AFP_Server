//===================================================================
// sendmsgdlg.cpp
//
// Routines handling the send message dialog box.
//===================================================================

#include "StdAfx.h"
#include "sendmsgdlg.h"

extern HINSTANCE	ghInstance;
extern BOOL			gMainWindowVisible;
extern WCHAR		gServerMessage[AFP_MSG_MAXLEN+1];



//---------------------------------------------------------------------
// SendMessageDlgProc()
//---------------------------------------------------------------------
// Handles messages to our dialog.

LRESULT CALLBACK SendMessageDlgProc(
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
                    SendDlgItemMessage(hDlg, IDC_MESSAGE, WM_GETTEXT,
                                            (WPARAM)sizeof(gServerMessage), (LPARAM)gServerMessage);
                    
                    //Let DSI do all the real work of sending the message
                    DSISendAttention(NULL, ATTN_SERVER_MESSAGE);
                case IDCANCEL:
                    EndDialog(hDlg, LOWORD(wParam));
                    return (TRUE);
            }

        default:
            break;
    }

    return( FALSE );
}


//---------------------------------------------------------------------
// SendMessageFromUI()
//---------------------------------------------------------------------
// Give the admin the opportunity to send a message as entered in a
// dialog box.
//

void SendMessageFromUI(HWND hParent)
{
    //
    //No way we should get here without the main window, but
    //just in case...
    //
    if (gMainWindowVisible) 
    {
        DialogBox(
                ghInstance,
                MAKEINTRESOURCE(IDD_SENDMESSAGE),
                hParent,
                (DLGPROC)SendMessageDlgProc);
    }
}
