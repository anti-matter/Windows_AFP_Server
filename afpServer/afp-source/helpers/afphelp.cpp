// ReSharper disable CppClangTidyClangDiagnosticGnuZeroVariadicMacroArguments
#include "StdAfx.h"

#include <windows.h>
#include <winerror.h>
#include <Aclapi.h>
#include <Ntsecapi.h>
#include <winnt.h>

#include "afphelp.h"

#include <codecvt>
#include <shlobj_core.h>

#include "afpdebug.h"
#include "afp.h"
#include "users_and_groups_cache.h"

users_and_groups_cache g_user_and_group_cache;
DWORD g_next_user_group_id = 1;

std::string wide_string_to_string(const std::wstring& wide_string, const UINT code_page)
{
    if (wide_string.empty())
    {
        return "";
    }

    const auto size_needed = WideCharToMultiByte(code_page, 0, &wide_string.at(0), static_cast<int>(wide_string.size()),
                                                 nullptr, 0, nullptr, nullptr);
    if (size_needed <= 0)
    {
        throw std::runtime_error("WideCharToMultiByte() failed: " + std::to_string(size_needed));
    }

    std::string result(size_needed, 0);
    WideCharToMultiByte(code_page, 0, &wide_string.at(0), static_cast<int>(wide_string.size()), &result.at(0),
                        size_needed, nullptr, nullptr);
    return result;
}

std::wstring string_to_wide_string(const std::string& string, const UINT code_page)
{
    if (string.empty())
    {
        return L"";
    }

    const auto size_needed = MultiByteToWideChar(code_page, 0, &string.at(0), static_cast<int>(string.size()), nullptr,
                                                 0);
    if (size_needed <= 0)
    {
        throw std::runtime_error("MultiByteToWideChar() failed: " + std::to_string(size_needed));
    }

    std::wstring result(size_needed, 0);
    MultiByteToWideChar(code_page, 0, &string.at(0), static_cast<int>(string.size()), &result.at(0), size_needed);
    return result;
}

AFPERROR afp_get_user_name_by_id(
    IN users_and_group_name_type type,
    IN const DWORD id,
    IN const PWCHAR name
)
{
    if (const auto user_and_group = g_user_and_group_cache.find_by_id(id); user_and_group != nullptr)
    {
        lstrcpyn(name, user_and_group->name.c_str(), static_cast<int>(user_and_group->name.size() + 1));
        return AFP_OK;
    }

    return afpParmErr;
}

