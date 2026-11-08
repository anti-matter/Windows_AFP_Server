#include "stdafx.h"
#include "aboutdlg.h"

#include "afphelp.h"
#include "resource.h"
#include "version.h"

extern HINSTANCE gh_instance;
extern BOOL g_main_window_visible;
extern WCHAR g_server_message[message_max_length + 1];


LRESULT CALLBACK about_dlg_proc(
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
        {
            const std::wstring version = string_to_wide_string(VER_FILE_SHORT_VERSION_STR, CP_UTF8);
            SendDlgItemMessage(h_dlg, IDC_VERSION_TEXT, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(version.c_str()));
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(w_param))
        {
        case IDOK:
            EndDialog(h_dlg, LOWORD(w_param));
            return (true);
        default:
            break;
        }
        break;

    default:
        break;
    }

    return false;
}

void about_dialog(const HWND h_parent)
{
    if (g_main_window_visible)
    {
        DialogBox(
            gh_instance,
            MAKEINTRESOURCE(IDD_ABOUT),
            h_parent,
            reinterpret_cast<DLGPROC>(about_dlg_proc));
    }
}
