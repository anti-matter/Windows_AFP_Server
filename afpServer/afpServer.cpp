// afpServer.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "afpServer.h"

#include "afp.h"
#include "volumes.h"
#include "afpdebug.h"
#include "sendmsgdlg.h"
#include "addshare.h"
#include "aboutdlg.h"

#include "afp_registry.h"
#include "Resource.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <strsafe.h>

#include "afpEvents.h"
#include "afp_volumes.h"
#include "dsi/dsi_connections.h"
#include "dsi/dsi_init.h"

#undef	AFP_HASVOL_PASSWORD_SUPPORT

#define AFP_ICON_UID		IDI_TRAYICON
#define AFP_TOOL_TIP		TEXT("Personal AFP Server Configuration")

HINSTANCE ghInstance;
BOOL gMainWindowVisible;

extern CRITICAL_SECTION gVolumesCS;
extern std::unique_ptr<afp_volumes> g_volumes;
extern BOOL g_allow_guests;
extern DWORD g_max_sessions;
extern BOOL g_force_encrypted_logon;

extern dsi::dsi_connections active_connections;


void FillVolumeListBox(
    IN HWND hDlg
);

//---------------------------------------------------------------------
// InitializeTrayIcon()
//---------------------------------------------------------------------
// Initializes the tray icon. The tray icon is used to open our settings
// dialog and perform operations on the afp server.
//

void InitializeTrayIcon(
    IN HWND hDialog
)
{
    NOTIFYICONDATA nid;
    HICON hIcon;

    hIcon = static_cast<HICON>(LoadImage(
        ghInstance,
        MAKEINTRESOURCE(IDI_TRAYICON),
        IMAGE_ICON,
        32,
        32,
        0));

    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hDialog;
    nid.uID = AFP_ICON_UID;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = AFPWM_NOTIFYICON;
    nid.hIcon = hIcon;

    StringCbCopy(nid.szTip, sizeof(nid.szTip), AFP_TOOL_TIP);

    Shell_NotifyIcon(NIM_ADD, &nid);

    if (hIcon)
        DestroyIcon(hIcon);
}


//---------------------------------------------------------------------
// RemoveTrayIcon()
//---------------------------------------------------------------------
// We're shutting down, so remove the tray icon.
//

void RemoveTrayIcon(
    IN HWND hDialog
)
{
    NOTIFYICONDATA nid;

    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hDialog;
    nid.uID = AFP_ICON_UID;
    nid.uFlags = 0;

    Shell_NotifyIcon(NIM_DELETE, &nid);
}


//---------------------------------------------------------------------
// FillLogonMessageBox()
//---------------------------------------------------------------------
// Self explanatory.
//

void FillLogonMessageBox(
    IN HWND hDlg
)
{
    TCHAR szMessage[message_max_length + sizeof(TCHAR)];
    afp_registry theRegistry;

    //
    //Tell the edit box to only allow message_max_length characters
    //
    SendDlgItemMessage(hDlg, IDC_LOGIN_MSG, EM_LIMITTEXT, message_max_length, 0);

    if (theRegistry.get_logon_message(szMessage) == NO_ERROR)
    {
        //
        //Set our current logon message in the edit field.
        //
        SendDlgItemMessage(hDlg, IDC_LOGIN_MSG, WM_SETTEXT,
                           message_max_length, (LPARAM)szMessage);
    }
}


//---------------------------------------------------------------------
// UpdateVolCaption()
//---------------------------------------------------------------------
// The user clicked the volume list box, update the volume path info
// and such.
//