DWORD afp_get_directory_owner_and_group_id(
    IN const std::filesystem::path& dir_win_path,
    IN const std::wstring& logged_on_user_name,
    OUT DWORD* owner_id,
    OUT DWORD* group_id
)
{
    PSID sid_group = nullptr;
    PSID sid_owner = nullptr;
    PSECURITY_DESCRIPTOR security_descriptor = nullptr;

    ASSERT(owner_id != nullptr);
    ASSERT(group_id != nullptr);

    // Initialize ID's to zero, this means no owner or group assigned.
    *owner_id = 0;
    *group_id = 0;

    // First step is to get the security descriptor and SIDs for
    // the directory.
    DWORD error = GetNamedSecurityInfo(
        dir_win_path.c_str(),
        SE_FILE_OBJECT,
        GROUP_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION,
        &sid_owner,
        &sid_group,
        nullptr,
        nullptr,
        &security_descriptor
    );

    if (FAILED(error) || security_descriptor == nullptr)
    {
        DBGWRITE(dbg_level_error, L"GetNamedSecurityInfo failed getting owner and group info for %s, error code: %d\n",
                 dir_win_path.c_str(),
                 error
        );

        LocalFree(security_descriptor);

        return error;
    }

    constexpr int max_name = 256;

    WCHAR account_name[max_name];
    WCHAR domain_name[max_name];
    DWORD cch_name = max_name;
    DWORD cch_domain_name = max_name;
    DWORD sid_type = 0;

    // First we get the name of the owner of the directory.
    auto succeeded = LookupAccountSid(
        nullptr,
        sid_owner,
        account_name,
        &cch_name,
        domain_name,
        &cch_domain_name,
        reinterpret_cast<PSID_NAME_USE>(&sid_type)
    );

    if (!succeeded)
    {
        // We failed to get the owner name.
        error = GetLastError();
        DBGWRITE(dbg_level_error, L"LookupAccountSid failed getting owner name for [%s], error code: %d\n",
                 dir_win_path.c_str(),
                 error
        );

        LocalFree(security_descriptor);

        return error;
    }

    // We successfully got the owner name from the directory SID. Now we
    // need to convert it to an AFP user ID.
    DBGWRITE(dbg_level_info, L"Owner name from SID = %s for dir %s\n", account_name, dir_win_path.c_str());

    // We only care about user SID's, check to make sure that's what we got.
    if (sid_type == SidTypeUser || sid_type == SidTypeAlias)
    {
        *owner_id = g_next_user_group_id++;
        g_user_and_group_cache.add(afp_user_name_type, account_name, *owner_id);

        DBGWRITE(dbg_level_info, L"Added username %s to the cache\n", account_name);
    }

    cch_name = max_name;
    cch_domain_name = max_name;

    // Get the name of the group that owns the directory.
    succeeded = LookupAccountSid(
        nullptr,
        sid_group,
        account_name,
        &cch_name,
        domain_name,
        &cch_domain_name,
        reinterpret_cast<PSID_NAME_USE>(&sid_type)
    );

    if (!succeeded)
    {
        // We failed to get the group name. Report it but only in debug.
        error = GetLastError();
        DBGWRITE(dbg_level_info, L"****Error getting group name! (%d)\n", error);

        LocalFree(security_descriptor);

        return error;
    }

    // We successfully got the group name from the directory SID. Now we
    // need to convert it to an AFP group ID.
    DBGWRITE(dbg_level_trace, L"Group name from SID = %s for dir %s\n", account_name, dir_win_path.c_str());

    // We only care about group SID's
    if (sid_type == SidTypeGroup)
    {
        *group_id = g_next_user_group_id++;
        g_user_and_group_cache.add(afp_group_name_type, account_name, *group_id);

        DBGWRITE(dbg_level_info, L"Added groupname %s to the cache\n", account_name);
    }
    else if (sid_type == SidTypeUser || sid_type == SidTypeAlias)
    {
        const auto user = g_user_and_group_cache.find_by_name(account_name);
        if (user == nullptr)
        {
            *group_id = g_next_user_group_id++;
            g_user_and_group_cache.add(afp_user_name_type, account_name, *group_id);
        }
        else
        {
            *group_id = user->id;
        }
    }

    LocalFree(security_descriptor);

    g_user_and_group_cache.add(afp_user_name_type, logged_on_user_name, g_next_user_group_id++);

    return error;
}

/*
 * afp_extract_domain_and_user_name()
 *
 * Description:
 *  Extracts the domain and user name from a string. The string can
 *  consist of "domain\username" or just "username". *
 *
 *	Return Value: none
 */
void afp_extract_domain_and_user_name(
    IN std::wstring& user_name,
    OUT std::wstring& domain_name
)
{
    if (user_name.empty())
    {
        return;
    }

    const std::size_t pos = user_name.find(L'\\');
    if (pos == std::wstring::npos)
    {
        // No domain name, leave everything untouched.
        return;
    }

    domain_name = user_name.substr(0, pos);
    user_name = user_name.substr(pos + 1);
}

