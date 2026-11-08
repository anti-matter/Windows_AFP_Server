#include "stdafx.h"

#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
#include <commctrl.h>
#include <strsafe.h>
#include <gdiplus.h>

#include "afp.h"
#include "afpdebug.h"
#include "afpServer.h"
#include "resource.h"
#include "afpEvents.h"
#include "main_window.h"
#include "dsi/dsi_connections.h"
#include "dsi/dsi_init.h"

#define AFP_ICON_UID		IDI_TRAYICON
#define AFP_TOOL_TIP		TEXT("Personal AFP Server")

HINSTANCE gh_instance;

// Server-wide settings. Edited by the main window, read by the server.
// Defined in fp-core/afpuser.cpp.
extern BOOL g_allow_guests;
extern DWORD g_max_sessions;
extern BOOL g_force_encrypted_logon;

extern dsi::dsi_connections active_connections;

void initialize_tray_icon(
    IN const HWND h_window
)
{
    NOTIFYICONDATA nid;

    const auto h_icon = static_cast<HICON>(LoadImage(
        gh_instance,
        MAKEINTRESOURCE(IDI_TRAYICON),
        IMAGE_ICON,
        32,
        32,
        0));

    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = h_window;
    nid.uID = AFP_ICON_UID;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = AFPWM_NOTIFYICON;
    nid.hIcon = h_icon;

    [[maybe_unused]] const auto ignore = StringCbCopy(nid.szTip, sizeof(nid.szTip), AFP_TOOL_TIP);

    Shell_NotifyIcon(NIM_ADD, &nid);

    if (h_icon)
        DestroyIcon(h_icon);
}

void remove_tray_icon(
    IN HWND h_window
)
{
    NOTIFYICONDATA nid;

    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = h_window;
    nid.uID = AFP_ICON_UID;
    nid.uFlags = 0;

    Shell_NotifyIcon(NIM_DELETE, &nid);
}

// Finalizes the shutdown sequence. The confirmation dialog is shown by the
// main window before calling this.
void server_shutdown(
    IN HWND h_window
)
{
    dsi::dsi_shutdown();

    remove_tray_icon(h_window);

    EventWriteAFPServerEvt_ServerHasShutdown(L"");
    EventUnregisterAFPServer();

    PostQuitMessage(0);
}

int APIENTRY WinMain(const HINSTANCE hInstance,
                     const HINSTANCE hPrevInstance,
                     const LPSTR lpCmdLine,
                     const int nCmdShow)
{
    UNREFERENCED_PARAMETER(nCmdShow);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(hPrevInstance);

    MSG lp_msg;

    gh_instance = hInstance;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES | ICC_LINK_CLASS;
    InitCommonControlsEx(&icc);

    ULONG_PTR gdiplus_token = 0;
    Gdiplus::GdiplusStartupInput gdiplus_input;
    Gdiplus::GdiplusStartup(&gdiplus_token, &gdiplus_input, nullptr);

    if (const ULONG status = EventRegisterAFPServer(); status != ERROR_SUCCESS)
    {
        DBGWRITE(dbg_level_warning, L"EventRegisterSG_AFP_Server_Provider() failed: %d\n", status);
    }

    EventWriteAFPServerEvt_ServerStarting(L"");

    const HWND h_main_window = main_window_create(hInstance);

    dsi::dsi_init();

    initialize_tray_icon(h_main_window);

    // "/show" brings the management window up at startup (also handy for
    // scripted UI testing).
    if (const auto* cmd_line = GetCommandLineW();
        wcsstr(cmd_line, L"/show") != nullptr || wcsstr(cmd_line, L"--show") != nullptr)
    {
        main_window_show();
    }

    while (GetMessage(&lp_msg, nullptr, 0, 0))
    {
        if (main_window_accelerator() != nullptr &&
            TranslateAccelerator(main_window_handle(), main_window_accelerator(), &lp_msg))
        {
            continue;
        }

        // Tab / Enter / Esc navigation for the window's child controls.
        if (IsDialogMessage(main_window_handle(), &lp_msg))
        {
            continue;
        }

        TranslateMessage(&lp_msg);
        DispatchMessage(&lp_msg);
    }

    Gdiplus::GdiplusShutdown(gdiplus_token);
    CoUninitialize();

    return static_cast<int>(lp_msg.wParam);
}
