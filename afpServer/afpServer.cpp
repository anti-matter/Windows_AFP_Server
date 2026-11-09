#include "stdafx.h"

#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <strsafe.h>
#include <gdiplus.h>
#include <thread>
#include <chrono>
#include <string>

#include "afp.h"
#include "afpdebug.h"
#include "afpServer.h"
#include "resource.h"
#include "afpEvents.h"
#include "main_window.h"
#include "ui_theme.h"
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

namespace
{
    // Explorer may store ExecutablePath under the Program Files known-folder
    // GUID instead of the plain drive path.
    constexpr const wchar_t* program_files_guid_prefix =
        L"{6D809377-6AF0-444B-8957-A3773F02200E}\\";

    // Expands Explorer's known-folder GUID form and 8.3 short names so two
    // references to the same executable compare equal.
    std::wstring canonicalize_exe_path(const wchar_t* path)
    {
        std::wstring expanded = path;

        if (_wcsnicmp(expanded.c_str(), program_files_guid_prefix,
                      wcslen(program_files_guid_prefix)) == 0)
        {
            wchar_t prog_files[MAX_PATH]{};
            if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES, nullptr, 0, prog_files)) &&
                prog_files[0] != L'\0')
            {
                expanded = prog_files;
                expanded += L'\\';
                expanded += path + wcslen(program_files_guid_prefix);
            }
        }

        wchar_t long_path[MAX_PATH]{};
        if (GetLongPathNameW(expanded.c_str(), long_path, ARRAYSIZE(long_path)) > 0)
            expanded = long_path;

        return expanded;
    }

    // Case-insensitive full match, falling back to a match of the shorter
    // path's tail inside the longer one. The tail must start on a path
    // boundary and cover at least one directory plus the file name, so a
    // same-named exe in a different tree (a dev build) is not a match.
    bool exe_paths_match(const std::wstring& left, const std::wstring& right)
    {
        if (_wcsicmp(left.c_str(), right.c_str()) == 0)
            return true;

        const std::wstring& shorter = left.size() <= right.size() ? left : right;
        const std::wstring& longer = left.size() <= right.size() ? right : left;

        if (shorter.empty() || shorter.find(L'\\') == std::wstring::npos)
            return false;

        const size_t offset = longer.size() - shorter.size();
        if (offset > 0 && longer[offset - 1] != L'\\')
            return false;

        return _wcsicmp(longer.c_str() + offset, shorter.c_str()) == 0;
    }

    // Sets IsPromoted=1 on our own icon's settings key, if it is present.
    // Returns true once the key has been found and marked promoted.
    bool promote_matching_icon_key(const std::wstring& our_path)
    {
        HKEY h_root = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\NotifyIconSettings",
                          0, KEY_READ, &h_root) != ERROR_SUCCESS)
        {
            return false;
        }

        bool promoted = false;
        wchar_t subkey_name[128];

        for (DWORD index = 0;; ++index)
        {
            DWORD name_chars = ARRAYSIZE(subkey_name);
            if (RegEnumKeyExW(h_root, index, subkey_name, &name_chars,
                              nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            {
                break;
            }

            HKEY h_subkey = nullptr;
            if (RegOpenKeyExW(h_root, subkey_name, 0, KEY_READ | KEY_SET_VALUE,
                              &h_subkey) != ERROR_SUCCESS)
            {
                continue;
            }

            DWORD uid = 0;
            DWORD value_size = sizeof(uid);
            DWORD value_type = 0;
            wchar_t exe_path[MAX_PATH + MAX_PATH]{};
            DWORD exe_size = sizeof(exe_path);

            if (RegQueryValueExW(h_subkey, L"UID", nullptr, &value_type,
                                 reinterpret_cast<LPBYTE>(&uid), &value_size) == ERROR_SUCCESS &&
                value_type == REG_DWORD && uid == AFP_ICON_UID &&
                RegQueryValueExW(h_subkey, L"ExecutablePath", nullptr, &value_type,
                                 reinterpret_cast<LPBYTE>(exe_path), &exe_size) == ERROR_SUCCESS &&
                value_type == REG_SZ &&
                exe_paths_match(our_path, canonicalize_exe_path(exe_path)))
            {
                const DWORD is_promoted = 1;
                promoted = RegSetValueExW(h_subkey, L"IsPromoted", 0, REG_DWORD,
                                          reinterpret_cast<const BYTE*>(&is_promoted),
                                          sizeof(is_promoted)) == ERROR_SUCCESS;
            }

            RegCloseKey(h_subkey);
        }

        RegCloseKey(h_root);
        return promoted;
    }

    // Windows 11 parks new tray icons in the hidden overflow. Explorer owns
    // HKCU\Control Panel\NotifyIconSettings and creates our icon's key there
    // (with its IconSnapshot) asynchronously after NIM_ADD; once the key
    // exists we mark it promoted so the icon shows in the visible area.
    // The key must never be pre-created: one that lacks Explorer's snapshot
    // blocks the icon's registration entirely.
    void promote_tray_icon_setting()
    {
        wchar_t module_path[MAX_PATH]{};
        const DWORD path_len = GetModuleFileNameW(nullptr, module_path, ARRAYSIZE(module_path));
        if (path_len == 0 || path_len >= ARRAYSIZE(module_path))
            return;

        const std::wstring our_path = canonicalize_exe_path(module_path);

        // The main window hides on close, so a detached helper thread is the
        // only thing that still runs while waiting for Explorer's key.
        std::thread([our_path]
        {
            for (int attempt = 0; attempt < 20; ++attempt)
            {
                if (promote_matching_icon_key(our_path))
                    return;
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        }).detach();
    }
}

namespace
{
    // Tray icon resource ownership.
    //
    // LoadImageW (without LR_SHARED) returns a fresh HICON copy that *we*
    // own, and the shell keeps using that handle to paint the icon - so it
    // must stay alive for as long as the icon is registered. We cache it and
    // destroy it only when the icon is removed, which both keeps the handle
    // valid and avoids reloading the bitmap on every show/hide cycle.
    HICON g_tray_icon = nullptr;

    // Set once NIM_ADD succeeds so re-initialization cannot register a second
    // copy of the same (hWnd, uID) identity, and so removal is a no-op when
    // there is nothing registered.
    bool g_tray_icon_registered = false;

    HICON load_tray_icon()
    {
        if (g_tray_icon == nullptr)
        {
            g_tray_icon = static_cast<HICON>(LoadImageW(
                gh_instance,
                MAKEINTRESOURCEW(IDI_TRAYICON),
                IMAGE_ICON,
                GetSystemMetrics(SM_CXSMICON),
                GetSystemMetrics(SM_CYSMICON),
                LR_DEFAULTCOLOR));
        }
        return g_tray_icon;
    }

    void release_tray_icon()
    {
        if (g_tray_icon != nullptr)
        {
            DestroyIcon(g_tray_icon);
            g_tray_icon = nullptr;
        }
    }
}

// Registers the notification-area icon. Returns without effect (and a
// diagnostic) if the window is not usable, the icon resource cannot be
// loaded, or the shell refuses the registration.
void initialize_tray_icon(
    IN const HWND h_window
)
{
    if (!IsWindow(h_window))
    {
        DBGWRITE(dbg_level_error, L"initialize_tray_icon: invalid owner window (%p)\n", h_window);
        return;
    }

    // Never register twice against the same identity.
    if (g_tray_icon_registered)
    {
        DBGWRITE(dbg_level_trace, L"initialize_tray_icon: already registered, skipping\n");
        return;
    }

    const HICON h_icon = load_tray_icon();
    if (h_icon == nullptr)
    {
        DBGWRITE(dbg_level_error, L"initialize_tray_icon: LoadImageW(IDI_TRAYICON) failed (%d)\n",
                 GetLastError());
        return;
    }

    // Zero-initialize: NOTIFYICONDATAW has fields we do not set (uVersion,
    // guidItem, dwState/dwStateMask, szInfo...). Leaving them as stack
    // garbage makes Shell_NotifyIconW silently reject the registration.
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = h_window;
    nid.uID = AFP_ICON_UID;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    // Version-0 callback semantics: the shell sends uCallbackMessage with the
    // mouse event in lParam. The window proc relies on this, so we deliberately
    // do not call NIM_SETVERSION to bump to v4.
    nid.uCallbackMessage = AFPWM_NOTIFYICON;
    nid.hIcon = h_icon;

    [[maybe_unused]] const auto ignore = StringCbCopyW(nid.szTip, sizeof(nid.szTip), AFP_TOOL_TIP);

    if (Shell_NotifyIconW(NIM_ADD, &nid))
    {
        g_tray_icon_registered = true;
        DBGWRITE(dbg_level_info, L"initialize_tray_icon: registered for window %p\n", h_window);
    }
    else
    {
        DBGWRITE(dbg_level_error, L"initialize_tray_icon: Shell_NotifyIconW(NIM_ADD) failed (%d)\n",
                 GetLastError());
        release_tray_icon();
    }
}

// Removes the notification-area icon, matching the (hWnd, uID) identity used
// at registration, and releases our cached icon handle.
void remove_tray_icon(
    IN HWND h_window
)
{
    if (g_tray_icon_registered)
    {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = h_window;
        nid.uID = AFP_ICON_UID;
        nid.uFlags = 0;

        if (!Shell_NotifyIconW(NIM_DELETE, &nid))
        {
            DBGWRITE(dbg_level_warning, L"remove_tray_icon: Shell_NotifyIconW(NIM_DELETE) failed (%d)\n",
                     GetLastError());
        }

        g_tray_icon_registered = false;
    }

    release_tray_icon();
}

// Explorer broadcasts "TaskbarCreated" whenever it starts. NotifyIcon
// registrations do not survive the shell exiting, so tray apps re-add their
// icon when they see the broadcast; without this the icon disappears until
// the app is restarted (e.g. after an Explorer crash or restart).
void tray_icon_on_taskbar_created(
    IN HWND h_window
)
{
    // The previous registration died with the old Explorer process - there is
    // nothing to NIM_DELETE, just forget it and register afresh. Our cached
    // HICON belongs to this process, so it stays valid and is reused as-is.
    g_tray_icon_registered = false;
    initialize_tray_icon(h_window);
    promote_tray_icon_setting();
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
    promote_tray_icon_setting();

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

    // The theme's GDI+ Font objects must die before GDI+ does: the Meyers
    // singleton destructor would otherwise run during static teardown, after
    // GdiplusShutdown, and freeing them then crashes inside GDI+.
    ui_theme::instance().shutdown();

    Gdiplus::GdiplusShutdown(gdiplus_token);
    CoUninitialize();

    return static_cast<int>(lp_msg.wParam);
}