ULONG afp_windows_to_mac_time(
    IN const PFILETIME file_time
)
{
    ULARGE_INTEGER seconds;
    ULARGE_INTEGER time;
    LARGE_INTEGER seconds_to_start_of1980;

    seconds_to_start_of1980.LowPart = 0xc8df3700;
    seconds_to_start_of1980.HighPart = 0x00000002;

    time.LowPart = file_time->dwLowDateTime;
    time.HighPart = file_time->dwHighDateTime;

    seconds.QuadPart = time.QuadPart / num_100_ns_per_second;
    seconds.QuadPart = seconds.QuadPart - seconds_to_start_of1980.QuadPart;

    return seconds.LowPart - seconds_from_1980_2000;
}

ULONG afp_windows_to_mac_local_time(
    IN const PFILETIME file_time
)
{
    LARGE_INTEGER local_time;
    LARGE_INTEGER seconds;
    LARGE_INTEGER seconds_to_start_of1980;

    seconds_to_start_of1980.LowPart = 0xc8df3700;
    seconds_to_start_of1980.HighPart = 0x00000002;

    FileTimeToLocalFileTime(file_time, reinterpret_cast<PFILETIME>(&local_time));

    seconds.QuadPart = local_time.QuadPart / num_100_ns_per_second;
    seconds.QuadPart = seconds.QuadPart - seconds_to_start_of1980.QuadPart;

    return seconds.LowPart - seconds_from_1980_2000;
}

void afp_mac_to_windows_time(
    IN ULONG mac_time,
    IN const PFILETIME file_time
)
{
    LARGE_INTEGER local_time;
    LARGE_INTEGER seconds_to_start_of1980;

    seconds_to_start_of1980.LowPart = 0xc8df3700;
    seconds_to_start_of1980.HighPart = 0x00000002;

    mac_time += seconds_from_1980_2000;

    local_time.QuadPart = mac_time + seconds_to_start_of1980.QuadPart;
    local_time.QuadPart = local_time.QuadPart * num_100_ns_per_second;

    file_time->dwLowDateTime = local_time.LowPart;
    file_time->dwHighDateTime = local_time.HighPart;
}

void afp_mac_to_windows_local_time(
    IN const ULONG mac_time,
    IN const PFILETIME file_time
)
{
    FILETIME utc_time;

    afp_mac_to_windows_time(mac_time, &utc_time);
    FileTimeToLocalFileTime(&utc_time, file_time);
}

BOOL open_current_token(
    IN OUT const PHANDLE token
)
{
    //
    //Attempt to open a thread token first since it take precedence
    //over a process token for most operations.
    //
    if (!OpenThreadToken(
        GetCurrentThread(),
        TOKEN_ADJUST_PRIVILEGES,
        FALSE,
        token))
    {
        //
        //Couldn't open a thread token, so now open one for
        //the process.
        //
        if (!OpenProcessToken(
            GetCurrentProcess(),
            TOKEN_ADJUST_PRIVILEGES,
            token))
        {
            DBGWRITE(dbg_level_info, L"[SEC]OpenProcessToken() failed! (%d)\n", GetLastError());
            return FALSE;
        }
    }

    return TRUE;
}

