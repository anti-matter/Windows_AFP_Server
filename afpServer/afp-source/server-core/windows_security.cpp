#include "stdafx.h"

#include "afpdebug.h"
#include "windows_security.h"

#include <accctrl.h>
#include <Aclapi.h>

#include "afp.h"
#include "afphelp.h"

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "authz.lib")

// Security routines to obtain access rights for a user against
// a file/directory object.
//
// From: https://learn.microsoft.com/en-us/windows/win32/api/aclapi/nf-aclapi-geteffectiverightsfromacla

namespace {
	users_and_groups_cache g_user_and_group_cache;
}

PSID get_sid_for_user(IN const std::wstring& user_name)
{
    DWORD cb_sid = 0;
    DWORD cb_referenced_domain_name = 0;
    SID_NAME_USE sid_name_use;

    LookupAccountName(
        nullptr,
        user_name.c_str(),
        nullptr,
        &cb_sid,
        nullptr,
        &cb_referenced_domain_name,
        &sid_name_use
    );

    const auto p_sid = LocalAlloc(LMEM_ZEROINIT, cb_sid);

    if (p_sid != nullptr)
    {
        WCHAR ref_domain_name[128];

        const auto success = LookupAccountName(
            nullptr,
            user_name.c_str(),
            p_sid,
            &cb_sid,
            ref_domain_name,
            &cb_referenced_domain_name,
            &sid_name_use
        );

        if (!success)
        {
            //
            //For some reason we failed to get the account SID.
            //
            DBGWRITE(dbg_level_info, L"LookupAccountName() failed getting user SID for [%s] (%d)\n",
                     user_name.c_str(),
                     GetLastError());

            LocalFree(p_sid);
            return nullptr;
        }
    }

    return p_sid;
}

void get_access(
    const AUTHZ_CLIENT_CONTEXT_HANDLE authz_client,
    const PSECURITY_DESCRIPTOR psd,
    ACCESS_MASK* access_mask)
{
    AUTHZ_ACCESS_REQUEST access_request;
    AUTHZ_ACCESS_REPLY access_reply{};
    BYTE buffer[1024];

    if (access_mask == nullptr)
    {
        ASSERT(0);
        return;
    }

    *access_mask = 0;

    access_request.DesiredAccess = MAXIMUM_ALLOWED;
    access_request.PrincipalSelfSid = nullptr;
    access_request.ObjectTypeList = nullptr;
    access_request.ObjectTypeListLength = 0;
    access_request.OptionalArguments = nullptr;

    RtlZeroMemory(buffer, sizeof(buffer));
    access_reply.ResultListLength = 1;
    access_reply.GrantedAccessMask = reinterpret_cast<PACCESS_MASK>(buffer);
    access_reply.Error = reinterpret_cast<PDWORD>(buffer + sizeof(ACCESS_MASK));

    if (!AuthzAccessCheck(0,
                          authz_client,
                          &access_request,
                          nullptr,
                          psd,
                          nullptr,
                          0,
                          &access_reply,
                          nullptr))
    {
        DBGWRITE(dbg_level_error, L"AuthzAccessCheck failed with %d\n", GetLastError());
    }
    else
    {
        *access_mask = *access_reply.GrantedAccessMask;
    }
}

bool get_effective_rights_for_user(
    AUTHZ_RESOURCE_MANAGER_HANDLE manager,
    PSECURITY_DESCRIPTOR psd,
    const std::wstring& user_name,
    ACCESS_MASK* access_mask)
{
    BOOL result = FALSE;
    constexpr LUID unused_id = {0, 0};
    AUTHZ_CLIENT_CONTEXT_HANDLE authz_client_context = nullptr;

    PSID p_sid = get_sid_for_user(user_name);
    if (p_sid != nullptr)
    {
        result = AuthzInitializeContextFromSid(
            0,
            p_sid,
            manager,
            nullptr,
            unused_id,
            nullptr,
            &authz_client_context);

        if (result)
        {
            get_access(authz_client_context, psd, access_mask);
            AuthzFreeContext(authz_client_context);
        }
        else
        {
            DBGWRITE(dbg_level_error, L"AuthzInitializeContextFromSid failed with %d\n", GetLastError());
        }
    }
    if (p_sid != nullptr)
    {
        LocalFree(p_sid);
        p_sid = nullptr;
    }

    return result;
}

