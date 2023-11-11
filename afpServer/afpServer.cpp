#include "stdafx.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <strsafe.h>

#include "afp.h"
#include "volumes.h"
#include "afpdebug.h"
#include "sendmsgdlg.h"
#include "addshare.h"
#include "aboutdlg.h"
#include "afp_registry.h"
#include "afpServer.h"
#include "resource.h"
#include "afpEvents.h"
#include "afp_volumes.h"
#include "version.h"
#include "dsi/dsi_connections.h"
#include "dsi/dsi_init.h"
#include <afphelp.h>

#define AFP_ICON_UID		IDI_TRAYICON
#define AFP_TOOL_TIP		TEXT("Personal AFP Server Configuration")

HINSTANCE gh_instance;
BOOL g_main_window_visible;

extern CRITICAL_SECTION gVolumesCS;
extern std::unique_ptr<afp_volumes> g_volumes;
extern BOOL g_allow_guests;
extern DWORD g_max_sessions;
extern BOOL g_force_encrypted_logon;

extern dsi::dsi_connections active_connections;


void initialize_tray_icon(
    IN const HWND h_dialog
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
    nid.hWnd = h_dialog;
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
    IN const HWND h_dialog
)
{
    NOTIFYICONDATA nid;

    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = h_dialog;
    nid.uID = AFP_ICON_UID;
    nid.uFlags = 0;

    Shell_NotifyIcon(NIM_DELETE, &nid);
}

void fill_logon_message_box(
    IN const HWND h_dlg
)
{
    TCHAR szMessage[message_max_length + sizeof(TCHAR)];
    afp_registry theRegistry;

    //
    //Tell the edit box to only allow message_max_length characters
    //
    SendDlgItemMessage(h_dlg, IDC_LOGIN_MSG, EM_LIMITTEXT, message_max_length, 0);

    if (theRegistry.get_logon_message(szMessage) == NO_ERROR)
    {
        //
        //Set our current logon message in the edit field.
        //
        SendDlgItemMessage(h_dlg, IDC_LOGIN_MSG, WM_SETTEXT,
                           message_max_length, reinterpret_cast<LPARAM>(szMessage));
    }
}

void update_vol_caption(const HWND h_parent)
{
    // First, send_ a message to the volume listbox to get the currently
    // selected cell.
    LRESULT result = SendDlgItemMessage(h_parent, IDC_SHARES, LB_GETCURSEL, 0, 0);
    if (result == LB_ERR)
    {
        return;
    }

    WCHAR share_name[MAX_PATH];
    DWORD vol_flags = 0;

    // Send a message to the volume list box and ask for the
    // string. This string is the full path to the share.
    result = SendDlgItemMessage(h_parent, IDC_SHARES, LB_GETTEXT, result, reinterpret_cast<LPARAM>(share_name));
    if (result == LB_ERR)
    {
        return;
    }

    afp_registry registry;
    if (!registry.open_volume_list_reg_key())
    {
        return;
    }

    WCHAR password[16];
    WCHAR temp_path[MAX_PATH];
    WCHAR temp_name[33];

    while (registry.next_volume(
        temp_name,
        sizeof(temp_name),
        temp_path,
        sizeof(temp_path),
        reinterpret_cast<PCHAR>(password),
        sizeof(password),
        &vol_flags))
    {
        if (!lstrcmp(temp_name, share_name))
        {
            SendDlgItemMessage(h_parent, IDC_VOLPATH, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(temp_path));

            // See if the volume is set to read only. Set the checkbox
            // to the correct value.
            if (vol_flags & flag_volume_readonly)
            {
                CheckDlgButton(h_parent, IDC_VOLREADONLY, TRUE);
            }
            else
            {
                CheckDlgButton(h_parent, IDC_VOLREADONLY, FALSE);
            }
        }
    }
}

