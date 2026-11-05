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

    if (LogonUser(
        user_name.c_str(),
        domain_name.length() > 0 ? domain_name.c_str() : nullptr,
        afp_password_w.c_str(),
        LOGON32_LOGON_NETWORK,
        LOGON32_PROVIDER_DEFAULT,
        &token) == 0)
    {
        DBGWRITE(dbg_level_info, L"LogonUser() failed (%d)\n", GetLastError());

        // If we had a failure to logon, close the session down.
        //connection->kill_connection();

        afp_error = afp_map_win_error_to_afp(GetLastError(), afpUserNotAuth);

        return afp_error;
    }

    // Clear out the password buffer for security purposes.
    afp_password_w.clear();

    HANDLE dupe_handle = nullptr;
    if (!DuplicateTokenEx(token, MAXIMUM_ALLOWED, nullptr,
                          SecurityImpersonation, TokenPrimary, &dupe_handle))
    {
        DBGWRITE(dbg_level_info, L"DuplicateToken() failed! (%d)\n", GetLastError());
        ASSERT(0);
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