BOOL afp_adjust_privilege(
    IN const PWCHAR privilege,
    IN const BOOL enable,
    IN const HANDLE h_token
)
{
    auto token = INVALID_HANDLE_VALUE;
    LUID luid;
    TOKEN_PRIVILEGES privileges;

    if (!LookupPrivilegeValue(nullptr, privilege, &luid))
    {
        DBGWRITE(dbg_level_error, L"LookupPrivilegeValue() failed for %s! (%d)\n", privilege, GetLastError());
        return FALSE;
    }

    // If hToken was not supplied, then we get the token for the
    // currently running thread.
    if (h_token == INVALID_HANDLE_VALUE)
    {
        if (!open_current_token(&token))
        {
            DBGWRITE(dbg_level_error, L"OpenCurrentToken() failed! (%d)\n", GetLastError());
            return FALSE;
        }
    }
    else
    {
        token = h_token;
    }

    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Luid = luid;
    privileges.Privileges[0].Attributes = enable ? SE_PRIVILEGE_ENABLED : 0;

    //
    //Here's the real API call that adjusts the privileges.
    //
    if (!AdjustTokenPrivileges(
        token,
        FALSE,
        &privileges,
        0,
        nullptr,
        nullptr))
    {
        const DWORD error = GetLastError();

        DBGWRITE(dbg_level_error, L"AdjustTokenPrivileges() failed! (%d)\n", error);

        //
        //If we have an invalid handle, we don't want to make a call on
        //it at this stage or we'll die. Only close if we created the token
        //handle locally.
        //
        if (error != ERROR_INVALID_HANDLE && h_token == INVALID_HANDLE_VALUE)
        {
            CloseHandle(token);
        }

        return FALSE;
    }

    DBGWRITE(dbg_level_trace, L"AdjustTokenPrivileges() succeeded for %s\n", privilege);

    // We only want to close the Token handle if we created it
    // locally with OpenCurrentToken().
    if (h_token == INVALID_HANDLE_VALUE)
    {
        CloseHandle(token);
    }

    return TRUE;
}

BOOL afp_adjust_account_right(
    IN const HANDLE h_user_token,
    const PWCHAR privileges,
    const USHORT cb_privileges //count in bytes the size of the privileges buffer
)
{
    UNREFERENCED_PARAMETER(h_user_token);

    LSA_OBJECT_ATTRIBUTES object_attributes;
    LSA_UNICODE_STRING user_rights;
    auto policy_handle = INVALID_HANDLE_VALUE;

    // First open a policy object for the local system.
    ZeroMemory(&object_attributes, sizeof(object_attributes));

    auto status = LsaOpenPolicy(nullptr, &object_attributes, POLICY_LOOKUP_NAMES | POLICY_CREATE_ACCOUNT,
                                &policy_handle);
    if (status == ERROR_SUCCESS)
    {
        const PSID account_sid = nullptr;
        ZeroMemory(&user_rights, sizeof(user_rights));

        // Build the LSA unicode string structure
        user_rights.Length = static_cast<USHORT>(lstrlen(privileges));
        user_rights.MaximumLength = cb_privileges;
        user_rights.Buffer = privileges;

        status = LsaAddAccountRights(
            policy_handle,
            account_sid,
            &user_rights,
            1
        );

        // ReSharper disable once CppFunctionResultShouldBeUsed
        LsaClose(policy_handle);

        if (status != ERROR_SUCCESS)
        {
            DBGWRITE(dbg_level_info, L"LsaAddAccountRights() failed %lu\n",
                     LsaNtStatusToWinError(status));
            return FALSE;
        }
    }
    else
    {
        DBGWRITE(dbg_level_info, L"LsaOpenPolicy() failed %lu\n",
                 LsaNtStatusToWinError(status));
        return FALSE;
    }

    return TRUE;
}


/*
 * afp_impersonate_user()
 *
 * Description:
 *  Workhorse routine for impersonating a logged in client. We have to
 *  do some special handling, especially for Vista, to ensure that
 *  the logged on user token has the privilege to impersonate on this
 *  machine.
 *
 *  NOTE: In order to set the impersonation privilege, the logged on user
 *  must be enabled as an impersonable user. To do this, go to the Local
 *  Security Settings in the control panel, go to Local Policies\User Rights
 *  Assignment and add your user to the 'Impersonate a client after
 *  authentication' policy.
 *
 *	Return Value: true if successful.
 */
BOOL afp_impersonate_user(
    IN const HANDLE h_user_token
)
{
    //auto success = afp_adjust_privilege(const_cast<PWCHAR>(SE_IMPERSONATE_NAME), TRUE);

    //if (success)
    {
        const auto success = ImpersonateLoggedOnUser(h_user_token);
        if (!success)
        {
            DBGWRITE(dbg_level_error, L"Impersonation failed\n");
            return success;
        }
    }

    return TRUE;
}