void remove_volume(const HWND h_parent)
{
    // First, send_ a message to the volume listbox to get the currently
    // selected cell.
    LRESULT result = SendDlgItemMessage(h_parent, IDC_SHARES, LB_GETCURSEL, 0, 0);
    if (result == LB_ERR)
    {
        return;
    }

    WCHAR share_name[MAX_PATH];
    WCHAR dlg_title[64];
    WCHAR text_body[64];
    WCHAR dlg_text[512];

    // Send a message to the volume list box and ask for the
    // string. This string is the name of the share.
    result = SendDlgItemMessage(h_parent, IDC_SHARES, LB_GETTEXT, result, reinterpret_cast<LPARAM>(share_name));
    if (result == LB_ERR)
    {
        DBGWRITE(dbg_level_info, L"[MAIN]Bad list offset removing volume\n");
        return;
    }

    LoadString(gh_instance, IDS_STOPSHARINGWARN, text_body, sizeof(text_body) / sizeof(WCHAR));
    LoadString(gh_instance, IDS_STOPSHARINGCONFIRM, dlg_title, sizeof(dlg_title) / sizeof(WCHAR));

    [[maybe_unused]] const auto ignore = StringCbPrintf(dlg_text, sizeof(dlg_text), L"%s %s", text_body, share_name);

    if (const auto dlg_result = MessageBox(h_parent, dlg_text, dlg_title, MB_OKCANCEL); dlg_result == IDCANCEL)
    {
        return;
    }

    DBGWRITE(dbg_level_info, L"[MAIN]Removing volume: %s\n", share_name);

    // Call the volume API that will un-share the volume and remove the
    // volume from the registry.
    if (const DWORD error = AFPRemoveVolumeFromUI(share_name); error != NO_ERROR)
    {
        LoadString(gh_instance, IDS_STOPSHARINGERR, text_body, sizeof(text_body) / sizeof(WCHAR));
        LoadString(gh_instance, IDS_STOPSHARINTERRTITLE, dlg_title, sizeof(dlg_title) / sizeof(WCHAR));

        MessageBox(h_parent, text_body, dlg_title, MB_OK);

        DBGWRITE(dbg_level_info, L"[MAIN]Removing volume failed! (%d)\n", error);
        return;
    }

    // Refreshing the volume list box will remove the item from
    // the list easily.
    SendDlgItemMessage(h_parent, IDC_SHARES, LB_RESETCONTENT, 0, 0);
    fill_volume_list_box(h_parent);
}

void save_logon_message(
    IN const HWND h_dlg
)
{
    WCHAR message[message_max_length + sizeof(TCHAR)];
    afp_registry registry;

    SendDlgItemMessage(h_dlg, IDC_LOGIN_MSG, WM_GETTEXT, message_max_length, reinterpret_cast<LPARAM>(message));

    registry.save_logon_message(message);
}

void fill_volume_list_box(
    IN const HWND h_dlg
)
{
    TCHAR share_name[64]{};
    TCHAR temp[64]{};
    TCHAR share_path[MAX_PATH]{};
    WCHAR first_password[16]{};
    DWORD vol_flags = 0;
    afp_registry registry;

    SendDlgItemMessage(h_dlg, IDC_SHARES, LB_SETCURSEL, 0, 0);

    memset(first_password, 0, sizeof(first_password));

    if (!registry.open_volume_list_reg_key())
    {
        return;
    }

    WORD count = 0;
    DWORD first_flags = 0;
    afp_clear_text_password password;

    while (registry.next_volume(
        share_name,
        sizeof(share_name),
        share_path,
        sizeof(share_path),
        reinterpret_cast<PCHAR>(password),
        sizeof(password),
        &vol_flags))
    {
        if (count == 0)
        {
            afp_registry vReg;

            vReg.get_volume_password(share_name, sizeof(first_password), first_password);
            first_flags = vol_flags;
        }

        count++;

        DBGWRITE(dbg_level_info, L"[SRVR]Adding volume to list (%s)\n", share_name);

        // See if the volume is still indexing. If so, then we append a
        // status message to the volume name.
        const auto afpVolumeInfo = g_volumes->find(share_name);
        if (afpVolumeInfo != nullptr)
        {
            if ((afpVolumeInfo->get_internal_flags() & flag_volume_indexing_complete) == 0)
            {
                // Indexing is not complete, so add the status message.
                LoadString(gh_instance, IDS_OFFLINEFORINDEXING, temp, sizeof(temp) / sizeof(WCHAR));
                [[maybe_unused]] const auto ignore = StringCbCat(share_name, sizeof(share_name), temp);
            }
        }

        SendDlgItemMessage(h_dlg, IDC_SHARES, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(share_name));
    }

    SendDlgItemMessage(h_dlg, IDC_SHARES, LB_SETCURSEL, 0, 0);

    if (first_flags & flag_volume_readonly)
    {
        CheckDlgButton(h_dlg, IDC_VOLREADONLY, TRUE);
    }
    else
    {
        CheckDlgButton(h_dlg, IDC_VOLREADONLY, FALSE);
    }
}


