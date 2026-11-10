#include "stdafx.h"
#include "main_window.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <strsafe.h>
#include <shellapi.h>
#include <vector>
#include <string>
#include <iterator>

#include "afp.h"
#include "afpServer.h"
#include "afp_registry.h"
#include "afp_volume.h"
#include "afp_volumes.h"
#include "volumes.h"
#include "afphelp.h"
#include "resource.h"
#include "version.h"
#include "line-conversion/line_conversion.h"
#include "dsi/dsi_connections.h"
#include "dsi/dsi_init.h"
#include "ui_theme.h"
#include "ui_controls.h"
#include "addshare.h"
#include "sendmsgdlg.h"
#include "aboutdlg.h"

// From afpServer.cpp
extern HINSTANCE gh_instance;
extern BOOL g_allow_guests;
extern DWORD g_max_sessions;
extern BOOL g_force_encrypted_logon;
extern dsi::dsi_connections active_connections;
extern std::unique_ptr<afp_volumes> g_volumes;
extern void server_shutdown(HWND hwnd);
extern void remove_tray_icon(HWND hwnd);
extern void tray_icon_on_taskbar_created(HWND hwnd);

// Maintained here; the secondary dialogs check it (they are only
// reachable from the visible main window).
BOOL g_main_window_visible = FALSE;

namespace
{
    constexpr auto window_class_name = L"MacFileMainWindow";

    // ---- layout metrics (96-DPI design units) ----------------------------
    constexpr int margin = 20;      // window side margins
    constexpr int header_h = 80;    // app header band
    constexpr int gap = 12;         // vertical gap between cards
    constexpr int card_pad = 16;    // inner card padding
    constexpr int card_radius = 8;
    constexpr int footer_h = 56;    // command bar
    constexpr int footer_gap = 16;  // space above the command bar
    constexpr int row_h = 36;       // volume list row

    constexpr int shares_head_h = 52;   // "Shared Volumes" + Add button
    constexpr int shares_cols_h = 24;   // column header strip
    constexpr int shares_bottom_h = 66; // divider + RO row + bottom padding
    constexpr int two_card_h = 292;     // Security / Login row height
    constexpr int adv_collapsed_h = 56;
    constexpr int adv_expanded_h = 190;

    // commctrl.h defines LVM_FIRST but not the (documented) item-height
    // message for report-view list controls.

    constexpr UINT timer_status = 1;
    constexpr UINT timer_toast = 2;

    constexpr int col_volume_w = 200;
    constexpr int col_access_w = 110;

    // UI-only registry values (persist window rect + advanced section).
    constexpr auto reg_value_window_rect = L"UiWindowRect";
    constexpr auto reg_value_advanced = L"UiAdvancedOpen";

    struct volume_entry
    {
        std::wstring name;
        std::wstring path;
        bool read_only = false;
        bool indexing = false;
    };

    struct main_state
    {
        HWND hwnd = nullptr;
        HACCEL accel = nullptr;

        // child controls
        HWND add_btn = nullptr;
        HWND list = nullptr;
        HWND ro_toggle = nullptr;
        HWND remove_btn = nullptr;
        HWND guest_toggle = nullptr;
        HWND enc_toggle = nullptr;
        HWND max_edit = nullptr;
        HWND msg_edit = nullptr;
        HWND adv_btn = nullptr;
        HWND ext_edit = nullptr;
        HWND about_btn = nullptr;
        HWND send_btn = nullptr;
        HWND shutdown_btn = nullptr;
        HWND savehide_btn = nullptr;
        HWND tooltip = nullptr;

        std::vector<volume_entry> volumes;
        int hot_row = -1;

        bool advanced_open = false;
        UINT dpi = 96;
        int focused_edit_id = 0;

        // cached layout
        RECT rc_shares{};
        RECT rc_security{};
        RECT rc_login{};
        RECT rc_adv{};
        RECT rc_footer{};
        RECT rc_list{};
        RECT rc_pill{};
        RECT rc_msg_counter{};
        RECT rc_ext_counter{};
        RECT rc_max_edit_frame{};
        RECT rc_msg_edit_frame{};
        RECT rc_ext_edit_frame{};

        std::wstring pill_text;
        int last_client_count = -1;

        std::wstring toast_text;
        DWORD toast_expire = 0;
        RECT rc_toast{};

        // painting buffers
        HDC mem_dc = nullptr;
        HBITMAP mem_bm = nullptr;
        SIZE buf_size{};

        HBRUSH edit_brush = nullptr;
    };

    main_state g;

    // ---------------------------------------------------------------- utils

    COLORREF mix_ref(const COLORREF a, const COLORREF b, const double t)
    {
        const int r = static_cast<int>(GetRValue(a) * (1.0 - t) + GetRValue(b) * t);
        const int gg = static_cast<int>(GetGValue(a) * (1.0 - t) + GetGValue(b) * t);
        const int bl = static_cast<int>(GetBValue(a) * (1.0 - t) + GetBValue(b) * t);
        return RGB(r, gg, bl);
    }