void UpdateVolCaption(HWND hParent)
{
    LRESULT dwResult = 0;
    TCHAR szShareName[MAX_PATH];

    //
    //First, send_ a message to the volume listbox to get the currently
    //selected cell.
    //
    dwResult = SendDlgItemMessage(hParent, IDC_SHARES, LB_GETCURSEL, 0, 0);

    if (dwResult != LB_ERR)
    {
        WCHAR szTempName[33];
        WCHAR szTempPath[MAX_PATH];
        WCHAR szPassword[16];
        afp_registry theRegistry;
        DWORD volFlags = 0;

        //Send a message to the volume list box and ask for the
        //string. This string is the full path to the share.
        //
        dwResult = SendDlgItemMessage(hParent, IDC_SHARES, LB_GETTEXT, dwResult, (LPARAM)szShareName);

        if (dwResult != LB_ERR)
        {
            if (theRegistry.open_volume_list_reg_key())
            {
                while (theRegistry.next_volume(
                    szTempName,
                    sizeof(szTempName),
                    szTempPath,
                    sizeof(szTempPath),
                    (PCHAR)szPassword,
                    sizeof(szPassword),
                    &volFlags))
                {
                    if (!lstrcmp(szTempName, szShareName))
                    {
                        SendDlgItemMessage(hParent, IDC_VOLPATH, WM_SETTEXT, 0, (LPARAM)szTempPath);

                        //
                        //See if the volume is set to read only. Set the checkbox
                        //to the correct value.
                        //
                        if (volFlags & flag_volume_readonly)
                        {
                            CheckDlgButton(hParent, IDC_VOLREADONLY, TRUE);
                        }
                        else
                        {
                            CheckDlgButton(hParent, IDC_VOLREADONLY, FALSE);
                        }

                        //
                        //Volume password support was removed in AppleShare 3.8. It
                        //does not exist in OS X either so we don't need to support
                        //it.
                        //
#ifdef AFP_HASVOL_PASSWORD_SUPPORT
                        //
                        //If there's a volume password, fill in the edit field.
                        //
                        memset(szPassword, 0, sizeof(szPassword));
                        
                        if (volFlags & FLAG_VOLUME_HASPASSWORD) {

                            theRegistry.GetVolumePassword(szShareName, sizeof(szPassword), szPassword);
                        }

                        SendDlgItemMessage(hParent, IDC_EDITPSWD, WM_SETTEXT, 0, (LPARAM)szPassword);

                        break;
#endif
                    }
                }
            }
        }
    }
}


//---------------------------------------------------------------------
// DoRemoveVolume()
//---------------------------------------------------------------------
// Put up UI so the user can add a new volume to the share list.
//

void DoRemoveVolume(HWND hParent)
{
    DWORD dwError = NO_ERROR;
    LRESULT dwResult = 0;
    int dlgResult = 0;
    WCHAR szDlgText[512];
    WCHAR szTxtBody[64];
    WCHAR szDlgTitle[64];
    WCHAR szSharename[MAX_PATH];

    //
    //First, send_ a message to the volume listbox to get the currently
    //selected cell.
    //
    dwResult = SendDlgItemMessage(hParent, IDC_SHARES, LB_GETCURSEL, 0, 0);

    //
    //If no cell is selected, LB_ERR is returned and we don't do anything.

    if (dwResult != LB_ERR)
    {
        //
        //Send a message to the volume list box and ask for the
        //string. This string is the name of the share.
        //
        dwResult = SendDlgItemMessage(hParent, IDC_SHARES, LB_GETTEXT, dwResult, (LPARAM)szSharename);

        if (dwResult == LB_ERR)
        {
            DBGWRITE(dbg_level_info, L"[MAIN]Bad list offset removing volume\n");
            return;
        }

        LoadString(ghInstance, IDS_STOPSHARINGWARN, szTxtBody, sizeof(szTxtBody) / sizeof(WCHAR));
        LoadString(ghInstance, IDS_STOPSHARINGCONFIRM, szDlgTitle, sizeof(szDlgTitle) / sizeof(WCHAR));

        StringCbPrintf(szDlgText, sizeof(szDlgText), L"%s %s", szTxtBody, szSharename);

        dlgResult = MessageBox(hParent, szDlgText, szDlgTitle, MB_OKCANCEL);

        if (dlgResult != IDCANCEL)
        {
            DBGWRITE(dbg_level_info, L"[MAIN]Removing volume: %s\n", szSharename);

            //
            //Call the volume API that will unshare the volume and remove the
            //volume from the registry.
            //
            dwError = AFPRemoveVolumeFromUI(szSharename);

            if (dwError != NO_ERROR)
            {
                LoadString(ghInstance, IDS_STOPSHARINGERR, szTxtBody, sizeof(szTxtBody) / sizeof(WCHAR));
                LoadString(ghInstance, IDS_STOPSHARINTERRTITLE, szDlgTitle, sizeof(szDlgTitle) / sizeof(WCHAR));

                MessageBox(hParent, szTxtBody, szDlgTitle, MB_OK);

                DBGWRITE(dbg_level_info, L"[MAIN]Removing volume failed! (%d)\n", dwError);
                return;
            }

            //
            //Refreshing the volume list box will remove the item from
            //the list easily.
            //
            SendDlgItemMessage(hParent, IDC_SHARES, LB_RESETCONTENT, 0, 0);
            FillVolumeListBox(hParent);
        }
    }
}