BOOL afp_revert_to_self(
    IN const HANDLE h_user_token
)
{
    UNREFERENCED_PARAMETER(h_user_token);

    const BOOL success = RevertToSelf();
    const std::wstring impersonate_name = SE_IMPERSONATE_NAME;

    //afp_adjust_privilege(const_cast<PWCHAR>(impersonate_name.c_str()), FALSE);

    return success;
}


bool is_white_space(const char c)
{
    // A white space checker that doesn't choke on negative values.

    return (c == ' ' || c == '\t' || c == '\r' || c == '\n');
}


void afp_get_object_name_from_path(
    IN const PWCHAR p_path,
    OUT const PWCHAR p_name_buff,
    IN const size_t cb_name_buff
)
{
    const auto path_len = lstrlen(p_path);

    if (path_len == 0)
    {
        return;
    }

    //
    //When this loop completes, szTemp should contain the
    //object name.
    //
    auto i = path_len - 1;

    do
    {
        if (p_path[i] == '\\')
        {
            i++;
            break;
        }

        i--;
    }
    while (i >= 0);

    // ReSharper disable once CppFunctionResultShouldBeUsed
    StringCbCopy(p_name_buff, cb_name_buff, &p_path[max(0, i)]);

    //
    //BUGBUG: Add code here to check for proper length of filename!
    //
}


void afp_get_object_dot3_extension(
    IN const PWCHAR p_path,
    OUT const PWCHAR p_name_buff,
    IN const size_t cb_name_buff
)
{
    const auto path_len = lstrlen(p_path);

    if (path_len == 0)
    {
        return;
    }

    //
    //When this loop completes, szTemp should contain the
    //object extension.
    //
    auto i = path_len - 1;
    auto j = 0;

    do
    {
        if (p_path[i] == '.')
        {
            i++;
            break;
        }

        i--;
        j++;

        if (j >= 4) break;
    }
    while (i >= 0);

    if (j < 4)
    {
        // ReSharper disable once CppFunctionResultShouldBeUsed
        StringCbCopy(p_name_buff, cb_name_buff, &p_path[max(0, i)]);
    }
}


void afp_get_object_path(
    IN const PWCHAR p_path,
    OUT const PWCHAR p_path_buff
)
{
    // Returns just the path to a file or dir. For example, if the full
    // path to a file is c:\dir1\dir2\dir3\filename, then this function
    // will return c:\dir1\dir2\dir3.

    const auto path_len = lstrlen(p_path);

    p_path_buff[0] = '\0';

    if (path_len == 0)
    {
        return;
    }

    auto j = path_len - 1;
    auto i = 0;

    do
    {
        if (p_path[j] == '\\')
        {
            break;
        }

        i++;
        j--;
    }
    while (i < path_len);

    lstrcpyn(p_path_buff, p_path, path_len - i);
}


AFPERROR afp_map_win_error_to_afp(
    IN const DWORD win_error,
    IN const AFPERROR default_error //Default AFP error to use if no map found
)
{
    switch (win_error)
    {
    case NO_ERROR: return AFP_OK;
    case ERROR_LOCK_VIOLATION: return afpLockErr;

    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND: return afpObjectNotFound;

    case ERROR_INVALID_PARAMETER:
    case ERROR_INVALID_NAME: return afpParmErr;

    case ERROR_ACCESS_DENIED: return afpAccessDenied;
    case ERROR_HANDLE_DISK_FULL: return afpDiskFull;
    case ERROR_FILE_EXISTS: return afpObjectExists;
    case ERROR_DIR_NOT_EMPTY: return afpDirNotEmpty;
    case ERROR_HANDLE_EOF: return afpEofError;
    case ERROR_INVALID_LOGON_HOURS: return afpUserNotAuth;
    case ERROR_PASSWORD_EXPIRED: return afpPwdExpiredErr;

    default: return default_error;
    }
}
