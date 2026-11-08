#include "StdAfx.h"
#include "afp.h"
#include "afp_session.h"
#include "logonimp.h"
#include "afpEvents.h"

#include <ntsecapi.h>

#include "windows_security.h"


AFPERROR afp_imp_user_logon(
    dsi::dsi_connection* connection,
    const afp_net_buffer& afp_reply,
    std::wstring& afp_password_w
)
{
    UNREFERENCED_PARAMETER(afp_reply);

    HANDLE token = nullptr;
    AFPERROR afp_error = AFP_OK;
    const std::wstring user_name = connection->get_user_name();
    const std::wstring domain_name = connection->get_domain_name();

    DBGWRITE(dbg_level_info, L"LogonUser for [%s]\\[%s] (guest session: %d)\n",
             domain_name.c_str(), user_name.c_str(), connection->get_is_guest_session());

    if (LogonUser(
        user_name.c_str(),
        domain_name.length() > 0 ? domain_name.c_str() : nullptr,
        afp_password_w.c_str(),
        LOGON32_LOGON_NETWORK,
        LOGON32_PROVIDER_DEFAULT,
        &token) == 0)
    {
        // Capture once: DBGWRITE below makes API calls that would clobber
        // the extended error code.
        const DWORD logon_error = GetLastError();

        DBGWRITE(dbg_level_warning, L"LogonUser() failed for [%s]\\[%s] (%d)\n",
                 domain_name.c_str(), user_name.c_str(), logon_error);

        if (connection->get_is_guest_session())
        {
            // The guest path logs on the built-in Guest account with its
            // blank password; the usual blockers are pure configuration,
            // so tell the administrator exactly what to change.
            switch (logon_error)
            {
            case ERROR_ACCOUNT_DISABLED:
                DBGWRITE(dbg_level_warning,
                         L"Guest login failed: the built-in Guest account [%s] is disabled. "
                         L"Enable it with \"net user %s /active:yes\" (or disallow guests).\n",
                         user_name.c_str(), user_name.c_str());
                break;

            case ERROR_LOGON_TYPE_NOT_GRANTED:
                DBGWRITE(dbg_level_warning,
                         L"Guest login failed: Windows denies the Guest account network logon. "
                         L"Grant it \"Access this computer from the network\" and remove it from "
                         L"\"Deny access to this computer from the network\" (secpol.msc, User Rights Assignment).\n");
                break;

            default:
                DBGWRITE(dbg_level_warning,
                         L"Guest login failed (%d).\n", logon_error);
                break;
            }
        }

        // If we had a failure to logon, close the session down.
        //connection->kill_connection();

        afp_error = afp_map_win_error_to_afp(logon_error, afpUserNotAuth);

        return afp_error;
    }

    // Scramble and release the password buffer for security purposes
    // (clear() alone would leave the characters behind in freed heap).
    if (!afp_password_w.empty())
    {
        SecureZeroMemory(&afp_password_w[0], afp_password_w.size() * sizeof(WCHAR));
    }
    afp_password_w.clear();

    HANDLE dupe_handle = nullptr;
    if (!DuplicateTokenEx(token, MAXIMUM_ALLOWED, nullptr,
                          SecurityImpersonation, TokenPrimary, &dupe_handle))
    {
        const DWORD dupe_error = GetLastError();
        DBGWRITE(dbg_level_error, L"DuplicateTokenEx() failed! (%d)\n", dupe_error);

        CloseHandle(token);

        //Without a usable impersonation token the session has no
        //identity to enforce access with; failing the login is the
        //only safe option (continuing would mark the session
        //authenticated with a null token).
        return afp_map_win_error_to_afp(dupe_error, afpMiscErr);
    }

    connection->set_user_token(dupe_handle);

    RevertToSelf();
    CloseHandle(token);

    // ReSharper disable once CppStringLiteralToCharPointerConversion
    adjust_privilege(const_cast<wchar_t*>(SE_IMPERSONATE_NAME), TRUE, dupe_handle);

    connection->set_session_authenticated(true);

    DBGWRITE(dbg_level_info, L"Login for user %s successfull!\n", user_name.c_str());

    // If we get here, then everything succeeded. We'll log an event
    // notifying the administrator that this user has logged on.

    EventWriteAFPServerEvt_ClientLoggedOn(user_name.c_str());

    return afp_error;
}