//---------------------------------------------------------------------
// SaveLogonMessage()
//---------------------------------------------------------------------
// Self explanatory.
//

void SaveLogonMessage(
    IN HWND hDlg
)
{
    TCHAR szMessage[message_max_length + sizeof(TCHAR)];
    afp_registry theRegistry;

    //
    //Set our current logon message in the edit field.
    //
    SendDlgItemMessage(hDlg, IDC_LOGIN_MSG, WM_GETTEXT,
                       message_max_length, (LPARAM)szMessage);

    theRegistry.save_logon_message(szMessage);
}


//---------------------------------------------------------------------
// FillVolumeListBox()
//---------------------------------------------------------------------
// Returns the number of volumes the server is sharing.
//

void FillVolumeListBox(
    IN HWND hDlg
)
{
    TCHAR szShareName[64] = {0};
    TCHAR szTemp[64] = {0};
    TCHAR szSharePath[MAX_PATH] = {0};
    afp_clear_text_password szPassword = {0};
    WCHAR firstPassword[16] = {0};
    DWORD volFlags = 0;
    DWORD firstFlags = 0;
    afp_registry theRegistry;
    WORD count = 0;

    SendDlgItemMessage(hDlg, IDC_SHARES, LB_SETCURSEL, 0, 0);

    memset(firstPassword, 0, sizeof(firstPassword));

    if (theRegistry.open_volume_list_reg_key())
    {
        while (theRegistry.next_volume(
            szShareName,
            sizeof(szShareName),
            szSharePath,
            sizeof(szSharePath),
            reinterpret_cast<PCHAR>(szPassword),
            sizeof(szPassword),
            &volFlags))
        {
            if (count == 0)
            {
                afp_registry vReg;

                vReg.get_volume_password(szShareName, sizeof(firstPassword), firstPassword);
                firstFlags = volFlags;
            }

            count++;

            DBGWRITE(dbg_level_info, L"[SRVR]Adding volume to list (%s)\n", szShareName);

            //
            //See if the volume is still indexing. If so, then we append a
            //status message to the volume name.
            //
            const auto afpVolumeInfo = g_volumes->find(szShareName);
            if (afpVolumeInfo != nullptr)
            {
                if ((afpVolumeInfo->get_internal_flags() & flag_volume_indexing_complete) == 0)
                {
                    //
                    //Indexing is not complete, so add the status message.
                    //
                    LoadString(ghInstance, IDS_OFFLINEFORINDEXING, szTemp, sizeof(szTemp) / sizeof(TCHAR));
                    StringCbCat(szShareName, sizeof(szShareName), szTemp);
                }
            }

            //
            //Now add the volume to our volume index and initialize it.
            //
            SendDlgItemMessage(hDlg, IDC_SHARES, LB_ADDSTRING, 0, (LPARAM)szShareName);
        }

        SendDlgItemMessage(hDlg, IDC_SHARES, LB_SETCURSEL, 0, 0);

        if (firstFlags & flag_volume_readonly)
        {
            CheckDlgButton(hDlg, IDC_VOLREADONLY, TRUE);
        }
        else
        {
            CheckDlgButton(hDlg, IDC_VOLREADONLY, FALSE);
        }

#ifdef AFP_HASVOL_PASSWORD_SUPPORT
        SendDlgItemMessage(hDlg, IDC_EDITPSWD, WM_SETTEXT, 0, (LPARAM)firstPassword);
#endif
    }
}


//---------------------------------------------------------------------
// SetMiscDialogItems()
//---------------------------------------------------------------------
// Self explanatory.
//

