#include "stdafx.h"

#include "afpdebug.h"
#include "windows_security.h"

#include <accctrl.h>
#include <Aclapi.h>

#include "afp.h"

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "authz.lib")

// Security routines to obtain access rights for a user against
// a file/directory object.
//
// From: https://learn.microsoft.com/en-us/windows/win32/api/aclapi/nf-aclapi-geteffectiverightsfromacla


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

#if _DEBUG
        const ACCESS_MASK mask = *access_reply.GrantedAccessMask;
        DBGWRITE(dbg_level_trace, L"Effective Allowed Access Mask : %8X\n", mask);

        if ((mask & GENERIC_ALL) == GENERIC_ALL || (mask & FILE_ALL_ACCESS) == FILE_ALL_ACCESS)
        {
            DBGWRITE(dbg_level_trace, L"Full Control\n");
            return;
        }
        if ((mask & GENERIC_READ) == GENERIC_READ || (mask & FILE_GENERIC_READ) == FILE_GENERIC_READ)
            DBGWRITE(dbg_level_trace, L"Read\n");
        if ((mask & GENERIC_WRITE) == GENERIC_WRITE || (mask & FILE_GENERIC_WRITE) == FILE_GENERIC_WRITE)
            DBGWRITE(dbg_level_trace, L"Write\n");
        if ((mask & GENERIC_EXECUTE) == GENERIC_EXECUTE || (mask & FILE_GENERIC_EXECUTE) == FILE_GENERIC_EXECUTE)
            DBGWRITE(dbg_level_trace, L"Execute\n");
#endif
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
                if ((access_rights & GENERIC_WRITE) == GENERIC_WRITE || (access_rights & FILE_GENERIC_WRITE) == FILE_GENERIC_WRITE)
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
            if ((access_rights & GENERIC_WRITE) == GENERIC_WRITE || (access_rights & FILE_GENERIC_WRITE) == FILE_GENERIC_WRITE)
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

    //
    //Now check for guest access.
    //
    //BUGBUG: I cannot find a way to get the name of the guest account. This
    //will fail on international systems as the guest name is different.
    //

    use_authz_solution(security_descriptor, L"Guest", &access_rights);

    if ((access_rights & GENERIC_READ) == GENERIC_READ || (access_rights & FILE_GENERIC_READ) == FILE_GENERIC_READ)
    {
        DBGWRITE(dbg_level_trace, L"Guest has read access!\n");
        *afp_rights |= kAccess_WR;
    }
    if ((access_rights & GENERIC_WRITE) == GENERIC_WRITE || (access_rights & FILE_GENERIC_WRITE) == FILE_GENERIC_WRITE)
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
        //
        //This pointer was allocate on our behalf by GetNamedSecurityInfo().
        //Clear it now that we're done with it.
        //
        LocalFree(security_descriptor);
    }

    return true;
}
