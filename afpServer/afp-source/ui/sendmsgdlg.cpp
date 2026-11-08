#include "StdAfx.h"
#include "sendmsgdlg.h"

#include <commctrl.h>

#include "resource.h"
#include "dsi/dsi_connections.h"
#include "afp.h"
#include "ui_theme.h"
#include "ui_controls.h"

extern HINSTANCE gh_instance;
extern BOOL g_main_window_visible;
extern WCHAR g_server_message[message_max_length + 1];
extern dsi::dsi_connections active_connections;

namespace
{
    int g_sent_clients = -1;

    void paint_dialog_background(const HWND h_dlg, HDC dc)
    {
        RECT rc{};
        GetClientRect(h_dlg, &rc);
        const HBRUSH brush = CreateSolidBrush(ui_theme::instance().colors().window_bg);
        FillRect(dc, &rc, brush);
        DeleteObject(brush);
    }
}

LRESULT CALLBACK send_message_dlg_proc(
    IN const HWND h_dlg,
    IN const DWORD message,
    IN const WPARAM w_param,
    IN const LPARAM l_param
)
{
    switch (message)
    {
    case WM_INITDIALOG:
        {
            g_sent_clients = -1;

            ui_theme::apply_titlebar_theme(h_dlg);

            make_fluent_button(GetDlgItem(h_dlg, IDOK), fluent_button_style::accent);
            fluent_button_set_surface(GetDlgItem(h_dlg, IDOK), ui_theme::instance().colors().window_bg);
            make_fluent_button(GetDlgItem(h_dlg, IDCANCEL), fluent_button_style::standard);
            fluent_button_set_surface(GetDlgItem(h_dlg, IDCANCEL), ui_theme::instance().colors().window_bg);

            const auto count = active_connections.count();
            WCHAR text[128]{};
            swprintf_s(text, L"%d Macintosh client%s currently connected.",
                       count, count == 1 ? L" is" : L"s are");
            SetDlgItemTextW(h_dlg, IDC_CLIENT_COUNT, text);
            EnableWindow(GetDlgItem(h_dlg, IDOK), count > 0);

            const int max_chars = static_cast<int>(sizeof(g_server_message) / sizeof(WCHAR)) - 1;
            SendDlgItemMessage(h_dlg, IDC_MESSAGE, EM_LIMITTEXT, max_chars, 0);

            if (count == 0)
                SetFocus(GetDlgItem(h_dlg, IDCANCEL));
            else
                SetFocus(GetDlgItem(h_dlg, IDC_MESSAGE));
            return FALSE;
        }

    case WM_ERASEBKGND:
        paint_dialog_background(h_dlg, reinterpret_cast<HDC>(w_param));
        return TRUE;

    case WM_CTLCOLORSTATIC:
        {
            const auto dc = reinterpret_cast<HDC>(w_param);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, ui_theme::instance().colors().text_primary);
            return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
        }

    case WM_DRAWITEM:
        draw_fluent_button(reinterpret_cast<const DRAWITEMSTRUCT*>(l_param));
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(w_param))
        {
        case IDOK:
            {
                const int max_chars = static_cast<int>(sizeof(g_server_message) / sizeof(WCHAR));
                SendDlgItemMessage(h_dlg, IDC_MESSAGE, WM_GETTEXT, max_chars,
                                   reinterpret_cast<LPARAM>(g_server_message));

                active_connections.broadcast_attention(dsi_attention_server_message);
                g_sent_clients = static_cast<int>(active_connections.count());
                EndDialog(h_dlg, IDOK);
                return TRUE;
            }

        case IDCANCEL:
            EndDialog(h_dlg, IDCANCEL);
            return TRUE;

        default:
            return FALSE;
        }

    default:
        break;
    }

    return FALSE;
}

int send_message_from_ui(const HWND h_parent)
{
    if (!g_main_window_visible)
        return -1;

    DialogBox(
        gh_instance,
        MAKEINTRESOURCE(IDD_SENDMESSAGE),
        h_parent,
        reinterpret_cast<DLGPROC>(send_message_dlg_proc));  // NOLINT(clang-diagnostic-cast-function-type-strict)

    return g_sent_clients;
}