void SetMiscDialogItems(
    IN HWND hDlg
)
{
    BOOL fAllowGuest = true;
    BOOL fEncrypt = false;
    DWORD dwMaxSessions = 0;
    afp_registry theRegistry;

    if (theRegistry.get_allow_guests_setting(&fAllowGuest) == NO_ERROR)
    {
        if (fAllowGuest)
        {
            CheckDlgButton(hDlg, IDC_ALLOW_GUESTS, true);
        }
    }

    if (theRegistry.get_force_encrypted_logon(&fEncrypt) == NO_ERROR)
    {
        if (fEncrypt)
        {
            CheckDlgButton(hDlg, IDC_REQ_ENCRYPTION, true);
        }
    }

    theRegistry.get_max_sessions(&dwMaxSessions);
    SetDlgItemInt(hDlg, IDC_MAX_CONNECTIONS, dwMaxSessions, false);

    g_allow_guests = fAllowGuest;
    g_max_sessions = dwMaxSessions;
}


//---------------------------------------------------------------------
// SaveGuestSettings()
//---------------------------------------------------------------------
// Self explanatory.
//

void SaveMiscSettings(
    IN HWND hDlg
)
{
    afp_registry theRegistry;
    DWORD dwMaxSessions;
    BOOL fTranslated;

    if (IsDlgButtonChecked(hDlg, IDC_ALLOW_GUESTS))
    {
        theRegistry.set_allow_guests_setting(true);
        g_allow_guests = true;
    }
    else
    {
        theRegistry.set_allow_guests_setting(false);
        g_allow_guests = false;
    }

    if (IsDlgButtonChecked(hDlg, IDC_REQ_ENCRYPTION))
    {
        theRegistry.set_force_encrypted_logon(true);
        g_force_encrypted_logon = true;
    }
    else
    {
        theRegistry.set_force_encrypted_logon(false);
        g_force_encrypted_logon = false;
    }

    dwMaxSessions = GetDlgItemInt(hDlg, IDC_MAX_CONNECTIONS, &fTranslated, false);
    theRegistry.set_max_sessions(dwMaxSessions);

    g_max_sessions = dwMaxSessions;
}


#ifdef AFP_HASVOL_PASSWORD_SUPPORT
//---------------------------------------------------------------------
// SaveVolPassword()
//---------------------------------------------------------------------
// Save the volume password.
//
void SaveVolPassword(
    IN	HWND	hDlg
)
{
    WCHAR				pass[16];
    WCHAR				share[64];
    AFPRegistry			theRegistry;
    DWORD				dwResult;
    DWORD				volFlags;
    AFP_VOLUME_INFO*	afpVolume	= NULL;

    //
    //First, send_ a message to the volume listbox to get the currently
    //selected cell.
    //
    dwResult = SendDlgItemMessage(hDlg, IDC_SHARES, LB_GETCURSEL, 0, 0);

    if (dwResult != LB_ERR)
    {
        //
        //Send a message to the volume list box and ask for the
        //string. This string is the share name.
        //
        dwResult = SendDlgItemMessage(hDlg, IDC_SHARES, LB_GETTEXT, dwResult, (LPARAM)share);

        if ((dwResult != LB_ERR) && (AFPGetVolumeInfoBlock(share, 0, &afpVolume)))
        {
            SendDlgItemMessage(hDlg, IDC_EDITPSWD, WM_GETTEXT, (WPARAM)sizeof(pass), (LPARAM)pass);
            theRegistry.SetVolumePassword(share, pass);

            theRegistry.GetVolumeFlags(share, &volFlags);

            if (lstrlen(pass) > 0)
            {
                volFlags				|= FLAG_VOLUME_HASPASSWORD;
                afpVolume->wVolumeFlags |= FLAG_VOLUME_HASPASSWORD;
            }
            else
            {
                volFlags				&= ~FLAG_VOLUME_HASPASSWORD;
                afpVolume->wVolumeFlags	&= ~FLAG_VOLUME_HASPASSWORD;
            }

            theRegistry.SetVolumeFlags(share, volFlags);
        }
    }
}
#endif

//---------------------------------------------------------------------
// HandleROCheckbox()
//---------------------------------------------------------------------
// Handle the volume read only checkbox.
//

