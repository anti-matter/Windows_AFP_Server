#include "StdAfx.h"
#include "sendmsgdlg.h"

#include "resource.h"
#include "dsi/dsi_connections.h"
#include "afp.h"

extern HINSTANCE gh_instance;
extern BOOL g_main_window_visible;
extern WCHAR g_server_message[message_max_length + 1];
extern dsi::dsi_connections active_connections;


LRESULT CALLBACK send_message_dlg_proc(
    IN const HWND h_dlg,
    IN const DWORD message,
    IN const WPARAM w_param,
    IN const LPARAM l_param
)
{
    UNREFERENCED_PARAMETER(l_param);

    switch (message)
    {
    case WM_INITDIALOG:
        break;

    case WM_COMMAND:
        switch (LOWORD(w_param))
        {
        case IDOK:
            SendDlgItemMessage(h_dlg, IDC_MESSAGE, WM_GETTEXT,
                               sizeof(g_server_message), reinterpret_cast<LPARAM>(g_server_message));

            active_connections.broadcast_attention(dsi_attention_server_message);

        case IDCANCEL: // NOLINT(clang-diagnostic-implicit-fallthrough)
            EndDialog(h_dlg, LOWORD(w_param));
            return TRUE;

        default: 
            return FALSE;
        }

    default:
        break;
    }

    return FALSE;
}

void send_message_from_ui(const HWND h_parent)
{
    if (g_main_window_visible)
    {
        DialogBox(
            gh_instance,
            MAKEINTRESOURCE(IDD_SENDMESSAGE),
            h_parent,
            reinterpret_cast<DLGPROC>(send_message_dlg_proc));
    }
}