void set_misc_dialog_items(
    IN const HWND h_dlg
)
{
    BOOL allow_guest = true;
    BOOL encrypt = false;
    DWORD max_sessions = 0;
    afp_registry registry;

    if (registry.get_allow_guests_setting(&allow_guest) == NO_ERROR)
    {
        if (allow_guest)
        {
            CheckDlgButton(h_dlg, IDC_ALLOW_GUESTS, true);
        }
    }

    if (registry.get_force_encrypted_logon(&encrypt) == NO_ERROR)
    {
        if (encrypt)
        {
            CheckDlgButton(h_dlg, IDC_REQ_ENCRYPTION, true);
        }
    }

    registry.get_max_sessions(&max_sessions);
    SetDlgItemInt(h_dlg, IDC_MAX_CONNECTIONS, max_sessions, false);

    g_allow_guests = allow_guest;
    g_max_sessions = max_sessions;
}

void save_misc_settings(
    IN const HWND h_dlg
)
{
    afp_registry registry;
    BOOL translated;

    if (IsDlgButtonChecked(h_dlg, IDC_ALLOW_GUESTS))
    {
        registry.set_allow_guests_setting(true);
        g_allow_guests = true;
    }
    else
    {
        registry.set_allow_guests_setting(false);
        g_allow_guests = false;
    }

    if (IsDlgButtonChecked(h_dlg, IDC_REQ_ENCRYPTION))
    {
        registry.set_force_encrypted_logon(true);
        g_force_encrypted_logon = true;
    }
    else
    {
        registry.set_force_encrypted_logon(false);
        g_force_encrypted_logon = false;
    }

    const DWORD max_sessions = GetDlgItemInt(h_dlg, IDC_MAX_CONNECTIONS, &translated, false);
    registry.set_max_sessions(max_sessions);

    g_max_sessions = max_sessions;
}

void handle_ro_checkbox(
    IN const HWND h_dlg
)
{
    afp_registry registry;
    DWORD vol_flags = 0;

    LRESULT result = SendDlgItemMessage(h_dlg, IDC_SHARES, LB_GETCURSEL, 0, 0);
    if (result == LB_ERR)
    {
        return;
    }

    WCHAR share_name[64]{};

    result = SendDlgItemMessage(h_dlg, IDC_SHARES, LB_GETTEXT, result, reinterpret_cast<LPARAM>(share_name));
    if (result == LB_ERR)
    {
        return;
    }

    const auto afp_volume = g_volumes->find(share_name);
    if (registry.get_volume_flags(share_name, &vol_flags) == ERROR_SUCCESS && afp_volume != nullptr)
    {
        auto flags = afp_volume->get_internal_flags();

        // Note that we set 2 different flags. The reason we do this is because there
        // is a difference between flags in memory and flags on disk. Flags on disk must
        // ignore the first 2 bits in the bitmap.
        if (IsDlgButtonChecked(h_dlg, IDC_VOLREADONLY))
        {
            DBGWRITE(dbg_level_info, L"[MAIN]Setting RO to true\n");

            flags |= flag_volume_readonly;
            vol_flags |= flag_volume_readonly;
        }
        else
        {
            DBGWRITE(dbg_level_info, L"[MAIN]Setting RO to false\n");

            vol_flags &= ~flag_volume_readonly;
            flags &= ~flag_volume_readonly;
        }

        afp_volume->set_internal_flags(flags);
        registry.set_volume_flags(share_name, vol_flags);
    }
}

void server_shutdown(
    IN const HWND h_dlg
)
{
    WCHAR dlg_msg[512]{};
    WCHAR dlg_title[128]{};

    LoadString(gh_instance, IDS_STRING_SHUTDOWNMSG, dlg_msg, sizeof(dlg_msg));
    LoadString(gh_instance, IDS_STRING_SHUTDOWNCONFIRM, dlg_title, sizeof(dlg_title));

    if (const int dlg_result = MessageBox(h_dlg, dlg_msg, dlg_title, MB_OKCANCEL); dlg_result == IDCANCEL)
    {
        return;
    }

    dsi::dsi_shutdown();

    remove_tray_icon(h_dlg);

    EventWriteAFPServerEvt_ServerHasShutdown(L"");
    EventUnregisterAFPServer();

    PostQuitMessage(0);
}