void HandleROCheckbox(
    IN HWND hDlg
)
{
    afp_registry theRegistry;
    WCHAR szShareName[64];
    LRESULT dwResult = 0;
    DWORD volFlags = 0;

    //
    //First, send_ a message to the volume listbox to get the currently
    //selected cell.
    //
    dwResult = SendDlgItemMessage(hDlg, IDC_SHARES, LB_GETCURSEL, 0, 0);

    if (dwResult != LB_ERR)
    {
        //
        //Send a message to the volume list box and ask for the
        //string. This string is the share name.
        //
        dwResult = SendDlgItemMessage(hDlg, IDC_SHARES, LB_GETTEXT, dwResult, (LPARAM)szShareName);

        if (dwResult != LB_ERR)
        {
            const auto afpVolume = g_volumes->find(szShareName);
            if (theRegistry.get_volume_flags(szShareName, &volFlags) == ERROR_SUCCESS &&
                afpVolume != nullptr)
            {
                auto flags = afpVolume->get_internal_flags();

                //
                //Note that we set 2 different flags. The reason we do this is because there
                //is a difference between flags in memory and flags on disk. Flags on disk must
                //ignore the first 2 bits in the bitmap.
                //
                if (IsDlgButtonChecked(hDlg, IDC_VOLREADONLY))
                {
                    DBGWRITE(dbg_level_info, L"[MAIN]Setting RO to true\n");

                    flags |= flag_volume_readonly;
                    volFlags |= flag_volume_readonly;
                }
                else
                {
                    DBGWRITE(dbg_level_info, L"[MAIN]Setting RO to false\n");

                    volFlags &= ~flag_volume_readonly;
                    flags &= ~flag_volume_readonly;
                }

                afpVolume->set_internal_flags(flags);
                theRegistry.set_volume_flags(szShareName, volFlags);
            }
        }
    }
}


//---------------------------------------------------------------------
// ServerShutdown()
//---------------------------------------------------------------------
// The user is requesting shutdown. Do it only after verifying that's
// what they want to do.
//

void ServerShutdown(
    IN HWND hDlg
)
{
    int dlgResult = 0;
    WCHAR szDlgMsg[512];
    WCHAR szDlgTitle[128];

    LoadString(ghInstance, IDS_STRING_SHUTDOWNMSG, szDlgMsg, sizeof(szDlgMsg));
    LoadString(ghInstance, IDS_STRING_SHUTDOWNCONFIRM, szDlgTitle, sizeof(szDlgTitle));

    dlgResult = MessageBox(hDlg, szDlgMsg, szDlgTitle, MB_OKCANCEL);

    if (dlgResult != IDCANCEL)
    {
        //
        //Shutdown the afp server by calling a routine that will
        //wait until shutdown is complete.
        //
        dsi::dsi_shutdown();

        //
        //We're shutting down, so remove the tray icon from the
        //taskbar at this time.
        //
        RemoveTrayIcon(hDlg);

        //
        //Make the final event log entry that we're going away.
        //
        EventWriteAFPServerEvt_ServerHasShutdown(L"");

        //
        //Unregister our events provider, found in SGAFPServerEvents.h
        //
        EventUnregisterAFPServer();

        //
        //Send the app quit message from here.
        //
        PostQuitMessage(0);
    }
}


//---------------------------------------------------------------------
// MainDialogProc()
//---------------------------------------------------------------------
// Self explanatory.
//

BOOL CALLBACK MainDialogProc(
    IN HWND hDlg,
    IN UINT uMsg,
    IN WPARAM wParam,
    IN LPARAM lParam
)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
        ShowWindow(hDlg, SW_HIDE);
        InitializeTrayIcon(hDlg);

        dsi::dsi_init();

        FillLogonMessageBox(hDlg);
        SetMiscDialogItems(hDlg);
        break;

    case WM_DESTROY:
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_QUITBUTTON:
            ServerShutdown(hDlg);
            break;

        case IDC_ABOUT:
            AboutDialog(hDlg);
            break;

        case IDC_CLOSE:
            SaveLogonMessage(hDlg);
            SaveMiscSettings(hDlg);
            SendDlgItemMessage(hDlg, IDC_SHARES, LB_RESETCONTENT, 0, 0);
            ShowWindow(hDlg, SW_HIDE);

        //
        //Signal that the window is now closed so we'll draw into
        //it the next time.
        //
            gMainWindowVisible = false;
            break;

        case IDC_NEWVOLUME:
            AddShare(hDlg);
            SendDlgItemMessage(hDlg, IDC_SHARES, LB_RESETCONTENT, 0, 0);
            FillVolumeListBox(hDlg);
            break;

        case IDC_REMOVEVOL:
            DoRemoveVolume(hDlg);
            break;