    Gdiplus::Color gdi_color(const COLORREF c)
    {
        return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c));
    }

    Gdiplus::RectF rectf(const RECT& rc)
    {
        return { static_cast<REAL>(rc.left), static_cast<REAL>(rc.top),
                 static_cast<REAL>(rc.right - rc.left), static_cast<REAL>(rc.bottom - rc.top) };
    }

    std::wstring format_count_text(const int count)
    {
        WCHAR buf[96]{};
        swprintf_s(buf, L"Running \u00b7 %d client%s", count, count == 1 ? L"" : L"s");
        return buf;
    }

    // ------------------------------------------------------------ toast/notify

    void show_toast(const std::wstring& text)
    {
        g.toast_text = text;
        g.toast_expire = GetTickCount() + 3200;
        SetTimer(g.hwnd, timer_toast, 250, nullptr);
        if (!IsRectEmpty(&g.rc_toast))
            InvalidateRect(g.hwnd, &g.rc_toast, FALSE);
        else
            InvalidateRect(g.hwnd, nullptr, FALSE);
    }

    void hide_toast()
    {
        const bool had = g.toast_expire != 0;
        g.toast_expire = 0;
        KillTimer(g.hwnd, timer_toast);
        if (had)
            InvalidateRect(g.hwnd, &g.rc_toast, FALSE);
    }

    // --------------------------------------------------------- registry bits

    void save_window_state()
    {
        if (g.hwnd == nullptr)
            return;

        RECT rc{};
        GetWindowRect(g.hwnd, &rc);

        HKEY h_key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, SUBKEY_BASEPARAMS, 0, nullptr, 0,
                            KEY_WRITE, nullptr, &h_key, nullptr) != ERROR_SUCCESS)
            return;

        RegSetValueExW(h_key, reg_value_window_rect, 0, REG_BINARY,
                       reinterpret_cast<const BYTE*>(&rc), sizeof(rc));
        const DWORD adv = g.advanced_open ? 1 : 0;
        RegSetValueExW(h_key, reg_value_advanced, 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&adv), sizeof(adv));
        RegCloseKey(h_key);
    }

    bool load_window_state(RECT& rc)
    {
        HKEY h_key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, SUBKEY_BASEPARAMS, 0, KEY_READ, &h_key) != ERROR_SUCCESS)
            return false;

        bool ok = false;
        DWORD type = 0;
        DWORD cb = sizeof(rc);
        if (RegQueryValueExW(h_key, reg_value_window_rect, nullptr, &type,
                             reinterpret_cast<LPBYTE>(&rc), &cb) == ERROR_SUCCESS &&
            type == REG_BINARY && cb == sizeof(rc))
        {
            const HMONITOR mon = MonitorFromRect(&rc, MONITOR_DEFAULTTONULL);
            ok = mon != nullptr && rc.right - rc.left >= ui_scale(680) && rc.bottom - rc.top >= ui_scale(400);
        }

        DWORD adv = 0;
        cb = sizeof(adv);
        if (RegQueryValueExW(h_key, reg_value_advanced, nullptr, &type,
                             reinterpret_cast<LPBYTE>(&adv), &cb) == ERROR_SUCCESS &&
            type == REG_DWORD)
        {
            g.advanced_open = adv != 0;
        }

        RegCloseKey(h_key);
        return ok;
    }

    // --------------------------------------------------------- volume model

    void relayout(); // defined below with the layout helpers

    int selected_index()
    {
        return ListView_GetNextItem(g.list, -1, LVNI_SELECTED);
    }

    void load_volumes(std::vector<volume_entry>& out)
    {
        out.clear();

        afp_registry registry;
        if (!registry.open_volume_list_reg_key())
            return;

        WCHAR name[64]{};
        WCHAR path[MAX_PATH]{};
        afp_clear_text_password password{};
        DWORD flags = 0;

        while (registry.next_volume(
            name, sizeof(name),
            path, sizeof(path),
            reinterpret_cast<PCHAR>(password), sizeof(password),
            &flags))
        {
            volume_entry entry;
            entry.name = name;
            entry.path = path;
            entry.read_only = (flags & flag_volume_readonly) != 0;

            const auto afp_volume = g_volumes->find(entry.name);
            if (afp_volume != nullptr)
                entry.indexing = (afp_volume->get_internal_flags() & flag_volume_indexing_complete) == 0;

            out.push_back(std::move(entry));
        }
    }

    void update_selection_ui()
    {
        const int sel = selected_index();
        const bool has_sel = sel >= 0 && sel < static_cast<int>(g.volumes.size());
        const bool has_any = !g.volumes.empty();

        EnableWindow(g.remove_btn, has_sel);
        EnableWindow(g.ro_toggle, has_sel);

        if (has_sel)
            toggle_set_checked(g.ro_toggle, g.volumes[sel].read_only);

        // Repaint the shares card (read-only label color + empty state).
        if (!IsRectEmpty(&g.rc_shares))
            InvalidateRect(g.hwnd, &g.rc_shares, FALSE);

        if (!has_any && IsWindowVisible(g.list))
            ShowWindow(g.list, SW_HIDE); // empty state is painted instead
        else if (has_any && !IsWindowVisible(g.list))
            ShowWindow(g.list, SW_SHOW);
    }

    void rebuild_volume_list(const std::wstring& preserve_name)
    {
        load_volumes(g.volumes);

        ListView_DeleteAllItems(g.list);

        int select_idx = -1;
        for (size_t i = 0; i < g.volumes.size(); ++i)
        {
            const auto& v = g.volumes[i];

            LVITEMW item{};
            item.mask = LVIF_TEXT | LVIF_PARAM;
            item.iItem = static_cast<int>(i);
            item.pszText = const_cast<LPWSTR>(v.name.c_str());
            item.lParam = static_cast<LPARAM>(i);
            ListView_InsertItem(g.list, &item);

            ListView_SetItemText(g.list, static_cast<int>(i), 1, const_cast<LPWSTR>(v.path.c_str()));
            ListView_SetItemText(g.list, static_cast<int>(i), 2,
                                 const_cast<LPWSTR>(v.read_only ? L"Read Only" : L"Read/Write"));

            if (preserve_name == v.name)
                select_idx = static_cast<int>(i);
        }

        // The list control is height-fitted to its row count in relayout();
        // without re-running it here, rows added after the initial layout
        // would be inserted into a too-short (possibly zero-height) control
        // and stay invisible until the window is resized.
        relayout();

        if (select_idx >= 0)
        {
            ListView_SetItemState(g.list, select_idx, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        }
        else if (!g.volumes.empty())
        {
            ListView_SetItemState(g.list, 0, LVIS_SELECTED, LVIS_SELECTED);
        }

        update_selection_ui();
        ListView_RedrawItems(g.list, 0, static_cast<int>(g.volumes.size()));
    }

    void rebuild_volume_list()
    {
        const int sel = selected_index();
        std::wstring preserve;
        if (sel >= 0 && sel < static_cast<int>(g.volumes.size()))
            preserve = g.volumes[sel].name;
        rebuild_volume_list(preserve);
    }

    // ------------------------------------------------------------- settings

    void set_globals_from_controls()
    {
        g_allow_guests = toggle_get_checked(g.guest_toggle) ? TRUE : FALSE;
        g_force_encrypted_logon = toggle_get_checked(g.enc_toggle) ? TRUE : FALSE;
    }

    bool save_login_message()
    {
        WCHAR message[message_max_length + 2]{};
        GetWindowTextW(g.msg_edit, message, message_max_length + 1);
        afp_registry registry;
        return registry.save_logon_message(message) == NO_ERROR;
    }

    bool save_misc_settings()
    {
        afp_registry registry;

        set_globals_from_controls();
        registry.set_allow_guests_setting(g_allow_guests);
        registry.set_force_encrypted_logon(g_force_encrypted_logon);

        BOOL translated = FALSE;
        const DWORD max_sessions = GetDlgItemInt(g.hwnd, IDC_MAX_CONNECTIONS, &translated, FALSE);
        if (translated)
        {
            g_max_sessions = max_sessions;
            registry.set_max_sessions(max_sessions);
        }

        WCHAR extensions[max_extension_string_size + 2]{};
        GetWindowTextW(g.ext_edit, extensions, max_extension_string_size + 1);
        registry.set_extensions_for_conversion(extensions);

        return translated != FALSE;
    }

    void load_settings_into_controls()
    {
        BOOL allow_guest = TRUE;
        BOOL encrypt = FALSE;
        DWORD max_sessions = 10;
        afp_registry registry;

        if (registry.get_allow_guests_setting(&allow_guest) != NO_ERROR)
        {
            allow_guest = FALSE;
            registry.set_allow_guests_setting(allow_guest);
        }
        if (registry.get_force_encrypted_logon(&encrypt) != NO_ERROR)
        {
            encrypt = FALSE;
            registry.set_force_encrypted_logon(encrypt);
        }
        if (registry.get_max_sessions(&max_sessions) != NO_ERROR)
        {
            max_sessions = 10;
            registry.set_max_sessions(max_sessions);
        }

        g_allow_guests = allow_guest;
        g_force_encrypted_logon = encrypt;
        g_max_sessions = max_sessions;

        toggle_set_checked(g.guest_toggle, allow_guest != FALSE);
        toggle_set_checked(g.enc_toggle, encrypt != FALSE);
        SetDlgItemInt(g.hwnd, IDC_MAX_CONNECTIONS, max_sessions, FALSE);

        SendMessageW(g.msg_edit, EM_LIMITTEXT, message_max_length, 0);
        WCHAR logon_message[message_max_length + 2]{};
        if (registry.get_logon_message(logon_message) == NO_ERROR)
            SetWindowTextW(g.msg_edit, logon_message);

        SendMessageW(g.ext_edit, EM_LIMITTEXT, max_extension_string_size, 0);
        std::wstring ext_string;
        if (registry.get_extensions_for_line_conversion(ext_string) == afperr_registry_no_extensions_for_conversion)
        {
            ext_string = line_conversion_default_extensions;
            registry.set_extensions_for_conversion(ext_string);
        }
        SetWindowTextW(g.ext_edit, ext_string.c_str());
    }

    // ---------------------------------------------------------- dialogs/tasks

    int task_dialog_confirm(
        const HWND owner,
        PCWSTR title,
        PCWSTR heading,
        PCWSTR content,
        PCWSTR command_label)
    {
        TASKDIALOGCONFIG config{};
        config.cbSize = sizeof(config);
        config.hwndParent = owner;
        config.hInstance = gh_instance;
        config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
        config.pszWindowTitle = title;
        config.pszMainInstruction = heading;
        config.pszContent = content;
        config.dwCommonButtons = TDCBF_CANCEL_BUTTON;

        const TASKDIALOG_BUTTON custom{ 100, command_label };
        config.pButtons = &custom;
        config.cButtons = 1;
        config.nDefaultButton = IDCANCEL;
        config.pszMainIcon = TD_WARNING_ICON;

        int pressed = 0;
        if (FAILED(TaskDialogIndirect(&config, &pressed, nullptr, nullptr)))
        {
            // TaskDialog should always be available (comctl32 v6); fall back.
            pressed = MessageBoxW(owner, content, title, MB_OKCANCEL) == IDOK ? 100 : IDCANCEL;
        }
        return pressed;
    }

    void task_dialog_error(const HWND owner, PCWSTR heading, PCWSTR content)
    {
        TASKDIALOGCONFIG config{};
        config.cbSize = sizeof(config);
        config.hwndParent = owner;
        config.hInstance = gh_instance;
        config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
        config.pszWindowTitle = L"MacFile";
        config.pszMainInstruction = heading;
        config.pszContent = content;
        config.dwCommonButtons = TDCBF_OK_BUTTON;
        config.pszMainIcon = TD_ERROR_ICON;

        if (FAILED(TaskDialogIndirect(&config, nullptr, nullptr, nullptr)))
            MessageBoxW(owner, content, heading, MB_OK);
    }

    void do_add_share()
    {
        const auto outcome = prompt_for_new_share(g.hwnd);

        switch (outcome.result)
        {
        case add_share_result::shared:
            rebuild_volume_list(outcome.name);
            show_toast(L"Shared \u201c" + outcome.name + L"\u201d");
            break;

        case add_share_result::invalid_folder:
            task_dialog_error(g.hwnd,
                              L"That folder can't be shared",
                              L"Only folders on local disks can be shared to Macintosh clients. "
                              L"Network locations and drive roots are not supported.");
            break;

        case add_share_result::share_failed:
            {
                WCHAR content[512]{};
                swprintf_s(content,
                           L"The folder could not be shared:\n\n%s\n\nMake sure the folder is on a local disk and hasn't already been shared.",
                           outcome.path.c_str());
                task_dialog_error(g.hwnd, L"Sharing failed", content);
            }
            break;

        case add_share_result::cancelled:
        default:
            break;
        }
    }

    void do_remove_volume()
    {
        const int sel = selected_index();
        if (sel < 0 || sel >= static_cast<int>(g.volumes.size()))
            return;

        const auto entry = g.volumes[sel];

        WCHAR content[600]{};
        swprintf_s(content,
                   L"%s\n\nMacintosh clients using this volume will lose access to it.",
                   entry.path.c_str());

        WCHAR heading[300]{};
        swprintf_s(heading, L"Stop sharing \u201c%s\u201d?", entry.name.c_str());

        if (task_dialog_confirm(g.hwnd, L"Stop Sharing", heading, content, L"Stop Sharing") != 100)
            return;

        const DWORD error = AFPRemoveVolumeFromUI(const_cast<LPWSTR>(entry.name.c_str()));
        if (error != NO_ERROR)
        {
            task_dialog_error(g.hwnd,
                              L"Couldn't remove the volume",
                              L"The volume could not be removed. It may be in use by a connected client.");
            return;
        }

        rebuild_volume_list(L"");
        show_toast(L"Stopped sharing \u201c" + entry.name + L"\u201d");
    }

    void do_read_only_toggle()
    {
        const int sel = selected_index();
        if (sel < 0 || sel >= static_cast<int>(g.volumes.size()))
            return;

        auto& entry = g.volumes[sel];
        const bool make_read_only = toggle_get_checked(g.ro_toggle);

        afp_registry registry;
        const auto afp_volume = g_volumes->find(entry.name);
        if (afp_volume == nullptr)
        {
            // The volume isn't mounted; revert the switch so it reflects reality.
            toggle_set_checked(g.ro_toggle, entry.read_only);
            return;
        }

        // A volume created by an older server may have no Flags value in the
        // registry; fall back to the live in-memory flags. set_volume_flags()
        // creates the value, so the next toggle finds it.
        DWORD vol_flags = afp_volume->get_internal_flags();
        if (registry.get_volume_flags(const_cast<LPWSTR>(entry.name.c_str()), &vol_flags) != ERROR_SUCCESS)
            vol_flags = afp_volume->get_internal_flags();

        if (make_read_only)
        {
            vol_flags |= flag_volume_readonly;
            afp_volume->set_internal_flags(flag_volume_readonly);
        }
        else
        {
            vol_flags &= ~flag_volume_readonly;
            afp_volume->clear_internal_flags(flag_volume_readonly);
        }
        registry.set_volume_flags(entry.name.c_str(), vol_flags);

        entry.read_only = make_read_only;
        ListView_SetItemText(g.list, sel, 2,
                             const_cast<LPWSTR>(entry.read_only ? L"Read Only" : L"Read/Write"));
        ListView_RedrawItems(g.list, sel, sel);

        show_toast(make_read_only
                       ? L"\u201c" + entry.name + L"\u201d is now read-only for Mac clients"
                       : L"\u201c" + entry.name + L"\u201d is now read/write for Mac clients");
    }

    void do_shutdown()
    {
        if (task_dialog_confirm(
                g.hwnd,
                L"Shut Down MacFile",
                L"Stop the MacFile server?",
                L"Connected Macintosh clients will be notified, then file sharing will stop. "
                L"Any unfinished file transfers may be lost.",
                L"Shut Down") != 100)
        {
            return;
        }

        save_login_message();
        save_misc_settings();
        save_window_state();

        server_shutdown(g.hwnd);
    }

    void do_save()
    {
        save_login_message();
        const bool ok = save_misc_settings();
        show_toast(ok ? L"Configuration saved" : L"Maximum connections must be a number");
    }

    void do_send_message()
    {
        const int clients = send_message_from_ui(g.hwnd);
        if (clients > 0)
        {
            WCHAR toast[64]{};
            swprintf_s(toast, L"Message sent to %d client%s", clients, clients == 1 ? L"" : L"s");
            show_toast(toast);
        }
    }

    void hide_window()
    {
        do_save();
        ShowWindow(g.hwnd, SW_HIDE);
        g_main_window_visible = FALSE;
    }

    // ------------------------------------------------------------- tray menu

    void show_tray_menu(const POINT pt)
    {
        HMENU menu = CreatePopupMenu();
        if (menu == nullptr)
            return;

        AppendMenuW(menu, MF_STRING, IDM_TRAY_OPEN, L"Open MacFile");
        AppendMenuW(menu, MF_STRING, IDM_TRAY_SEND, L"Send Message\u2026");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, IDM_TRAY_SHUTDOWN, L"Shut Down Server");

        SetForegroundWindow(g.hwnd);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, g.hwnd, nullptr);
        PostMessageW(g.hwnd, WM_NULL, 0, 0);
        DestroyMenu(menu);
    }

    void update_tray_tooltip(const int clients)
    {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = g.hwnd;
        nid.uID = IDI_TRAYICON;
        nid.uFlags = NIF_TIP;
        swprintf_s(nid.szTip, L"MacFile \u2014 Running \u00b7 %d client%s",
                   clients, clients == 1 ? L"" : L"s");
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }

    // -------------------------------------------------------------- painting

    void ensure_buffer(const HDC ref_dc, const RECT& client)
    {
        const LONG w = client.right - client.left;
        const LONG h = client.bottom - client.top;

        if (g.mem_dc != nullptr && g.buf_size.cx == w && g.buf_size.cy == h)
            return;

        if (g.mem_dc != nullptr)
        {
            DeleteDC(g.mem_dc);
            DeleteObject(g.mem_bm);
            g.mem_dc = nullptr;
            g.mem_bm = nullptr;
        }

        g.mem_dc = CreateCompatibleDC(ref_dc);
        if (g.mem_dc == nullptr)
            return;

        g.mem_bm = CreateCompatibleBitmap(ref_dc, w, h);
        if (g.mem_bm == nullptr)
        {
            DeleteDC(g.mem_dc);
            g.mem_dc = nullptr;
            return;
        }
        SelectObject(g.mem_dc, g.mem_bm);
        // GDI+ needs a transparent background mode on the target DC or smooth
        // (anti-aliased) text falls back to jagged output.
        SetBkMode(g.mem_dc, TRANSPARENT);
        g.buf_size = { w, h };
    }

    void paint_header(HDC dc, Gdiplus::Graphics& gfx, const RECT& client)
    {
        const auto& p = ui_theme::instance().colors();

        // App icon.
        const int icon_size = ui_scale(34);
        const int icon_x = margin;
        const int icon_y = (ui_scale(header_h) - icon_size) / 2;
        if (const HICON h_icon = LoadIconW(gh_instance, MAKEINTRESOURCE(IDI_TRAYICON)))
        {
            DrawIconEx(dc, icon_x, icon_y, h_icon, icon_size, icon_size, 0, nullptr, DI_NORMAL);
        }

        const int text_x = icon_x + icon_size + ui_scale(13);

        {
            Gdiplus::RectF rc(static_cast<REAL>(text_x), static_cast<REAL>(ui_scale(14)),
                              static_cast<REAL>(client.right - text_x - ui_scale(230)),
                              static_cast<REAL>(ui_scale(28)));
            ui_draw::text(gfx, ui_theme::instance().font_title(),
                          L"MacFile", rc, DT_LEFT, DT_TOP, p.text_primary);
        }
        {
            Gdiplus::RectF rc(static_cast<REAL>(text_x), static_cast<REAL>(ui_scale(44)),
                              static_cast<REAL>(client.right - text_x - ui_scale(230)),
                              static_cast<REAL>(ui_scale(20)));
            ui_draw::text(gfx, ui_theme::instance().font_caption(),
                          L"Apple Filing Protocol \u00b7 Windows", rc, DT_LEFT, DT_TOP, p.text_secondary);
        }

        // Status pill.
        if (!g.pill_text.empty())
        {
            const Gdiplus::RectF text_box = ui_draw::measure(gfx, ui_theme::instance().font_caption(), g.pill_text);
            const REAL pad_x = static_cast<REAL>(ui_scale(11));
            const REAL dot_d = static_cast<REAL>(ui_scale(7));
            const REAL pill_w = text_box.Width + pad_x * 2 + dot_d + ui_scale(6);
            const REAL pill_h = static_cast<REAL>(ui_scale(28));
            const REAL pill_x = static_cast<REAL>(client.right - margin) - pill_w;
            const REAL pill_y = (static_cast<REAL>(ui_scale(header_h)) - pill_h) * 0.5f;

            const Gdiplus::RectF pill(pill_x, pill_y, pill_w, pill_h);
            ui_draw::round_rect(gfx, pill, pill_h * 0.5f, gdi_color(p.pill_ok_bg), nullptr);

            Gdiplus::SolidBrush dot(gdi_color(p.pill_ok_text));
            gfx.FillEllipse(&dot, pill_x + pad_x, pill_y + (pill_h - dot_d) * 0.5f, dot_d, dot_d);

            Gdiplus::RectF txt(pill_x + pad_x + dot_d + ui_scale(6), pill_y,
                               pill_w - pad_x * 2 - dot_d - ui_scale(6), pill_h);
            ui_draw::text(gfx, ui_theme::instance().font_caption(), g.pill_text, txt,
                          DT_LEFT, DT_VCENTER, p.pill_ok_text);

            g.rc_pill.left = static_cast<LONG>(pill_x) - ui_scale(4);
            g.rc_pill.top = static_cast<LONG>(pill_y) - ui_scale(4);
            g.rc_pill.right = static_cast<LONG>(pill_x + pill_w) + ui_scale(4);
            g.rc_pill.bottom = static_cast<LONG>(pill_y + pill_h) + ui_scale(4);
        }
    }

    void paint_shares_card(Gdiplus::Graphics& gfx)
    {
        const auto& p = ui_theme::instance().colors();
        const RECT& card = g.rc_shares;

        ui_draw::round_rect(gfx, rectf(card), static_cast<REAL>(ui_scale(card_radius)),
                            gdi_color(p.card_bg), gdi_color(p.card_stroke));

        // Section header.
        {
            Gdiplus::RectF rc(static_cast<REAL>(card.left + card_pad),
                              static_cast<REAL>(card.top + (ui_scale(shares_head_h) - ui_scale(26)) / 2),
                              static_cast<REAL>(ui_scale(360)), static_cast<REAL>(ui_scale(26)));
            ui_draw::text(gfx, ui_theme::instance().font_section(),
                          L"Shared Volumes", rc, DT_LEFT, DT_VCENTER, p.text_primary);
        }

        // Column headers.
        {
            const int y = card.top + ui_scale(shares_head_h);
            const int list_x = card.left + card_pad;
            const int list_w = card.right - card_pad - list_x;
            const int col1 = ui_scale(col_volume_w);
            const int col3 = ui_scale(col_access_w);

            Gdiplus::RectF c1(static_cast<REAL>(list_x + ui_scale(24)), static_cast<REAL>(y),
                              static_cast<REAL>(col1 - ui_scale(24)), static_cast<REAL>(ui_scale(shares_cols_h)));
            ui_draw::text(gfx, ui_theme::instance().font_caption(), L"Volume",
                          c1, DT_LEFT, DT_VCENTER, p.text_tertiary);

            Gdiplus::RectF c2(static_cast<REAL>(list_x + col1), static_cast<REAL>(y),
                              static_cast<REAL>(list_w - col1 - col3), static_cast<REAL>(ui_scale(shares_cols_h)));
            ui_draw::text(gfx, ui_theme::instance().font_caption(), L"Folder",
                          c2, DT_LEFT, DT_VCENTER, p.text_tertiary);

            Gdiplus::RectF c3(static_cast<REAL>(card.right - card_pad - col3), static_cast<REAL>(y),
                              static_cast<REAL>(col3), static_cast<REAL>(ui_scale(shares_cols_h)));
            ui_draw::text(gfx, ui_theme::instance().font_caption(), L"Access",
                          c3, DT_LEFT, DT_VCENTER, p.text_tertiary);
        }

        // Box around the volume rows.
        if (!IsRectEmpty(&g.rc_list))
        {
            Gdiplus::RectF box = rectf(g.rc_list);
            box.X += 0.5f;
            box.Y += 0.5f;
            box.Width -= 1.0f;
            box.Height -= 1.0f;
            ui_draw::round_rect(gfx, box, static_cast<REAL>(ui_scale(6)),
                                gdi_color(p.card_bg), gdi_color(p.card_stroke));
        }

        // Bottom strip: read-only switch for the selected volume + Remove.
        const int bottom_y = card.bottom - ui_scale(shares_bottom_h);
        ui_draw::h_line(gfx, card.left + card_pad, card.right - card.left - card_pad * 2, bottom_y, p.divider);

        {
            const bool has_sel = g.hwnd != nullptr && g.list != nullptr && selected_index() >= 0;

            RECT toggle_rc{};
            GetWindowRect(g.ro_toggle, &toggle_rc);
            MapWindowPoints(nullptr, g.hwnd, reinterpret_cast<POINT*>(&toggle_rc), 2);

            Gdiplus::RectF label(static_cast<REAL>(card.left + card_pad), static_cast<REAL>(bottom_y),
                                 static_cast<REAL>(toggle_rc.left - card.left - card_pad - ui_scale(12)),
                                 static_cast<REAL>(card.bottom - bottom_y));
            // Right-align the label against the toggle so the two read as
            // one control group (the rect already ends 12px before it).
            ui_draw::text(gfx, ui_theme::instance().font_body(), L"Read only for Mac clients",
                          label, DT_RIGHT, DT_VCENTER, has_sel ? p.text_primary : p.text_disabled);
        }

        // Empty state.
        if (g.volumes.empty())
        {
            const RECT& list_rc = g.rc_list;
            const int cx = (list_rc.left + list_rc.right) / 2;

            {
                Gdiplus::RectF rc(static_cast<REAL>(cx - ui_scale(200)), static_cast<REAL>(list_rc.top + ui_scale(10)),
                                  static_cast<REAL>(ui_scale(400)), static_cast<REAL>(ui_scale(38)));
                ui_draw::text(gfx, ui_theme::instance().font_glyph_large(), fluent_glyph::folder,
                              rc, DT_CENTER, DT_VCENTER, p.text_tertiary);
            }
            {
                Gdiplus::RectF rc(static_cast<REAL>(cx - ui_scale(300)), static_cast<REAL>(list_rc.top + ui_scale(56)),
                                  static_cast<REAL>(ui_scale(600)), static_cast<REAL>(ui_scale(20)));
                ui_draw::text(gfx, ui_theme::instance().font_body_bold(), L"No shared volumes",
                              rc, DT_CENTER, DT_VCENTER, p.text_secondary);
            }
            {
                Gdiplus::RectF rc(static_cast<REAL>(cx - ui_scale(340)), static_cast<REAL>(list_rc.top + ui_scale(82)),
                                  static_cast<REAL>(ui_scale(680)), static_cast<REAL>(ui_scale(18)));
                ui_draw::text(gfx, ui_theme::instance().font_caption(),
                              L"Select \u201cAdd Share\u201d to make a Windows folder available to Macintosh clients.",
                              rc, DT_CENTER, DT_VCENTER, p.text_tertiary);
            }
        }
    }

    // Draws a settings row: label (+ optional caption) with the toggle on
    // the right edge of the card. label_top is in design units.
    void paint_toggle_row(
        Gdiplus::Graphics& gfx,
        const RECT& card,
        const HWND toggle,
        const std::wstring& label,
        const std::wstring& caption,
        const int label_top)
    {
        const auto& p = ui_theme::instance().colors();

        RECT toggle_rc{};
        GetWindowRect(toggle, &toggle_rc);
        MapWindowPoints(nullptr, g.hwnd, reinterpret_cast<POINT*>(&toggle_rc), 2);

        Gdiplus::RectF label_rc(static_cast<REAL>(card.left + card_pad),
                                static_cast<REAL>(card.top + ui_scale(label_top)),
                                static_cast<REAL>(toggle_rc.left - card.left - card_pad - ui_scale(12)),
                                static_cast<REAL>(ui_scale(24)));
        ui_draw::text(gfx, ui_theme::instance().font_body(), label, label_rc, DT_LEFT, DT_VCENTER, p.text_primary);

        if (!caption.empty())
        {
            Gdiplus::RectF cap(static_cast<REAL>(card.left + card_pad), label_rc.GetBottom() + ui_scale(3),
                               static_cast<REAL>(card.right - card.left - card_pad * 2), static_cast<REAL>(ui_scale(40)));
            ui_draw::text_wrap(gfx, ui_theme::instance().font_caption(), caption, cap, DT_LEFT, p.text_secondary);
        }
    }

    void paint_frame_around_edit(Gdiplus::Graphics& gfx, const RECT& frame, const bool focused)
    {
        if (IsRectEmpty(&frame))
            return;

        const auto& p = ui_theme::instance().colors();

        Gdiplus::RectF f = rectf(frame);
        f.X += 0.5f;
        f.Y += 0.5f;
        f.Width -= 1.0f;
        f.Height -= 1.0f;

        const REAL d = static_cast<REAL>(ui_scale(4) * 2);
        Gdiplus::GraphicsPath path;
        path.AddArc(f.X, f.Y, d, d, 180.0f, 90.0f);
        path.AddArc(f.GetRight() - d, f.Y, d, d, 270.0f, 90.0f);
        path.AddArc(f.GetRight() - d, f.GetBottom() - d, d, d, 0.0f, 90.0f);
        path.AddArc(f.X, f.GetBottom() - d, d, d, 90.0f, 90.0f);
        path.CloseFigure();

        const Gdiplus::Pen pen(gdi_color(focused ? p.accent : p.edit_stroke), focused ? 2.0f : 1.0f);
        gfx.DrawPath(&pen, &path);
    }

    void paint_security_card(Gdiplus::Graphics& gfx)
    {
        const auto& p = ui_theme::instance().colors();
        const RECT& card = g.rc_security;

        ui_draw::round_rect(gfx, rectf(card), static_cast<REAL>(ui_scale(card_radius)),
                            gdi_color(p.card_bg), gdi_color(p.card_stroke));

        {
            Gdiplus::RectF rc(static_cast<REAL>(card.left + card_pad), static_cast<REAL>(card.top + card_pad),
                              static_cast<REAL>(ui_scale(300)), static_cast<REAL>(ui_scale(24)));
            ui_draw::text(gfx, ui_theme::instance().font_section(),
                          L"Security & Access", rc, DT_LEFT, DT_VCENTER, p.text_primary);
        }

        paint_toggle_row(gfx, card, g.guest_toggle, L"Allow guest access",
                         L"Let Macintosh clients connect without a username and password.", 52);
        paint_toggle_row(gfx, card, g.enc_toggle, L"Require encrypted login",
                         L"Refuse cleartext passwords from older clients. AFP file transfers themselves are not encrypted.",
                         128);

        ui_draw::h_line(gfx, card.left + card_pad, card.right - card.left - card_pad * 2,
                        card.top + ui_scale(200), p.divider);

        {
            Gdiplus::RectF rc(static_cast<REAL>(card.left + card_pad), static_cast<REAL>(card.top + ui_scale(214)),
                              static_cast<REAL>(ui_scale(260)), static_cast<REAL>(ui_scale(22)));
            ui_draw::text(gfx, ui_theme::instance().font_body(),
                          L"Maximum connections", rc, DT_LEFT, DT_VCENTER, p.text_primary);

            Gdiplus::RectF hint(static_cast<REAL>(card.right - card_pad - ui_scale(150)),
                                static_cast<REAL>(card.top + ui_scale(215)),
                                static_cast<REAL>(ui_scale(150)), static_cast<REAL>(ui_scale(20)));
            ui_draw::text(gfx, ui_theme::instance().font_caption(),
                          L"0 = unlimited", hint, DT_RIGHT, DT_VCENTER, p.text_tertiary);
        }

        paint_frame_around_edit(gfx, g.rc_max_edit_frame, g.focused_edit_id == IDC_MAX_CONNECTIONS);
    }

    void paint_login_card(Gdiplus::Graphics& gfx)
    {
        const auto& p = ui_theme::instance().colors();
        const RECT& card = g.rc_login;

        ui_draw::round_rect(gfx, rectf(card), static_cast<REAL>(ui_scale(card_radius)),
                            gdi_color(p.card_bg), gdi_color(p.card_stroke));

        {
            Gdiplus::RectF rc(static_cast<REAL>(card.left + card_pad), static_cast<REAL>(card.top + card_pad),
                              static_cast<REAL>(ui_scale(300)), static_cast<REAL>(ui_scale(24)));
            ui_draw::text(gfx, ui_theme::instance().font_section(),
                          L"Login Message", rc, DT_LEFT, DT_VCENTER, p.text_primary);
        }
        {
            Gdiplus::RectF rc(static_cast<REAL>(card.left + card_pad), static_cast<REAL>(card.top + ui_scale(56)),
                              static_cast<REAL>(card.right - card.left - card_pad * 2), static_cast<REAL>(ui_scale(18)));
            ui_draw::text(gfx, ui_theme::instance().font_caption(),
                          L"Shown to Macintosh clients when they connect", rc, DT_LEFT, DT_VCENTER, p.text_secondary);
        }

        paint_frame_around_edit(gfx, g.rc_msg_edit_frame, g.focused_edit_id == IDC_LOGIN_MSG);

        {
            const int len = GetWindowTextLengthW(g.msg_edit);
            WCHAR counter[32]{};
            swprintf_s(counter, L"%d / %d", len, message_max_length);
            ui_draw::text(gfx, ui_theme::instance().font_caption(), counter, rectf(g.rc_msg_counter),
                          DT_RIGHT, DT_VCENTER, len >= message_max_length ? p.warn_text : p.text_tertiary);
        }
    }

    void paint_advanced_card(Gdiplus::Graphics& gfx)
    {
        const auto& p = ui_theme::instance().colors();
        const RECT& card = g.rc_adv;

        ui_draw::round_rect(gfx, rectf(card), static_cast<REAL>(ui_scale(card_radius)),
                            gdi_color(p.card_bg), gdi_color(p.card_stroke));

        if (!g.advanced_open)
            return;

        ui_draw::h_line(gfx, card.left + card_pad, card.right - card.left - card_pad * 2,
                        card.top + ui_scale(adv_collapsed_h), p.divider);

        {
            Gdiplus::RectF rc(static_cast<REAL>(card.left + card_pad), static_cast<REAL>(card.top + ui_scale(64)),
                              static_cast<REAL>(card.right - card.left - card_pad * 2), static_cast<REAL>(ui_scale(36)));
            ui_draw::text(gfx, ui_theme::instance().font_caption(),
                          L"Line endings in files with these extensions convert to Unix (LF) when saved to the server and "
                          L"back to Macintosh (CR) when read by Mac clients.",
                          rc, DT_LEFT, DT_TOP, p.text_secondary);
        }

        paint_frame_around_edit(gfx, g.rc_ext_edit_frame, g.focused_edit_id == IDC_CONVERT_EXTENSIONS);

        {
            const int len = GetWindowTextLengthW(g.ext_edit);
            WCHAR counter[32]{};
            swprintf_s(counter, L"%d / %d", len, max_extension_string_size);
            ui_draw::text(gfx, ui_theme::instance().font_caption(), counter, rectf(g.rc_ext_counter),
                          DT_RIGHT, DT_VCENTER, len >= max_extension_string_size ? p.warn_text : p.text_tertiary);
        }
    }

    void paint_footer(Gdiplus::Graphics& gfx)
    {
        const auto& p = ui_theme::instance().colors();

        const std::wstring ver = string_to_wide_string(VER_FILE_SHORT_VERSION_STR, CP_UTF8);
        WCHAR version[64]{};
        swprintf_s(version, L"Version %s", ver.c_str());

        const Gdiplus::RectF rc(static_cast<REAL>(margin), static_cast<REAL>(g.rc_footer.top),
                                static_cast<REAL>(ui_scale(300)),
                                static_cast<REAL>(g.rc_footer.bottom - g.rc_footer.top));
        ui_draw::text(gfx, ui_theme::instance().font_caption(), version, rc, DT_LEFT, DT_VCENTER, p.text_tertiary);
    }

    void paint_adv_header_button(const DRAWITEMSTRUCT* dis)
    {
        const auto& p = ui_theme::instance().colors();
        const bool hot = fluent_button_is_hot(dis->hwndItem);

        const int item_w = dis->rcItem.right - dis->rcItem.left;
        const int item_h = dis->rcItem.bottom - dis->rcItem.top;
        if (item_w <= 0 || item_h <= 0)
            return;

        // Render into a 32-bit memory bitmap and blit: GDI+ text drawn
        // straight onto an owner-draw (DRAWITEMSTRUCT) DC degrades to 1-bit
        // aliased output; a memory DC renders grayscale AA correctly.
        HDC mem_dc = CreateCompatibleDC(dis->hDC);
        if (mem_dc == nullptr)
            return;
        HBITMAP mem_bm = CreateCompatibleBitmap(dis->hDC, item_w, item_h);
        if (mem_bm == nullptr)
        {
            DeleteDC(mem_dc);
            return;
        }
        HGDIOBJ old_bm = SelectObject(mem_dc, mem_bm);
        SetBkMode(mem_dc, TRANSPARENT);

        Gdiplus::Graphics gfx(mem_dc);
        gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        gfx.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
        gfx.TranslateTransform(static_cast<Gdiplus::REAL>(-dis->rcItem.left),
                               static_cast<Gdiplus::REAL>(-dis->rcItem.top));

        const Gdiplus::RectF rc(static_cast<REAL>(dis->rcItem.left), static_cast<REAL>(dis->rcItem.top),
                                static_cast<REAL>(item_w), static_cast<REAL>(item_h));

        // Clear with the card color, then a hover wash.
        {
            gfx.SetSmoothingMode(Gdiplus::SmoothingModeNone);
            Gdiplus::SolidBrush surface(gdi_color(p.card_bg));
            gfx.FillRectangle(&surface, rc);
            gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        }
        if (hot)
        {
            // Inset by 1px: AA geometry touching the canvas boundary
            // renders with half coverage (gray fringe).
            Gdiplus::RectF hover_rc = rc;
            hover_rc.X += 1.0f;
            hover_rc.Y += 1.0f;
            hover_rc.Width -= 2.0f;
            hover_rc.Height -= 2.0f;
            const COLORREF hover_bg = mix_ref(p.card_bg, p.list_selected, 0.55);
            ui_draw::round_rect(gfx, hover_rc, static_cast<REAL>(ui_scale(card_radius)), gdi_color(hover_bg), nullptr);
        }

        const REAL text_x = static_cast<REAL>(dis->rcItem.left + ui_scale(card_pad));
        const REAL text_h = rc.Height;

        Gdiplus::RectF glyph_rc(static_cast<REAL>(text_x), rc.Y, static_cast<REAL>(ui_scale(20)), text_h);
        ui_draw::text(gfx, ui_theme::instance().font_glyph(), fluent_glyph::settings,
                      glyph_rc, DT_LEFT, DT_VCENTER, p.text_secondary);

        Gdiplus::RectF title(static_cast<REAL>(text_x + ui_scale(32)), rc.Y,
                             static_cast<REAL>(ui_scale(360)), text_h);
        ui_draw::text(gfx, ui_theme::instance().font_section(), L"Advanced Settings",
                      title, DT_LEFT, DT_VCENTER, p.text_primary);

        Gdiplus::RectF chevron(static_cast<REAL>(dis->rcItem.right - ui_scale(card_pad) - ui_scale(16)),
                               rc.Y, static_cast<REAL>(ui_scale(16)), text_h);
        ui_draw::text(gfx, ui_theme::instance().font_glyph_small(),
                      g.advanced_open ? fluent_glyph::chevron_down : fluent_glyph::chevron_right,
                      chevron, DT_LEFT, DT_VCENTER, p.text_secondary);

        if ((dis->itemState & ODS_FOCUS) != 0)
        {
            Gdiplus::RectF focus_rc = rc;
            focus_rc.X += 1.0f;
            focus_rc.Y += 1.0f;
            focus_rc.Width -= 2.0f;
            focus_rc.Height -= 2.0f;
            ui_draw::focus_ring(gfx, focus_rc, static_cast<REAL>(ui_scale(4)), p.card_bg);
        }

        gfx.Flush();
        BitBlt(dis->hDC, dis->rcItem.left, dis->rcItem.top, item_w, item_h, mem_dc, 0, 0, SRCCOPY);
        SelectObject(mem_dc, old_bm);
        DeleteObject(mem_bm);
        DeleteDC(mem_dc);
    }

    void paint_toast(Gdiplus::Graphics& gfx, const RECT& client)
    {
        if (g.toast_expire == 0 || g.toast_text.empty())
            return;

        const auto& p = ui_theme::instance().colors();

        const Gdiplus::RectF text_box = ui_draw::measure(gfx, ui_theme::instance().font_body(), g.toast_text);
        const REAL pad_x = static_cast<REAL>(ui_scale(14));
        const REAL h = static_cast<REAL>(ui_scale(38));
        const REAL w = text_box.Width + pad_x * 2 + static_cast<REAL>(ui_scale(20));
        const REAL x = client.left + (client.right - client.left - w) * 0.5f;
        const REAL y = static_cast<REAL>(g.rc_footer.top - ui_scale(12)) - h;

        const Gdiplus::RectF toast(x, y, w, h);
        const Gdiplus::Color stroke_color(gdi_color(p.toast_stroke));
        ui_draw::round_rect(gfx, toast, static_cast<REAL>(ui_scale(6)), gdi_color(p.toast_bg), &stroke_color);

        Gdiplus::RectF glyph(x + pad_x, y, static_cast<REAL>(ui_scale(16)), h);
        ui_draw::text(gfx, ui_theme::instance().font_glyph(), fluent_glyph::check_mark,
                      glyph, DT_LEFT, DT_VCENTER, p.pill_ok_text);

        Gdiplus::RectF txt(x + pad_x + ui_scale(20), y, text_box.Width + ui_scale(4), h);
        ui_draw::text(gfx, ui_theme::instance().font_body(), g.toast_text, txt, DT_LEFT, DT_VCENTER, p.text_primary);

        g.rc_toast.left = static_cast<LONG>(x) - 2;
        g.rc_toast.top = static_cast<LONG>(y) - 2;
        g.rc_toast.right = static_cast<LONG>(x + w) + 2;
        g.rc_toast.bottom = static_cast<LONG>(y + h) + 2;
    }

    void on_paint(const HWND hwnd)
    {
        PAINTSTRUCT ps;
        const HDC dc = BeginPaint(hwnd, &ps);

        RECT client{};
        GetClientRect(hwnd, &client);
        ensure_buffer(dc, client);

        HDC paint_dc = g.mem_dc != nullptr ? g.mem_dc : dc;

        {
            Gdiplus::Graphics gfx(paint_dc);
            gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            gfx.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);

            const auto& p = ui_theme::instance().colors();
            {
                // Smoothing off: the root fill touches the back-buffer
                // boundary, where AA geometry half-bleeds into a gray fringe.
                gfx.SetSmoothingMode(Gdiplus::SmoothingModeNone);
                Gdiplus::SolidBrush bg(gdi_color(p.window_bg));
                gfx.FillRectangle(&bg, 0, 0, client.right, client.bottom);
                gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            }

            paint_header(paint_dc, gfx, client);
            paint_shares_card(gfx);
            paint_security_card(gfx);
            paint_login_card(gfx);
            paint_advanced_card(gfx);
            paint_footer(gfx);
            paint_toast(gfx, client);
        }

        if (g.mem_dc != nullptr)
            BitBlt(dc, 0, 0, client.right, client.bottom, g.mem_dc, 0, 0, SRCCOPY);

        EndPaint(hwnd, &ps);
    }

    // ------------------------------------------------------- list item draw

    void on_draw_volume_row(const DRAWITEMSTRUCT* dis)
    {
        const int index = static_cast<int>(dis->itemID);
        if (index < 0 || index >= static_cast<int>(g.volumes.size()))
        {
            // Empty filler rows: paint the card background ourselves so the
            // themed list view cannot draw gridline artifacts in dark mode.
            Gdiplus::Graphics empty_gfx(dis->hDC);
            empty_gfx.SetSmoothingMode(Gdiplus::SmoothingModeNone);
            Gdiplus::SolidBrush card_brush(gdi_color(ui_theme::instance().colors().card_bg));
            empty_gfx.FillRectangle(&card_brush,
                                    static_cast<int>(dis->rcItem.left), static_cast<int>(dis->rcItem.top),
                                    static_cast<int>(dis->rcItem.right - dis->rcItem.left),
                                    static_cast<int>(dis->rcItem.bottom - dis->rcItem.top));
            return;
        }

        const auto& v = g.volumes[index];
        const auto& p = ui_theme::instance().colors();

        RECT row = dis->rcItem;
        {
            RECT list_client{};
            GetClientRect(g.list, &list_client);
            if (list_client.right > row.right)
                row.right = list_client.right;
        }

        const int row_w = row.right - row.left;
        const int row_h = row.bottom - row.top;
        if (row_w <= 0 || row_h <= 0)
            return;

        // Render the row into a 32-bit memory bitmap and blit it. GDI+ text
        // drawn straight onto the list view's draw-item DC degrades to
        // 1-bit aliased output; a memory DC renders grayscale
        // anti-aliasing correctly (same as the window back-buffer).
        HDC row_dc = CreateCompatibleDC(dis->hDC);
        if (row_dc == nullptr)
            return;
        HBITMAP row_bm = CreateCompatibleBitmap(dis->hDC, row_w, row_h);
        if (row_bm == nullptr)
        {
            DeleteDC(row_dc);
            return;
        }
        HGDIOBJ old_bm = SelectObject(row_dc, row_bm);
        SetBkMode(row_dc, TRANSPARENT);

        Gdiplus::Graphics gfx(row_dc);
        gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        gfx.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
        gfx.TranslateTransform(static_cast<Gdiplus::REAL>(-row.left),
                               static_cast<Gdiplus::REAL>(-row.top));

        const bool selected = (dis->itemState & ODS_SELECTED) != 0;
        const bool hot = index == g.hot_row;

        {
            // Smoothing off for the full-bleed background fill: AA fills at
            // the bitmap boundary half-bleed into a gray fringe.
            gfx.SetSmoothingMode(Gdiplus::SmoothingModeNone);
            Gdiplus::SolidBrush bg(gdi_color(p.card_bg));
            gfx.FillRectangle(&bg, row.left, row.top, row_w, row_h);
            gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        }

        const Gdiplus::RectF inset(4.0f, static_cast<REAL>(row.top + 2),
                                   static_cast<REAL>(row.right - 8),
                                   static_cast<REAL>(row.bottom - row.top - 4));

        if (selected)
            ui_draw::round_rect(gfx, inset, static_cast<REAL>(ui_scale(4)),
                                gdi_color(hot ? p.list_selected_hover : p.list_selected), nullptr);
        else if (hot)
            ui_draw::round_rect(gfx, inset, static_cast<REAL>(ui_scale(4)), gdi_color(p.list_hover), nullptr);

        if (selected)
        {
            const REAL bar_h = static_cast<REAL>(ui_scale(16));
            const Gdiplus::RectF bar(inset.X + static_cast<REAL>(ui_scale(4)),
                                     static_cast<REAL>(row.top) + (static_cast<REAL>(row.bottom - row.top) - bar_h) * 0.5f,
                                     static_cast<REAL>(ui_scale(3)), bar_h);
            ui_draw::round_rect(gfx, bar, bar_h * 0.5f * 0.1875f, gdi_color(p.accent), nullptr);
        }

        const int text_h = row.bottom - row.top;
        const int pad = ui_scale(card_pad) + ui_scale(10);

        Gdiplus::RectF glyph(static_cast<REAL>(pad), static_cast<REAL>(row.top),
                             static_cast<REAL>(ui_scale(18)), static_cast<REAL>(text_h));
        ui_draw::text(gfx, ui_theme::instance().font_glyph(), fluent_glyph::folder,
                      glyph, DT_LEFT, DT_VCENTER, selected ? p.text_primary : p.text_secondary);

        const int col0_x = pad + ui_scale(26);
        const int col0_w = ui_scale(col_volume_w) - ui_scale(26) - ui_scale(8);
        Gdiplus::RectF name(static_cast<REAL>(col0_x), static_cast<REAL>(row.top),
                            static_cast<REAL>(col0_w), static_cast<REAL>(text_h));
        ui_draw::text_ellipsis(gfx, ui_theme::instance().font_body_bold(), v.name,
                               name, DT_LEFT, DT_VCENTER, p.text_primary);

        const int col1_x = ui_scale(col_volume_w);
        const int col1_w = row.right - col1_x - ui_scale(col_access_w) - ui_scale(8);
        Gdiplus::RectF path(static_cast<REAL>(col1_x), static_cast<REAL>(row.top),
                            static_cast<REAL>(col1_w), static_cast<REAL>(text_h));
        ui_draw::text_ellipsis(gfx, ui_theme::instance().font_body(), v.path,
                               path, DT_LEFT, DT_VCENTER, p.text_secondary);

        Gdiplus::RectF access(static_cast<REAL>(row.right - ui_scale(col_access_w)), static_cast<REAL>(row.top),
                              static_cast<REAL>(ui_scale(col_access_w) - ui_scale(8)), static_cast<REAL>(text_h));
        if (v.indexing)
            ui_draw::text_ellipsis(gfx, ui_theme::instance().font_caption(),
                                   L"Indexing\u2026", access, DT_LEFT, DT_VCENTER, p.warn_text);
        else
            ui_draw::text_ellipsis(gfx, ui_theme::instance().font_body(),
                                   v.read_only ? L"Read Only" : L"Read/Write",
                                   access, DT_LEFT, DT_VCENTER, p.text_secondary);

        gfx.Flush();
        BitBlt(dis->hDC, row.left, row.top, row_w, row_h, row_dc, 0, 0, SRCCOPY);
        SelectObject(row_dc, old_bm);
        DeleteObject(row_bm);
        DeleteDC(row_dc);
    }

    // --------------------------------------------------------------- layout

    // Measures a Fluent button label (optional leading glyph) and places the
    // button at x,y with the given height.
    void place_button(const HWND btn, const int x, const int y, const int h)
    {
        WCHAR text[256]{};
        GetWindowTextW(btn, text, static_cast<int>(std::size(text)));

        int width = 0;
        if (text[0] != 0)
        {
            const HDC dc = GetDC(g.hwnd);
            Gdiplus::Graphics gfx(dc);
            gfx.SetSmoothingMode(Gdiplus::SmoothingModeNone);

            std::wstring label = text;
            const auto first = text[0];
            if (first >= 0xE000 && first <= 0xF8FF)
            {
                const std::wstring glyph(1, first);
                label.erase(label.begin());
                while (!label.empty() && label.front() == L' ')
                    label.erase(label.begin());

                width += static_cast<int>(ui_draw::measure(gfx, ui_theme::instance().font_glyph(), glyph).Width);
                width += ui_scale(7);
            }
            width += static_cast<int>(ui_draw::measure(gfx, ui_theme::instance().font_button(), label).Width);
            ReleaseDC(g.hwnd, dc);
        }
        // +ui_scale(4): MeasureString returns ink bounds, which can be a few
        // px narrower than the layout advance, triggering a spurious ellipsis.
        width += ui_scale(11) * 2 + ui_scale(4);

        MoveWindow(btn, x, y, width, h, TRUE);
    }

    int button_width(HWND btn)
    {
        RECT rc{};
        GetWindowRect(btn, &rc);
        return rc.right - rc.left;
    }

    void relayout()
    {
        if (g.hwnd == nullptr || g.list == nullptr)
            return;

        RECT client{};
        GetClientRect(g.hwnd, &client);

        const int w = client.right;
        const int adv_h = ui_scale(g.advanced_open ? adv_expanded_h : adv_collapsed_h);

        g.rc_footer = { 0, client.bottom - ui_scale(footer_h), w, client.bottom };

        int y = ui_scale(header_h) + ui_scale(gap);

        const int fixed_below = adv_h + ui_scale(two_card_h) + ui_scale(gap * 2 + footer_gap + footer_h);
        int shares_h = client.bottom - y - fixed_below;
        shares_h = max(shares_h, ui_scale(178));

        g.rc_shares = { margin, y, w - margin, y + shares_h };
        y = g.rc_shares.bottom + ui_scale(gap);

        const int card_w = (w - margin * 2 - ui_scale(gap)) / 2;
        g.rc_security = { margin, y, margin + card_w, y + ui_scale(two_card_h) };
        g.rc_login = { margin + card_w + ui_scale(gap), y, w - margin, y + ui_scale(two_card_h) };
        y = g.rc_security.bottom + ui_scale(gap);

        g.rc_adv = { margin, y, w - margin, y + adv_h };

        const int btn_h = ui_scale(34);

        // Add Share (top-right of the shares card).
        place_button(g.add_btn, 0, 0, btn_h);
        place_button(g.add_btn, g.rc_shares.right - card_pad - button_width(g.add_btn),
                     g.rc_shares.top + (ui_scale(shares_head_h) - btn_h) / 2, btn_h);

        // Volume list.
        {
            const int list_x = g.rc_shares.left + card_pad;
            const int list_top = g.rc_shares.top + ui_scale(shares_head_h + shares_cols_h);
            const int list_bottom = g.rc_shares.bottom - ui_scale(shares_bottom_h) - ui_scale(12);
            // Shrink the list to exactly fit the rows when they do not fill
            // the available space: a themed (dark mode) list view draws
            // gridline artifacts in its empty area below the last row.
            const int avail_h = list_bottom - list_top;
            const int items_h = ListView_GetItemCount(g.list) * MulDiv(row_h, g.dpi, 96) + 2; // + frame inset
            const int fitted_bottom = list_top + ((items_h > 0 && items_h < avail_h) ? items_h : avail_h);

            g.rc_list = { list_x, list_top, g.rc_shares.right - card_pad, fitted_bottom };
            // Sit the list 1px inside the card-drawn box frame so its row
            // painting never covers the frame stroke.
            MoveWindow(g.list, g.rc_list.left + 1, g.rc_list.top + 1,
                       g.rc_list.right - g.rc_list.left - 2, g.rc_list.bottom - g.rc_list.top - 2, TRUE);

            const int list_w = g.rc_list.right - g.rc_list.left - 2;
            ListView_SetColumnWidth(g.list, 0, ui_scale(col_volume_w));
            ListView_SetColumnWidth(g.list, 1, list_w - ui_scale(col_volume_w) - ui_scale(col_access_w));
            ListView_SetColumnWidth(g.list, 2, ui_scale(col_access_w));
        }

        // Read-only toggle + Remove button in the shares card footer.
        {
            const int toggle_w = ui_scale(44);
            const int toggle_h = ui_scale(22);
            const int bottom_row_top = g.rc_shares.bottom - ui_scale(shares_bottom_h) + ui_scale(13);
            const int row_h2 = ui_scale(34);

            place_button(g.remove_btn, 0, 0, btn_h);
            const int remove_w = button_width(g.remove_btn);
            place_button(g.remove_btn, g.rc_shares.right - card_pad - remove_w,
                         bottom_row_top + (row_h2 - btn_h) / 2, btn_h);

            MoveWindow(g.ro_toggle,
                       g.rc_shares.right - card_pad - remove_w - ui_scale(16) - toggle_w,
                       bottom_row_top + (row_h2 - toggle_h) / 2,
                       toggle_w, toggle_h, TRUE);
        }

        // Security card children.
        {
            const int toggle_w = ui_scale(44);
            const int toggle_h = ui_scale(22);
            const int right = g.rc_security.right - card_pad - toggle_w;

            MoveWindow(g.guest_toggle, right, g.rc_security.top + ui_scale(54), toggle_w, toggle_h, TRUE);
            MoveWindow(g.enc_toggle, right, g.rc_security.top + ui_scale(130), toggle_w, toggle_h, TRUE);

            g.rc_max_edit_frame = { g.rc_security.left + card_pad,
                                    g.rc_security.top + ui_scale(240),
                                    g.rc_security.right - card_pad,
                                    g.rc_security.top + ui_scale(272) };
            MoveWindow(g.max_edit,
                       g.rc_max_edit_frame.left + ui_scale(7),
                       g.rc_max_edit_frame.top + ui_scale(4),
                       g.rc_max_edit_frame.right - g.rc_max_edit_frame.left - ui_scale(14),
                       g.rc_max_edit_frame.bottom - g.rc_max_edit_frame.top - ui_scale(8),
                       TRUE);
        }

        // Login card children.
        {
            g.rc_msg_edit_frame = { g.rc_login.left + card_pad,
                                    g.rc_login.top + ui_scale(82),
                                    g.rc_login.right - card_pad,
                                    g.rc_login.bottom - card_pad - ui_scale(22) };
            MoveWindow(g.msg_edit,
                       g.rc_msg_edit_frame.left + ui_scale(8),
                       g.rc_msg_edit_frame.top + ui_scale(6),
                       g.rc_msg_edit_frame.right - g.rc_msg_edit_frame.left - ui_scale(16),
                       g.rc_msg_edit_frame.bottom - g.rc_msg_edit_frame.top - ui_scale(12),
                       TRUE);

            g.rc_msg_counter = { g.rc_msg_edit_frame.left, g.rc_msg_edit_frame.bottom + ui_scale(4),
                                 g.rc_msg_edit_frame.right, g.rc_msg_edit_frame.bottom + ui_scale(20) };
        }

        // Advanced card children.
        {
            MoveWindow(g.adv_btn,
                       g.rc_adv.left + 1, g.rc_adv.top + 1,
                       g.rc_adv.right - g.rc_adv.left - 2,
                       ui_scale(adv_collapsed_h) - 2,
                       TRUE);

            if (g.advanced_open)
            {
                g.rc_ext_edit_frame = { g.rc_adv.left + card_pad,
                                        g.rc_adv.top + ui_scale(116),
                                        g.rc_adv.right - card_pad,
                                        g.rc_adv.top + ui_scale(148) };
                MoveWindow(g.ext_edit,
                           g.rc_ext_edit_frame.left + ui_scale(8),
                           g.rc_ext_edit_frame.top + ui_scale(4),
                           g.rc_ext_edit_frame.right - g.rc_ext_edit_frame.left - ui_scale(16),
                           g.rc_ext_edit_frame.bottom - g.rc_ext_edit_frame.top - ui_scale(8),
                           TRUE);

                g.rc_ext_counter = { g.rc_ext_edit_frame.left, g.rc_ext_edit_frame.bottom + ui_scale(4),
                                     g.rc_ext_edit_frame.right, g.rc_ext_edit_frame.bottom + ui_scale(20) };
                ShowWindow(g.ext_edit, SW_SHOW);
            }
            else
            {
                ShowWindow(g.ext_edit, SW_HIDE);
                SetRectEmpty(&g.rc_ext_edit_frame);
                SetRectEmpty(&g.rc_ext_counter);
            }
        }

        // Footer buttons, right aligned: About, Send Message, Shut Down,
        // Save & Hide.
        {
            HWND buttons[] = { g.about_btn, g.send_btn, g.shutdown_btn, g.savehide_btn };
            int x = g.rc_footer.right - margin;
            const int b_y = g.rc_footer.top + (ui_scale(footer_h) - btn_h) / 2;

            for (int i = 3; i >= 0; --i)
            {
                place_button(buttons[i], 0, 0, btn_h);
                const int bw = button_width(buttons[i]);
                place_button(buttons[i], x - bw, b_y, btn_h);
                x -= bw + ui_scale(10);
            }
        }
    }

    // ---------------------------------------------------- child creation

    HWND make_button(PCWSTR text, const int id, const fluent_button_style style, const COLORREF surface)
    {
        HWND btn = CreateWindowExW(
            0, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 10, ui_scale(32),
            g.hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), gh_instance, nullptr);

        make_fluent_button(btn, style);
        fluent_button_set_surface(btn, surface);
        return btn;
    }

    HWND make_edit(const int id, const DWORD extra_styles)
    {
        HWND edit = CreateWindowExW(
            0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPCHILDREN |
            ES_AUTOHSCROLL | extra_styles,
            0, 0, 10, 10,
            g.hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), gh_instance, nullptr);

        SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(ui_theme::instance().gdi_font_body()), FALSE);
        return edit;
    }

    void apply_child_themes()
    {
        const bool dark = ui_theme::instance().is_dark();

        // Undocumented but widely used system theme names that make the
        // standard scrollbars follow dark mode. Harmless if unavailable.
        SetWindowTheme(g.list, dark ? L"DarkMode_Explorer" : nullptr, nullptr);
        SetWindowTheme(g.list, dark ? L"DarkMode_ItemsView" : nullptr, nullptr);
        SetWindowTheme(g.msg_edit, dark ? L"DarkMode_Explorer" : nullptr, nullptr);

        // The owner-draw rows paint themselves, but the empty area below the
        // last row uses the control's background color.
        if (g.list != nullptr)
        {
            const auto& cols = ui_theme::instance().colors();
            ListView_SetBkColor(g.list, cols.card_bg);
            ListView_SetTextBkColor(g.list, cols.card_bg);
            ListView_SetTextColor(g.list, cols.text_primary);
        }

        if (g.edit_brush != nullptr)
        {
            DeleteObject(g.edit_brush);
            g.edit_brush = nullptr;
        }
        g.edit_brush = CreateSolidBrush(ui_theme::instance().colors().edit_bg);

        const HFONT body = ui_theme::instance().gdi_font_body();
        SendMessageW(g.max_edit, WM_SETFONT, reinterpret_cast<WPARAM>(body), TRUE);
        SendMessageW(g.msg_edit, WM_SETFONT, reinterpret_cast<WPARAM>(body), TRUE);
        SendMessageW(g.ext_edit, WM_SETFONT, reinterpret_cast<WPARAM>(body), TRUE);
        SendMessageW(g.list, WM_SETFONT, reinterpret_cast<WPARAM>(body), TRUE);
    }

    // ----------------------------------------------------------- list subclass

    void invalidate_row(const int row)
    {
        if (row < 0)
            return;
        RECT rc{};
        if (ListView_GetItemRect(g.list, row, &rc, LVIR_BOUNDS))
            InvalidateRect(g.list, &rc, FALSE);
    }

    LRESULT CALLBACK list_subclass(
        IN const HWND hwnd,
        IN const UINT message,
        IN const WPARAM w_param,
        IN const LPARAM l_param,
        IN const UINT_PTR,
        IN const DWORD_PTR)
    {
        switch (message)
        {
        case WM_PAINT:
            {
                const LRESULT result = DefSubclassProc(hwnd, message, w_param, l_param);

                // The themed list view draws 1-pixel separator lines across
                // the top/bottom edge of every item - including over our
                // owner-drawn rows - and column-separator artifacts in the
                // empty area below the last item. Paint them all away with
                // the card background; the row paint keeps each row's outer
                // scanline card-colored, so selection pills are untouched.
                const int count = ListView_GetItemCount(hwnd);
                int rows_bottom = 0;
                if (count > 0)
                {
                    RECT last{};
                    if (ListView_GetItemRect(hwnd, count - 1, &last, LVIR_BOUNDS))
                        rows_bottom = last.bottom;
                }

                RECT client{};
                GetClientRect(hwnd, &client);

                const HDC dc = GetDC(hwnd);
                const HBRUSH brush = CreateSolidBrush(ui_theme::instance().colors().card_bg);
                for (int i = 0; i < count; ++i)
                {
                    RECT r{};
                    if (ListView_GetItemRect(hwnd, i, &r, LVIR_BOUNDS))
                    {
                        RECT top{ r.left, r.top, r.right, r.top + 1 };
                        RECT bottom{ r.left, r.bottom - 1, r.right, r.bottom };
                        FillRect(dc, &top, brush);
                        FillRect(dc, &bottom, brush);
                    }
                }
                if (rows_bottom > 0 && rows_bottom < client.bottom)
                {
                    RECT fill{ 0, rows_bottom, client.right, client.bottom };
                    FillRect(dc, &fill, brush);
                }
                DeleteObject(brush);
                ReleaseDC(hwnd, dc);
                return result;
            }

        case WM_ERASEBKGND:
            {
                RECT rc{};
                GetClientRect(hwnd, &rc);
                const HBRUSH brush = CreateSolidBrush(ui_theme::instance().colors().card_bg);
                FillRect(reinterpret_cast<HDC>(w_param), &rc, brush);
                DeleteObject(brush);
                return 1;
            }

        case WM_MOUSEMOVE:
            {
                const POINT pt{ GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param) };
                LVHITTESTINFO hit{};
                hit.pt = pt;
                ListView_SubItemHitTest(hwnd, &hit);
                const int row = (hit.flags & LVHT_ONITEM) != 0 ? hit.iItem : -1;
                if (row != g.hot_row)
                {
                    const int old_row = g.hot_row;
                    g.hot_row = row;
                    invalidate_row(old_row);
                    invalidate_row(row);
                }
                break;
            }

        case WM_MOUSELEAVE:
            if (g.hot_row != -1)
            {
                const int old_row = g.hot_row;
                g.hot_row = -1;
                invalidate_row(old_row);
            }
            break;

        case WM_KEYDOWN:
            if (w_param == VK_DELETE)
            {
                SendMessageW(GetParent(hwnd), WM_COMMAND, IDC_REMOVEVOL,
                             reinterpret_cast<LPARAM>(hwnd));
                return 0;
            }
            break;

        default:
            break;
        }

        // Re-arm hover tracking after the default handler ran.
        if (message == WM_MOUSEMOVE || message == WM_NCMOUSEMOVE)
        {
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
        }

        return DefSubclassProc(hwnd, message, w_param, l_param);
    }

    LRESULT CALLBACK edit_subclass(
        IN const HWND hwnd,
        IN const UINT message,
        IN const WPARAM w_param,
        IN const LPARAM l_param,
        IN const UINT_PTR,
        IN const DWORD_PTR)
    {
        if (message == WM_KEYDOWN && w_param == VK_RETURN)
        {
            // Enter in an edit means "apply", not "hide the window".
            SendMessageW(GetParent(hwnd), WM_COMMAND, ID_ACTION_SAVE, 0);
            return 0;
        }

        return DefSubclassProc(hwnd, message, w_param, l_param);
    }

    // -------------------------------------------------------------- callbacks

    void on_command(const HWND hwnd, const int id, const int code)
    {
        switch (id)
        {
        case IDC_NEWVOLUME:
            do_add_share();
            break;

        case IDC_REMOVEVOL:
            do_remove_volume();
            break;

        case IDC_VOLREADONLY:
            if (code == BN_CLICKED)
                do_read_only_toggle();
            break;

        case IDC_ALLOW_GUESTS:
        case IDC_REQ_ENCRYPTION:
            if (code == BN_CLICKED)
            {
                afp_registry registry;
                registry.set_allow_guests_setting(toggle_get_checked(g.guest_toggle) ? TRUE : FALSE);
                registry.set_force_encrypted_logon(toggle_get_checked(g.enc_toggle) ? TRUE : FALSE);
                set_globals_from_controls();
                show_toast(L"Settings saved");
            }
            break;

        case IDC_MAX_CONNECTIONS:
            if (code == EN_KILLFOCUS)
            {
                BOOL translated = FALSE;
                const DWORD value = GetDlgItemInt(hwnd, IDC_MAX_CONNECTIONS, &translated, FALSE);
                if (translated && value != g_max_sessions)
                {
                    g_max_sessions = value;
                    afp_registry registry;
                    registry.set_max_sessions(value);
                    show_toast(L"Settings saved");
                }
                else if (!translated)
                {
                    SetDlgItemInt(hwnd, IDC_MAX_CONNECTIONS, g_max_sessions, FALSE);
                }
                g.focused_edit_id = 0;
                InvalidateRect(hwnd, &g.rc_max_edit_frame, FALSE);
            }
            else if (code == EN_SETFOCUS)
            {
                g.focused_edit_id = IDC_MAX_CONNECTIONS;
                InvalidateRect(hwnd, &g.rc_max_edit_frame, FALSE);
            }
            break;

        case IDC_LOGIN_MSG:
            if (code == EN_CHANGE)
            {
                if (!IsRectEmpty(&g.rc_login))
                    InvalidateRect(hwnd, &g.rc_login, FALSE);
            }
            else if (code == EN_KILLFOCUS)
            {
                save_login_message();
                g.focused_edit_id = 0;
                InvalidateRect(hwnd, &g.rc_msg_edit_frame, FALSE);
            }
            else if (code == EN_SETFOCUS)
            {
                g.focused_edit_id = IDC_LOGIN_MSG;
                InvalidateRect(hwnd, &g.rc_msg_edit_frame, FALSE);
            }
            break;

        case IDC_CONVERT_EXTENSIONS:
            if (code == EN_CHANGE)
            {
                if (!IsRectEmpty(&g.rc_adv))
                    InvalidateRect(hwnd, &g.rc_adv, FALSE);
            }
            else if (code == EN_KILLFOCUS)
            {
                afp_registry registry;
                WCHAR extensions[max_extension_string_size + 2]{};
                GetWindowTextW(g.ext_edit, extensions, max_extension_string_size + 1);
                registry.set_extensions_for_conversion(extensions);
                g.focused_edit_id = 0;
                InvalidateRect(hwnd, &g.rc_ext_edit_frame, FALSE);
            }
            else if (code == EN_SETFOCUS)
            {
                g.focused_edit_id = IDC_CONVERT_EXTENSIONS;
                InvalidateRect(hwnd, &g.rc_ext_edit_frame, FALSE);
            }
            break;

        case IDC_ADV_HEADER:
            if (code == BN_CLICKED)
            {
                g.advanced_open = !g.advanced_open;

                // Grow / shrink the window so the section fits.
                const int delta = ui_scale(adv_expanded_h - adv_collapsed_h);
                WINDOWPLACEMENT wp{};
                wp.length = sizeof(wp);
                GetWindowPlacement(hwnd, &wp);

                if (wp.showCmd != SW_SHOWMAXIMIZED)
                {
                    RECT rc = wp.rcNormalPosition;
                    const HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                    MONITORINFO mi{};
                    mi.cbSize = sizeof(mi);
                    GetMonitorInfoW(mon, &mi);

                    if (g.advanced_open)
                    {
                        const int available = mi.rcWork.bottom - rc.top;
                        const int want = (rc.bottom - rc.top) + delta;
                        rc.bottom = rc.top + min(want, available);
                    }
                    else
                    {
                        rc.bottom = max(rc.bottom - delta, mi.rcWork.top + ui_scale(648));
                    }
                    SetWindowPos(hwnd, nullptr, rc.left, rc.top,
                                 rc.right - rc.left, rc.bottom - rc.top,
                                 SWP_NOZORDER | SWP_NOACTIVATE);
                }
                relayout();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            break;

        case IDC_CLOSE:
            hide_window();
            break;

        case IDC_QUITBUTTON:
            do_shutdown();
            break;

        case IDC_SENDMSG:
            do_send_message();
            break;

        case IDC_ABOUT:
            about_dialog(hwnd);
            break;

        case ID_ACTION_SAVE:
            do_save();
            break;

        case IDM_TRAY_OPEN:
            main_window_show();
            break;

        case IDM_TRAY_SEND:
            main_window_show();
            do_send_message();
            break;

        case IDM_TRAY_SHUTDOWN:
            do_shutdown();
            break;

        case IDOK:
            {
                const HWND focus = GetFocus();
                if (focus != nullptr)
                {
                    WCHAR class_name[32]{};
                    GetClassNameW(focus, class_name, static_cast<int>(std::size(class_name)));
                    if (lstrcmpiW(class_name, L"BUTTON") == 0)
                    {
                        SendMessageW(focus, BM_CLICK, 0, 0);
                        break;
                    }
                }
                do_save();
            }
            break;

        case IDCANCEL:
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            break;

        default:
            break;
        }
    }

    void on_notify(const NMHDR* nm)
    {
        if (nm->hwndFrom == g.list)
        {
            if (nm->code == LVN_ITEMCHANGED)
            {
                const auto* lv = reinterpret_cast<const NMLISTVIEW*>(nm);
                if ((lv->uChanged & LVIF_STATE) != 0 &&
                    ((lv->uNewState ^ lv->uOldState) & LVIS_SELECTED) != 0)
                {
                    update_selection_ui();
                }
            }
        }
        else if (nm->hwndFrom == g.tooltip && nm->code == TTN_GETDISPINFOW)
        {
            auto* tip = reinterpret_cast<NMTTDISPINFOW*>(const_cast<NMHDR*>(nm));

            POINT pt{};
            GetCursorPos(&pt);
            ScreenToClient(g.list, &pt);
            LVHITTESTINFO hit{};
            hit.pt = pt;
            const int row = ListView_SubItemHitTest(g.list, &hit);

            if (row >= 0 && row < static_cast<int>(g.volumes.size()))
            {
                static std::wstring tip_text;
                tip_text = g.volumes[row].name + L"\n" + g.volumes[row].path;
                tip->lpszText = tip_text.data();
            }
        }
    }

    LRESULT CALLBACK wnd_proc(
        IN const HWND hwnd,
        IN const UINT message,
        IN const WPARAM w_param,
        IN const LPARAM l_param)
    {
        // Dynamic id: the shell broadcasts this registered message when it
        // (re)starts. Re-register our tray icon so it survives Explorer
        // restarts. Checked before the switch because the id is not a
        // compile-time constant.
        static const UINT taskbar_created_msg = RegisterWindowMessageW(L"TaskbarCreated");
        if (message == taskbar_created_msg)
        {
            tray_icon_on_taskbar_created(hwnd);
            return 0;
        }

        switch (message)
        {
        case WM_CREATE:
            g.hwnd = hwnd;
            // Route indexing-complete notifications straight to this window
            // (see afp_volumes::set_index_notify_window).
            g_volumes->set_index_notify_window(hwnd);
            return 0;

        case WM_MEASUREITEM:
            // Row height for the owner-draw volume list. (There is no real
            // LVM_SETITEMHEIGHT message; this is the supported mechanism.)
            if (auto* mis = reinterpret_cast<LPMEASUREITEMSTRUCT>(l_param);
                mis->CtlType == ODT_LISTVIEW && mis->itemID == 0)
            {
                mis->itemHeight = MulDiv(row_h, g.dpi, 96);
                return TRUE;
            }
            break;

        case WM_UI_GET_SURFACE:
            *reinterpret_cast<COLORREF*>(l_param) = ui_theme::instance().colors().card_bg;
            return TRUE;

        case WM_TIMER:
            if (w_param == timer_status)
            {
                const int count = static_cast<int>(active_connections.count());
                if (count != g.last_client_count)
                {
                    g.last_client_count = count;
                    g.pill_text = format_count_text(count);
                    update_tray_tooltip(count);
                    if (!IsRectEmpty(&g.rc_pill))
                        InvalidateRect(hwnd, &g.rc_pill, FALSE);
                    else if (g_main_window_visible)
                        InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            else if (w_param == timer_toast)
            {
                if (GetTickCount() > g.toast_expire)
                    hide_toast();
            }
            return 0;

        case WM_ERASEBKGND:
            return 1; // fully painted in WM_PAINT

        case WM_PAINT:
            on_paint(hwnd);
            return 0;

        case WM_DRAWITEM:
            {
                const auto* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(l_param);
                if (dis->hwndItem == g.list)
                    on_draw_volume_row(dis);
                else if (dis->CtlID == IDC_ADV_HEADER)
                    paint_adv_header_button(dis);
                else
                    draw_fluent_button(dis);
                return TRUE;
            }

        case WM_CTLCOLOREDIT:
            {
                const auto& p = ui_theme::instance().colors();
                const auto dc = reinterpret_cast<HDC>(w_param);
                SetTextColor(dc, p.text_primary);
                SetBkColor(dc, p.edit_bg);
                if (g.edit_brush == nullptr)
                    g.edit_brush = CreateSolidBrush(p.edit_bg);
                return reinterpret_cast<LRESULT>(g.edit_brush);
            }

        case WM_COMMAND:
            on_command(hwnd, LOWORD(w_param), HIWORD(w_param));
            return TRUE;

        case WM_NOTIFY:
            on_notify(reinterpret_cast<const NMHDR*>(l_param));
            return 0;

        case WM_LBUTTONDOWN:
            {
                const POINT pt{ GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param) };
                if (g.toast_expire != 0 && PtInRect(&g.rc_toast, pt))
                    hide_toast();
                return 0;
            }

        case WM_SIZE:
            relayout();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_GETMINMAXINFO:
            {
                auto* mmi = reinterpret_cast<MINMAXINFO*>(l_param);
                mmi->ptMinTrackSize.x = ui_scale(760);
                mmi->ptMinTrackSize.y = ui_scale(756);
                return 0;
            }

        case WM_DPICHANGED:
            {
                g.dpi = HIWORD(w_param);
                ui_theme::instance().set_dpi(g.dpi);
                const auto* suggested = reinterpret_cast<const RECT*>(l_param);
                SetWindowPos(hwnd, nullptr,
                             suggested->left, suggested->top,
                             suggested->right - suggested->left,
                             suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                apply_child_themes();
                relayout();
                InvalidateRect(hwnd, nullptr, TRUE);
                return 0;
            }

        case WM_SETTINGCHANGE:
            if (ui_theme::instance().refresh_if_changed())
            {
                ui_theme::apply_titlebar_theme(hwnd);
                apply_child_themes();

                // Re-point the owner-draw buttons at the new surface colors
                // (they paint the surrounding surface behind their rounded
                // corners), then repaint everything.
                const auto& nc = ui_theme::instance().colors();
                fluent_button_set_surface(g.add_btn, nc.card_bg);
                fluent_button_set_surface(g.remove_btn, nc.card_bg);
                fluent_button_set_surface(g.adv_btn, nc.card_bg);
                fluent_button_set_surface(g.about_btn, nc.window_bg);
                fluent_button_set_surface(g.send_btn, nc.window_bg);
                fluent_button_set_surface(g.shutdown_btn, nc.window_bg);
                fluent_button_set_surface(g.savehide_btn, nc.window_bg);
                HWND buttons[] = { g.add_btn, g.about_btn, g.send_btn, g.shutdown_btn, g.savehide_btn, g.remove_btn };
                for (const auto b : buttons)
                    InvalidateRect(b, nullptr, TRUE);
                if (!g.volumes.empty())
                    ListView_RedrawItems(g.list, 0, static_cast<int>(g.volumes.size()));
                InvalidateRect(g.list, nullptr, TRUE);
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return TRUE;

        case WM_CLOSE:
            // Closing the window must never stop the server: save and hide.
            hide_window();
            return TRUE;

        case WM_QUERYENDSESSION:
            save_window_state();
            return TRUE;

        case WM_ENDSESSION:
            if (w_param != 0)
            {
                remove_tray_icon(hwnd);
                if (g.mem_dc != nullptr)
                {
                    DeleteDC(g.mem_dc);
                    DeleteObject(g.mem_bm);
                    g.mem_dc = nullptr;
                    g.mem_bm = nullptr;
                }
            }
            return 0;

        case WM_DESTROY:
            // Stop posting indexing notices to this (now dead) window first.
            g_volumes->set_index_notify_window(nullptr);
            KillTimer(hwnd, timer_status);
            KillTimer(hwnd, timer_toast);
            save_window_state();
            if (g.mem_dc != nullptr)
            {
                DeleteDC(g.mem_dc);
                DeleteObject(g.mem_bm);
                g.mem_dc = nullptr;
                g.mem_bm = nullptr;
            }
            return 0;

        case AFPWM_INDEXING_COMPLETE:
            // Rebuild regardless of visibility: the Access column is
            // owner-drawn from the cached model, and skipping updates
            // while hidden risks a stale "Indexing..." if any later
            // notification is lost. The rebuild is cheap.
            rebuild_volume_list();
            return 0;

        case AFPWM_SHOW_WINDOW:
            // Cross-process activation request from a duplicate launch
            // (SendMessageTimeoutW from that process; both parameters are
            // zero - never dereference them). Restore the window and bring
            // it forward.
            main_window_show();
            return TRUE;

        case AFPWM_NOTIFYICON:
            switch (l_param)
            {
            case WM_LBUTTONUP:
            case WM_LBUTTONDBLCLK:
                main_window_show();
                break;

            case WM_RBUTTONUP:
            case WM_CONTEXTMENU:
                {
                    POINT pt{};
                    GetCursorPos(&pt);
                    show_tray_menu(pt);
                }
                break;

            default:
                break;
            }
            return 0;

        case WM_POWERBROADCAST:
            if (w_param == PBT_APMQUERYSUSPEND)
            {
                if (active_connections.count() > 0)
                    return BROADCAST_QUERY_DENY;
                return TRUE;
            }
            break;

        default:
            break;
        }

        return DefWindowProcW(hwnd, message, w_param, l_param);
    }
}

// ------------------------------------------------------------------- public

HWND main_window_create(HINSTANCE instance)
{
    static bool registered = false;
    if (!registered)
    {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = wnd_proc;
        wc.hInstance = instance;
        wc.hIcon = LoadIconW(instance, MAKEINTRESOURCE(IDI_TRAYICON));
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = window_class_name;
        RegisterClassExW(&wc);
        registered = true;
    }

    register_toggle_class(instance);

    ui_theme::instance().initialize();
    g.dpi = GetDpiForSystem();
    ui_theme::instance().set_dpi(g.dpi);

    constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME;

    // Window size / position.
    RECT initial{};
    if (!load_window_state(initial))
    {
        const UINT dpi = g.dpi;
        const int cw = MulDiv(1010, dpi, 96);
        const int ch = MulDiv(876, dpi, 96);

        RECT wr = { 0, 0, cw, ch };
        AdjustWindowRectExForDpi(&wr, style, FALSE, WS_EX_APPWINDOW, dpi);

        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        const int fw = wr.right - wr.left;
        const int fh = wr.bottom - wr.top;
        const int x = work.left + ((work.right - work.left) - fw) / 2;
        const int y = work.top + ((work.bottom - work.top) - fh) / 2;
        initial = { x, y, x + fw, y + fh };
    }

    HWND hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        window_class_name,
        L"MacFile",
        style,
        initial.left, initial.top,
        initial.right - initial.left, initial.bottom - initial.top,
        nullptr, nullptr, instance, nullptr);

    if (hwnd == nullptr)
        return nullptr;

    ui_theme::apply_titlebar_theme(hwnd);

    // ---- create children (tab order follows creation order) ----
    const auto& p = ui_theme::instance().colors();

    g.add_btn = make_button((std::wstring(fluent_glyph::add) + L" Add Share").c_str(),
                            IDC_NEWVOLUME, fluent_button_style::accent, p.card_bg);

    g.list = CreateWindowExW(
        0, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP |
        LVS_REPORT | LVS_SINGLESEL | LVS_OWNERDRAWFIXED | LVS_SHOWSELALWAYS |
        LVS_NOCOLUMNHEADER | WS_VSCROLL,
        0, 0, 10, 10,
        hwnd, reinterpret_cast<HMENU>(IDC_SHARES), instance, nullptr);
    // NB: LVS_EX_DOUBLEBUFFER combined with LVS_OWNERDRAWFIXED leaves
    // ghosted artifacts in the empty area below the last row, so it is
    // deliberately not used. Flicker is avoided by the full-row custom draw.
    ListView_SetExtendedListViewStyle(g.list, LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP);
    SendMessageW(g.list, WM_SETFONT, reinterpret_cast<WPARAM>(ui_theme::instance().gdi_font_body()), FALSE);

    {
        LVCOLUMNW col{};
        col.mask = LVIF_TEXT;
        const WCHAR* headers[] = { L"Volume", L"Folder", L"Access" };
        for (int i = 0; i < 3; ++i)
        {
            col.iSubItem = i;
            col.pszText = const_cast<LPWSTR>(headers[i]);
            col.cx = ui_scale(200);
            ListView_InsertColumn(g.list, i, &col);
        }
    }
    SetWindowSubclass(g.list, list_subclass, 1, 0);

    g.ro_toggle = create_toggle_switch(hwnd, instance, IDC_VOLREADONLY, 0, 0, ui_scale(44), ui_scale(22));
    SetWindowTextW(g.ro_toggle, L"Read only for Mac clients");

    g.remove_btn = make_button((std::wstring(fluent_glyph::delete_) + L" Remove Share").c_str(),
                               IDC_REMOVEVOL, fluent_button_style::destructive, p.card_bg);

    g.guest_toggle = create_toggle_switch(hwnd, instance, IDC_ALLOW_GUESTS, 0, 0, ui_scale(44), ui_scale(22));
    SetWindowTextW(g.guest_toggle, L"Allow guest access");

    g.enc_toggle = create_toggle_switch(hwnd, instance, IDC_REQ_ENCRYPTION, 0, 0, ui_scale(44), ui_scale(22));
    SetWindowTextW(g.enc_toggle, L"Require encrypted login");

    g.max_edit = make_edit(IDC_MAX_CONNECTIONS, ES_NUMBER);
    SendMessageW(g.max_edit, EM_LIMITTEXT, 5, 0);
    SetWindowSubclass(g.max_edit, edit_subclass, 1, 0);

    g.msg_edit = make_edit(IDC_LOGIN_MSG, ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL);
    SetWindowSubclass(g.msg_edit, edit_subclass, 2, 0);

    g.adv_btn = CreateWindowExW(
        0, L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 10, ui_scale(adv_collapsed_h),
        hwnd, reinterpret_cast<HMENU>(IDC_ADV_HEADER), instance, nullptr);
    make_fluent_button(g.adv_btn, fluent_button_style::subtle);
    fluent_button_set_surface(g.adv_btn, p.card_bg);

    g.ext_edit = make_edit(IDC_CONVERT_EXTENSIONS, 0);
    SetWindowSubclass(g.ext_edit, edit_subclass, 3, 0);

    g.about_btn = make_button((std::wstring(fluent_glyph::info) + L" About").c_str(),
                              IDC_ABOUT, fluent_button_style::subtle, p.window_bg);
    g.send_btn = make_button((std::wstring(fluent_glyph::send) + L" Send Message").c_str(),
                             IDC_SENDMSG, fluent_button_style::subtle, p.window_bg);
    g.shutdown_btn = make_button((std::wstring(fluent_glyph::power) + L" Shut Down").c_str(),
                                 IDC_QUITBUTTON, fluent_button_style::destructive, p.window_bg);
    g.savehide_btn = make_button((std::wstring(fluent_glyph::save) + L" Save & Hide").c_str(),
                                 IDC_CLOSE, fluent_button_style::accent, p.window_bg);

    // Tooltip for volume rows (full path on hover).
    g.tooltip = CreateWindowExW(
        0, TOOLTIPS_CLASS, nullptr,
        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        hwnd, nullptr, instance, nullptr);
    SendMessageW(g.tooltip, TTM_SETMAXTIPWIDTH, 0, ui_scale(520));
    {
        TOOLINFOW tmi{};
        tmi.cbSize = sizeof(tmi);
        tmi.uFlags = TTF_IDISHWND;
        tmi.hwnd = hwnd;
        tmi.uId = reinterpret_cast<UINT_PTR>(g.list);
        tmi.lpszText = LPSTR_TEXTCALLBACKW;
        SendMessageW(g.tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tmi));
    }

    // ---- initial data ----
    load_settings_into_controls();
    apply_child_themes();
    rebuild_volume_list(L"");
    relayout();

    g.pill_text = format_count_text(0);
    SetTimer(hwnd, timer_status, 1000, nullptr);

    static ACCEL accels[] = {
        { FCONTROL | FVIRTKEY, 'S', ID_ACTION_SAVE },
        { FCONTROL | FVIRTKEY, 'N', IDC_NEWVOLUME },
        { FCONTROL | FVIRTKEY, 'M', IDC_SENDMSG },
    };
    g.accel = CreateAcceleratorTableW(accels, static_cast<int>(std::size(accels)));

    return hwnd;
}

HWND main_window_handle()
{
    return g.hwnd;
}

// Exposed for the single-instance duplicate finder in afpServer.cpp:
// find_primary_main_window() locates the running instance by this class name.
const wchar_t* main_window_class_name()
{
    return window_class_name;
}

HACCEL main_window_accelerator()
{
    return g.accel;
}

void main_window_show()
{
    if (g.hwnd == nullptr)
        return;

    if (!g_main_window_visible)
    {
        const int sel = selected_index();
        std::wstring preserve;
        if (sel >= 0 && sel < static_cast<int>(g.volumes.size()))
            preserve = g.volumes[sel].name;

        rebuild_volume_list(preserve);
        ShowWindow(g.hwnd, SW_SHOW);
        g_main_window_visible = TRUE;
    }
    else
    {
        ShowWindow(g.hwnd, SW_RESTORE);
    }

    SetForegroundWindow(g.hwnd);

    g.last_client_count = -1; // force pill refresh
    if (!g.volumes.empty())
        SetFocus(g.list);
    else
        SetFocus(g.add_btn);
}

void main_window_refresh_volumes()
{
    if (g.hwnd != nullptr)
        rebuild_volume_list();
}