BOOL CALLBACK main_dialog_proc(
    IN const HWND h_dlg,
    IN const UINT u_msg,
    IN const WPARAM w_param,
    IN const LPARAM l_param
)
{
    switch (u_msg)
    {
    case WM_INITDIALOG:
    {
        ShowWindow(h_dlg, SW_HIDE);
        initialize_tray_icon(h_dlg);

        const std::wstring version = string_to_wide_string(VER_FILE_SHORT_VERSION_STR);
        SendDlgItemMessage(h_dlg, IDC_VERSION_TEXT, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(version.c_str()));

        dsi::dsi_init();

        fill_logon_message_box(h_dlg);
        set_misc_dialog_items(h_dlg);
        break;
    }

    case WM_DESTROY:
        break;

    case WM_COMMAND:
        switch (LOWORD(w_param))
        {
        case IDC_QUITBUTTON:
            server_shutdown(h_dlg);
            break;

        case IDC_ABOUT:
            about_dialog(h_dlg);
            break;

        case IDC_CLOSE:
            save_logon_message(h_dlg);
            save_misc_settings(h_dlg);
            SendDlgItemMessage(h_dlg, IDC_SHARES, LB_RESETCONTENT, 0, 0);
            ShowWindow(h_dlg, SW_HIDE);

            g_main_window_visible = false;
            break;

        case IDC_NEWVOLUME:
            add_share(h_dlg);
            SendDlgItemMessage(h_dlg, IDC_SHARES, LB_RESETCONTENT, 0, 0);
            fill_volume_list_box(h_dlg);
            break;

        case IDC_REMOVEVOL:
            remove_volume(h_dlg);
            break;

        case IDC_VOLREADONLY:
            handle_ro_checkbox(h_dlg);
            break;

        case IDC_SENDMSG:
            send_message_from_ui(h_dlg);
            break;

        case IDC_SHARES:
            switch (HIWORD(w_param))
            {
            case LBN_SELCHANGE:
                update_vol_caption(h_dlg);
                return TRUE;

            default:
                break;
            }
            break;

        default:
            break;
        }
        break;

    // Respond to power state changes. Only do something if there
    // are active connections.
    case WM_POWERBROADCAST:
        switch (w_param)  // NOLINT(hicpp-multiway-paths-covered)
        {
        case PBT_APMQUERYSUSPEND:
            {
                if (active_connections.count() > 0)
                {
                    return (BROADCAST_QUERY_DENY);
                }
                return true;
            }

        default:
            break;
        }
        break;

    case AFPWM_NOTIFYICON:
        switch (l_param)  // NOLINT(hicpp-multiway-paths-covered)
        {
        case WM_LBUTTONDOWN:
            if (!g_main_window_visible)
            {
                fill_volume_list_box(h_dlg);
            }

            ShowWindow(h_dlg, SW_SHOW);
            SetForegroundWindow(h_dlg);
            update_vol_caption(h_dlg);

            g_main_window_visible = true;
            break;

        default:
            break;
        }
        break;

    case AFPWM_INDEXING_COMPLETE:
        if (g_main_window_visible)
        {
            SendDlgItemMessage(h_dlg, IDC_SHARES, LB_RESETCONTENT, 0, 0);
            fill_volume_list_box(h_dlg);
        }
        break;

    default:
        return FALSE;
    }

    return TRUE;
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
    g_main_window_visible = false;

    if (const ULONG status = EventRegisterAFPServer(); status != ERROR_SUCCESS)
    {
        DBGWRITE(dbg_level_warning, L"EventRegisterSG_AFP_Server_Provider() failed: %d\n", status);
    }

    EventWriteAFPServerEvt_ServerStarting(L"");

    [[maybe_unused]] HWND h_wnd = CreateDialog(
        hInstance,
        MAKEINTRESOURCE(IDD_MAIN_DIALOG),
        NULL,
        main_dialog_proc);

    while (GetMessage(&lp_msg, nullptr, 0, 0))
    {
        TranslateMessage(&lp_msg);
        DispatchMessage(&lp_msg);
    }

    return static_cast<int>(lp_msg.wParam);
}