#ifdef AFP_HASVOL_PASSWORD_SUPPORT
                case IDC_SETPASSWORD:
                    SaveVolPassword(hDlg);
                    break;
#endif

        case IDC_VOLREADONLY:
            HandleROCheckbox(hDlg);
            break;

        case IDC_SENDMSG:
            SendMessageFromUI(hDlg);
            break;

        case IDC_SHARES:
            switch (HIWORD(wParam))
            {
            case LBN_SELCHANGE:
                UpdateVolCaption(hDlg);
                return (TRUE);
                break;

            case LBN_DBLCLK:
                break;

            default:
                break;
            }
            break;

        default:
            break;
        }
        break;

    //
    //Respond to power state changes. Only do something if there
    //are active connections.
    //
    case WM_POWERBROADCAST:
        switch (wParam)
        {
        case PBT_APMQUERYSUSPEND:
            {
                if (active_connections.count() > 0)
                {
                    return (BROADCAST_QUERY_DENY);
                }
                return (true);
            }
            break;

        default:
            break;
        }
        break;

    case AFPWM_NOTIFYICON:
        switch (lParam)
        {
        case WM_LBUTTONDOWN:
            if (!gMainWindowVisible)
            {
                //
                //Populate the volume list box with the volumes
                //currently begin shared.
                //
                FillVolumeListBox(hDlg);
            }

        //
        //Show the window which was hidden, we always want to do this
        //as if the user clicked in the tray again, we want to make
        //sure the window is selected.
        //
            ShowWindow(hDlg, SW_SHOW);
            SetForegroundWindow(hDlg);
            UpdateVolCaption(hDlg);

        //
        //Signal that the window is visible so multiple clicks in
        //the tray icon won't cause weird things to happend in the
        //window (like listing volumes twice).
        //
            gMainWindowVisible = true;
            break;

        case WM_RBUTTONDOWN:
            break;

        default:
            break;
        }
        break;

    case AFPWM_INDEXING_COMPLETE:
        if (gMainWindowVisible)
        {
            //
            //We reset and refresh the volume list box  since one of
            //the volumes has completed indexing.
            //
            SendDlgItemMessage(hDlg, IDC_SHARES, LB_RESETCONTENT, 0, 0);
            FillVolumeListBox(hDlg);
        }
        break;

    default:
        return (FALSE);
    }

    return (TRUE);
}


//---------------------------------------------------------------------
// WinMain()
//---------------------------------------------------------------------
// Self explanatory.
//

int APIENTRY WinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPSTR lpCmdLine,
                     int nCmdShow)
{
    UNREFERENCED_PARAMETER(nCmdShow);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(hPrevInstance);

    MSG lpMsg;

    ULONG status = ERROR_SUCCESS;

    ghInstance = hInstance;
    gMainWindowVisible = false;

    //
    //Register our event provider, this is located in SGAFPServerEvents.h
    //
    status = EventRegisterAFPServer();
    if (status != ERROR_SUCCESS)
    {
        DBGWRITE(dbg_level_warning, L"EventRegisterSG_AFP_Server_Provider() failed: %d\n", status);
    }

    //
    //Log that we are starting up for auditing purposes.
    //
    EventWriteAFPServerEvt_ServerStarting(L"");

    [[maybe_unused]] HWND h_wnd = CreateDialog(
        hInstance,
        MAKEINTRESOURCE(IDD_MAIN_DIALOG),
        NULL,
        MainDialogProc);

    while (GetMessage(&lpMsg, nullptr, 0, 0))
    {
        TranslateMessage(&lpMsg);
        DispatchMessage(&lpMsg);
    }
    return static_cast<int>(lpMsg.wParam);
}