void use_authz_solution(
    const PSECURITY_DESCRIPTOR psd,
    const std::wstring& user_name,
    ACCESS_MASK* access_mask)
{
    AUTHZ_RESOURCE_MANAGER_HANDLE manager;

    if (AuthzInitializeResourceManager(
        AUTHZ_RM_FLAG_NO_AUDIT,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        &manager))
    {
        get_effective_rights_for_user(manager, psd, user_name, access_mask);
        AuthzFreeResourceManager(manager);
    }
    else
    {
        DBGWRITE(dbg_level_error, L"AuthzInitializeResourceManager failed with %d\n", GetLastError());
    }
}

bool get_directory_rights(
    const std::wstring& dir_path,
    const std::wstring& logged_on_user_name,
    DWORD* afp_rights
)
{
    PACL p_dacl = nullptr;
    PSECURITY_DESCRIPTOR security_descriptor = nullptr;
    PSID p_sid_group = nullptr;
    PSID p_sid_owner = nullptr;
    ACCESS_MASK access_rights = 0;

    DBGWRITE(dbg_level_trace, L"Getting rights for %s...\n", dir_path.c_str());
    DBGWRITE(dbg_level_trace, L"user name = %s\n", logged_on_user_name.c_str());

    *afp_rights = 0;

    const DWORD error = GetNamedSecurityInfo(
        dir_path.c_str(),
        SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION,
        &p_sid_owner,
        &p_sid_group,
        &p_dacl,
        nullptr,
        &security_descriptor
    );

    if (!(SUCCEEDED(error)) || p_sid_owner == nullptr)
    {
        DBGWRITE(dbg_level_info, L"GetNamedSecurityInfo() failed getting dir rights (%d)!\n", error);
        return false;
    }

    // Check to see if the current user is the owner of the directory. If not, we
    // get the access rights for this user as allowed by it's SID.

    if (const auto user_sid = get_sid_for_user(logged_on_user_name); user_sid != nullptr)
    {
        use_authz_solution(security_descriptor, logged_on_user_name, &access_rights);

        if (EqualSid(user_sid, p_sid_owner))
        {
            DBGWRITE(dbg_level_trace, L"Logged on user (%s) is the owner!!\n", logged_on_user_name.c_str());
            DBGWRITE(dbg_level_trace, L"Checking access rights for owner\n");


            if ((access_rights & GENERIC_ALL) == GENERIC_ALL || (access_rights & FILE_ALL_ACCESS) == FILE_ALL_ACCESS)
            {
                DBGWRITE(dbg_level_trace, L"Owner has full control.\n");

                *afp_rights = kAccess_OALL | kAccess_UALL;
            }
            else
            {
                if ((access_rights & GENERIC_READ) == GENERIC_READ || (access_rights & FILE_GENERIC_READ) == FILE_GENERIC_READ)
                {
                    DBGWRITE(dbg_level_trace, L"Owner has read access!\n");
                    *afp_rights |= kAccess_OR | kAccess_UAR;
                }
                if ((access_rights & GENERIC_WRITE) == GENERIC_WRITE || (access_rights & FILE_GENERIC_WRITE) == FILE_GENERIC_WRITE ||
                    (access_rights & FILE_WRITE_DATA) == FILE_WRITE_DATA)
                {
                    DBGWRITE(dbg_level_trace, L"Owner has write access!\n");
                    *afp_rights |= kAccess_OW | kAccess_UAW;
                }
                if ((access_rights & GENERIC_EXECUTE) == GENERIC_EXECUTE || (access_rights & FILE_GENERIC_EXECUTE) == FILE_GENERIC_EXECUTE)
                {
                    DBGWRITE(dbg_level_trace, L"Owner has execute access!\n");
                    *afp_rights |= kAccess_OS | kAccess_UAS;
                }
            }

            *afp_rights |= kAccess_Owner;
        }
        else
        {
            // Get the explicit access rights for this user.

            if ((access_rights & GENERIC_READ) == GENERIC_READ || (access_rights & FILE_GENERIC_READ) == FILE_GENERIC_READ)
            {
                DBGWRITE(dbg_level_trace, L"User has read access!\n");
                *afp_rights |= kAccess_UAR;
            }
            if ((access_rights & GENERIC_WRITE) == GENERIC_WRITE || (access_rights & FILE_GENERIC_WRITE) == FILE_GENERIC_WRITE ||
                (access_rights & FILE_WRITE_DATA) == FILE_WRITE_DATA)
            {
                DBGWRITE(dbg_level_trace, L"User has write access!\n");
                *afp_rights |= kAccess_UAW;
            }
            if ((access_rights & GENERIC_EXECUTE) == GENERIC_EXECUTE || (access_rights & FILE_GENERIC_EXECUTE) == FILE_GENERIC_EXECUTE)
            {
                DBGWRITE(dbg_level_trace, L"User has execute access!\n");
                *afp_rights |= kAccess_UAS;
            }
        }

        LocalFree(user_sid);
    }

    // Now check for guest access.
    //
    // BUGBUG: I cannot find a way to get the name of the guest account. This
    // will fail on international systems as the guest name is different.

    use_authz_solution(security_descriptor, L"Guest", &access_rights);

    if ((access_rights & GENERIC_READ) == GENERIC_READ || (access_rights & FILE_GENERIC_READ) == FILE_GENERIC_READ)
    {
        DBGWRITE(dbg_level_trace, L"Guest has read access!\n");
        *afp_rights |= kAccess_WR;
    }
    if ((access_rights & GENERIC_WRITE) == GENERIC_WRITE || (access_rights & FILE_GENERIC_WRITE) == FILE_GENERIC_WRITE ||
        (access_rights & FILE_WRITE_DATA) == FILE_WRITE_DATA)
    {
        DBGWRITE(dbg_level_trace, L"Guest has write access!\n");
        *afp_rights |= kAccess_WW;
    }
    if ((access_rights & GENERIC_EXECUTE) == GENERIC_EXECUTE || (access_rights & FILE_GENERIC_EXECUTE) == FILE_GENERIC_EXECUTE)
    {
        DBGWRITE(dbg_level_trace, L"Guest has execute access!\n");
        *afp_rights |= kAccess_WS;
    }

    if (security_descriptor != nullptr)
    {
        LocalFree(security_descriptor);
    }

    return true;
}

AFPERROR get_user_name_by_id(
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

DWORD get_directory_owner_and_group_id(
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
        *owner_id = g_user_and_group_cache.add(afp_user_name_type, account_name);
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
        *group_id = g_user_and_group_cache.add(afp_group_name_type, account_name);
    }
    else if (sid_type == SidTypeUser || sid_type == SidTypeAlias)
    {
        const auto user = g_user_and_group_cache.find_by_name(afp_group_name_type, account_name);
        if (user == nullptr)
        {
            *group_id = g_user_and_group_cache.add(afp_user_name_type, account_name);
        }
        else
        {
            *group_id = user->id;
        }
    }

    LocalFree(security_descriptor);

    g_user_and_group_cache.add(afp_user_name_type, logged_on_user_name);

    return error;
}

BOOL open_current_token(
    IN OUT const PHANDLE token
)
{
    // Attempt to open a thread token first since it take precedence
    // over a process token for most operations.
    if (!OpenThreadToken(
        GetCurrentThread(),
        TOKEN_ADJUST_PRIVILEGES,
        FALSE,
        token))
    {
        // Couldn't open a thread token, so now open one for
        // the process.
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

BOOL adjust_privilege(
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

    // Here's the real API call that adjusts the privileges.
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

        // If we have an invalid handle, we don't want to make a call on
        // it at this stage, or we'll die. Only close if we created the token
        // handle locally.
        if (error != ERROR_INVALID_HANDLE && h_token == INVALID_HANDLE_VALUE)
        {
            CloseHandle(token);
        }

        return FALSE;
    }

    DBGWRITE(dbg_level_trace, L"AdjustTokenPrivileges() succeeded for %s\n", privilege);

    // We only want to close the Token handle if we created it
    // locally with OpenCurrentToken().
    if (h_token == INVALID_HANDLE_VALUE && token != INVALID_HANDLE_VALUE)
    {
        CloseHandle(token);
    }

    return TRUE;
}

BOOL impersonate_user(
    IN const HANDLE h_user_token
)
{
    const auto success = ImpersonateLoggedOnUser(h_user_token);
    if (!success)
    {
        DBGWRITE(dbg_level_error, L"Impersonation failed\n");
        return success;
    }

    return TRUE;
}

BOOL revert_to_self(
    IN const HANDLE h_user_token
)
{
    UNREFERENCED_PARAMETER(h_user_token);

    const BOOL success = RevertToSelf();
    const std::wstring impersonate_name = SE_IMPERSONATE_NAME;

    return success;
}
