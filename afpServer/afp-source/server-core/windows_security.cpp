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

    if (cb_sid == 0)
    {
        DBGWRITE(dbg_level_info, L"LookupAccountName() sizing failed getting user SID for [%s] (%d)\n",
                 user_name.c_str(),
                 GetLastError());
        return nullptr;
    }

    const auto p_sid = LocalAlloc(LMEM_ZEROINIT, cb_sid);

    if (p_sid != nullptr)
    {
        // The sizing call tells us how big the referenced-domain string is;
        // it is not bounded by 128 characters (long enterprise forest names
        // easily exceed that), so size the buffer from the report instead
        // of a fixed array the second call would overrun.
        std::wstring ref_domain_name;
        ref_domain_name.resize(cb_referenced_domain_name + 1);
        cb_referenced_domain_name = static_cast<DWORD>(ref_domain_name.size());

        const auto success = LookupAccountName(
            nullptr,
            user_name.c_str(),
            p_sid,
            &cb_sid,
            ref_domain_name.data(),
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

/*
 * get_sid_last_rid()
 *
 * Description:
 *		Returns the relative identifier (last sub-authority) of a SID.
 */
static DWORD get_sid_last_rid(PSID sid)
{
    const auto count = GetSidSubAuthorityCount(sid);
    if (count == nullptr || *count == 0)
    {
        return 0;
    }
    return *GetSidSubAuthority(sid, *count - 1);
}

/*
 * get_guest_account_sid()
 *
 * Description:
 *		Resolves the SID of the built-in Guest account of this machine.
 *
 *		CreateWellKnownSid cannot be used: its domain-relative account
 *		types (WinAccountGuestSid, WinAccountAdministratorSid) fail with
 *		ERROR_INVALID_PARAMETER when passed a NULL domain on current
 *		Windows, and the BUILTIN alias SIDs do not carry the machine SID.
 *
 *		Instead we resolve the *canonical* name "Guest" scoped to the
 *		local machine. Windows keeps canonical account names working on
 *		every locale (the display name is "Invitado", "Gast", etc. on
 *		Spanish/French systems), and the resulting SID is verified to
 *		carry the built-in Guest RID (501), so a mis-resolution can
 *		never be mistaken for the built-in account.
 *
 * Returns: LocalAlloc-allocated SID (caller LocalFrees) or nullptr.
 */
PSID get_guest_account_sid()
{
    // Built-in Guest account RID on every Windows machine.
    constexpr DWORD kBuiltInGuestRid = 501;

    WCHAR machine_name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD name_len = _countof(machine_name);
    if (!GetComputerNameW(machine_name, &name_len))
    {
        DBGWRITE(dbg_level_info, L"GetComputerName() failed getting guest SID (%d)\n", GetLastError());
        return nullptr;
    }

    const std::wstring qualified_name = std::wstring(machine_name) + L"\\Guest";

    PSID sid = nullptr;
    DWORD sid_size = 0;
    DWORD domain_size = 0;
    SID_NAME_USE sid_use = SidTypeUnknown;

    // First call sizes the SID buffer (and the referenced-domain string).
    LookupAccountNameW(nullptr, qualified_name.c_str(), nullptr, &sid_size, nullptr, &domain_size, &sid_use);
    if (sid_size == 0)
    {
        DBGWRITE(dbg_level_info, L"LookupAccountName() sizing for [%s] failed (%d)\n",
                 qualified_name.c_str(), GetLastError());
        return nullptr;
    }

    sid = static_cast<PSID>(LocalAlloc(LMEM_ZEROINIT, sid_size));
    if (sid == nullptr)
    {
        return nullptr;
    }

    // Size the domain string from the sizing call - it is not limited to a
    // small fixed buffer for domain-joined machines.
    std::wstring ref_domain;
    ref_domain.resize(domain_size + 1);
    domain_size = static_cast<DWORD>(ref_domain.size());

    const BOOL resolved = LookupAccountNameW(nullptr, qualified_name.c_str(), sid, &sid_size,
                                             ref_domain.data(), &domain_size, &sid_use);

    if (!resolved || !IsValidSid(sid) || get_sid_last_rid(sid) != kBuiltInGuestRid)
    {
        DBGWRITE(dbg_level_info, L"LookupAccountName() for [%s] failed (%d) or RID mismatch\n",
                 qualified_name.c_str(), GetLastError());
        LocalFree(sid);
        return nullptr;
    }

    return sid;
}

/*
 * get_guest_account_name()
 *
 * Description:
 *		Resolves the name + domain to use for logging on the built-in
 *		Guest account (canonical "Guest" name scoped to this machine,
 *		verified through its SID to be the built-in account, RID 501).
 *
 * Returns: true on success.
 */
bool get_guest_account_name(
    OUT std::wstring& name,
    OUT std::wstring& domain
)
{
    name.clear();
    domain.clear();

    const auto guest_sid = get_guest_account_sid();
    if (guest_sid == nullptr)
    {
        return false;
    }
    LocalFree(guest_sid);

    WCHAR machine_name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD name_len = _countof(machine_name);
    if (!GetComputerNameW(machine_name, &name_len))
    {
        return false;
    }

    name = L"Guest";
    domain = machine_name;
    return true;
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

bool get_effective_rights_for_sid(
    AUTHZ_RESOURCE_MANAGER_HANDLE manager,
    PSECURITY_DESCRIPTOR psd,
    PSID p_sid,
    ACCESS_MASK* access_mask)
{
    BOOL result = FALSE;
    constexpr LUID unused_id = {0, 0};
    AUTHZ_CLIENT_CONTEXT_HANDLE authz_client_context = nullptr;

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

    return result;
}

bool get_effective_rights_for_user(
    AUTHZ_RESOURCE_MANAGER_HANDLE manager,
    PSECURITY_DESCRIPTOR psd,
    const std::wstring& user_name,
    ACCESS_MASK* access_mask)
{
    PSID p_sid = get_sid_for_user(user_name);
    if (p_sid == nullptr)
    {
        return false;
    }

    const bool result = get_effective_rights_for_sid(manager, psd, p_sid, access_mask);
    LocalFree(p_sid);
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

// True if the token's user is a member of the built-in Administrators
// alias. CheckTokenMembership honours the token's group attributes, so a
// full (unfiltered) administrator token reports TRUE while a UAC-filtered
// one reports FALSE.
bool is_admin_token(
    IN HANDLE token
)
{
    if (token == nullptr)
    {
        return false;
    }

    SID_IDENTIFIER_AUTHORITY nt_authority = SECURITY_NT_AUTHORITY;
    PSID admin_sid = nullptr;

    if (!AllocateAndInitializeSid(
        &nt_authority,
        2,
        SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0,
        &admin_sid))
    {
        DBGWRITE(dbg_level_info, L"AllocateAndInitializeSid(admins) failed (%d)\n", GetLastError());
        return false;
    }

    BOOL is_member = FALSE;

    // The connection's token is an impersonation-capable duplicate that is
    // not the process token: CheckTokenMembership on such a handle fails
    // with ERROR_IMPERSONATION_LEVEL_INSUFFICIENT unless the calling
    // thread is impersonating that very user. Temporarily impersonate for
    // the membership test (the token always belongs to the session's own
    // user).
    if (ImpersonateLoggedOnUser(token))
    {
        if (!CheckTokenMembership(nullptr, admin_sid, &is_member))
        {
            DBGWRITE(dbg_level_info, L"CheckTokenMembership(current) failed (%d)\n", GetLastError());
            is_member = FALSE;
        }
        RevertToSelf();
    }
    else
    {
        DBGWRITE(dbg_level_info, L"CheckTokenMembership: impersonation failed (%d)\n", GetLastError());
    }

    FreeSid(admin_sid);
    return is_member != FALSE;
}

namespace {

// Localized display name of the built-in Users group (S-1-5-32-545),
// e.g. "Users" / "Benutzer". Empty on failure.
std::wstring get_builtin_users_group_name()
{
    SID_IDENTIFIER_AUTHORITY nt_authority = SECURITY_NT_AUTHORITY;
    PSID users_sid = nullptr;

    if (!AllocateAndInitializeSid(
        &nt_authority,
        2,
        SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_USERS,
        0, 0, 0, 0, 0, 0,
        &users_sid))
    {
        return {};
    }

    WCHAR name[256];
    WCHAR domain[256];
    DWORD cch_name = _countof(name);
    DWORD cch_domain = _countof(domain);
    SID_NAME_USE use = SidTypeUnknown;

    std::wstring result;
    if (LookupAccountSidW(nullptr, users_sid, name, &cch_name, domain, &cch_domain, &use))
    {
        result = name;
    }

    FreeSid(users_sid);
    return result;
}

// Coarse AFP read/write/execute classification of a Windows access mask.
// Only *concrete* bits are listed: the composite macros (GENERIC_*,
// FILE_GENERIC_*, STANDARD_RIGHTS_*) overlap - FILE_GENERIC_READ and
// FILE_GENERIC_WRITE both embed STANDARD_RIGHTS_WRITE (READ_CONTROL,
// 0x20000) - so testing composites misclassifies every readable object
// as writable. AuthzAccessCheck with MAXIMUM_ALLOWED returns the mapped
// (concrete) mask, which classifies cleanly.
constexpr ACCESS_MASK k_read_bits = GENERIC_READ | FILE_READ_DATA |
    FILE_LIST_DIRECTORY | FILE_READ_EA | FILE_READ_ATTRIBUTES;
constexpr ACCESS_MASK k_write_bits = GENERIC_WRITE | FILE_WRITE_DATA |
    FILE_ADD_FILE | FILE_APPEND_DATA | FILE_ADD_SUBDIRECTORY |
    FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES | DELETE | FILE_DELETE_CHILD;
constexpr ACCESS_MASK k_exec_bits = GENERIC_EXECUTE | FILE_EXECUTE |
    FILE_TRAVERSE;

DWORD mask_to_rw_bits(const ACCESS_MASK mask, const DWORD s_bit, const DWORD r_bit, const DWORD w_bit)
{
    DWORD rw = 0;
    if (mask & k_read_bits) rw |= r_bit;
    if (mask & k_write_bits) rw |= w_bit;
    if (mask & k_exec_bits) rw |= s_bit;
    return rw;
}

ACCESS_MASK rw_bits_to_mask(const DWORD rw, const DWORD s_bit, const DWORD r_bit, const DWORD w_bit)
{
    ACCESS_MASK mask = 0;
    if (rw & r_bit) mask |= GENERIC_READ;
    if (rw & w_bit) mask |= GENERIC_WRITE;
    if (rw & s_bit) mask |= GENERIC_EXECUTE;
    return mask;
}

} // unnamed namespace

bool get_directory_rights(
    const std::wstring& dir_path,
    const std::wstring& logged_on_user_name,
    DWORD* afp_rights,
    IN HANDLE user_token
)
{
    PACL p_dacl = nullptr;
    PSECURITY_DESCRIPTOR security_descriptor = nullptr;
    PSID p_sid_group = nullptr;
    PSID p_sid_owner = nullptr;
    ACCESS_MASK access_rights = 0;
    ACCESS_MASK group_access_rights = 0;
    ACCESS_MASK world_access_rights = 0;

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

        // Even on failure the API may have allocated a partial descriptor;
        // LocalFree-ing a null pointer is safe.
        LocalFree(security_descriptor);
        return false;
    }

    // One resource manager for this call, shared by the user and guest
    // access checks (each AuthzInitializeResourceManager is expensive).
    AUTHZ_RESOURCE_MANAGER_HANDLE authz_manager = nullptr;
    const BOOL have_manager = AuthzInitializeResourceManager(
        AUTHZ_RM_FLAG_NO_AUDIT,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        &authz_manager);

    if (!have_manager)
    {
        DBGWRITE(dbg_level_error, L"AuthzInitializeResourceManager failed with %d\n", GetLastError());
    }

    // Check to see if the current user is the owner of the directory. If not, we
    // get the access rights for this user as allowed by it's SID.
    bool owner_equivalent = false;

    if (const auto user_sid = get_sid_for_user(logged_on_user_name); user_sid != nullptr)
    {
        if (have_manager)
        {
            get_effective_rights_for_sid(authz_manager, security_descriptor, user_sid, &access_rights);
        }

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
            owner_equivalent = true;
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

    // Windows lets members of the built-in Administrators group edit any
    // object's DACL and owner. Report them as owner-equivalent (AFP has no
    // separate "administrator" right); without this, Finder grays out the
    // whole Sharing panel for administrators on objects owned by
    // BUILTIN\Administrators - the default owner of objects that an admin
    // creates with a full (non-filtered) administrator token.
    if (!owner_equivalent && is_admin_token(user_token))
    {
        DBGWRITE(dbg_level_trace, L"Logged on user is a Windows administrator; reporting owner-equivalent rights\n");
        *afp_rights |= kAccess_Owner | kAccess_OALL | kAccess_UALL;
    }

    // The group rights describe what the object's primary group may do
    // (the Finder group row).
    if (have_manager && p_sid_group != nullptr)
    {
        get_effective_rights_for_sid(authz_manager, security_descriptor, p_sid_group, &group_access_rights);
    }

    *afp_rights |= mask_to_rw_bits(group_access_rights, kAccess_GS, kAccess_GR, kAccess_GW);

    // The "world" (everyone) rights. The Finder "everyone" row means the
    // anonymous/world principal, so it is evaluated against the Everyone
    // well-known SID - not the built-in Guest account, which on modern
    // Windows holds no rights on ordinary shares at all.
    BYTE world_sid_buffer[SECURITY_MAX_SID_SIZE];
    DWORD world_sid_length = sizeof(world_sid_buffer);
    const auto world_sid = reinterpret_cast<PSID>(world_sid_buffer);

    if (CreateWellKnownSid(WinWorldSid, nullptr, world_sid, &world_sid_length) && have_manager)
    {
        get_effective_rights_for_sid(authz_manager, security_descriptor, world_sid, &world_access_rights);
    }
    else
    {
        DBGWRITE(dbg_level_info, L"CreateWellKnownSid(WinWorldSid) failed (%d)\n", GetLastError());
    }

    *afp_rights |= mask_to_rw_bits(world_access_rights, kAccess_WS, kAccess_WR, kAccess_WW);

    if (have_manager)
    {
        AuthzFreeResourceManager(authz_manager);
    }

    if (security_descriptor != nullptr)
    {
        LocalFree(security_descriptor);
    }

    return true;
}

AFPERROR get_user_name_by_id(
    IN const DWORD id,
    IN OUT PWCHAR name,
    IN const size_t cch_name
)
{
    if (name == nullptr || cch_name == 0)
    {
        return afpParmErr;
    }

    if (const auto user_and_group = g_user_and_group_cache.find_by_id(id); user_and_group != nullptr)
    {
        // lstrcpyn always null-terminates and copies at most cch_name - 1
        // characters: a cached name longer than the caller's buffer is
        // truncated, not run over it.
        lstrcpyn(name, user_and_group->name.c_str(), static_cast<int>(cch_name));
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

    // A Windows *local* account's primary group is the machine-local group
    // whose SAM name is literally "None" (every local user has it as
    // primary; whoami /groups shows it unnamed). Reporting that verbatim
    // makes Finder show "User/Group: None" for every object on the share.
    // Report the built-in Users group instead - every local account (and
    // every administrator) is a member of it, so it is a truthful primary
    // group for display and for the group-rights row.
    if (_wcsicmp(account_name, L"None") == 0)
    {
        if (const auto users_name = get_builtin_users_group_name(); !users_name.empty())
        {
            lstrcpyn(account_name, users_name.c_str(), max_name);
            sid_type = SidTypeGroup;
        }
    }

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

DWORD afp_id_for_account_name(
    IN const users_and_group_name_type type,
    IN const std::wstring& name
)
{
    return g_user_and_group_cache.add(type, name);
}

DWORD set_directory_owner_and_group(
    IN const std::wstring& obj_path,
    IN PSID owner_sid,
    IN PSID group_sid
)
{
    if (owner_sid == nullptr && group_sid == nullptr)
    {
        return ERROR_SUCCESS;
    }

    SECURITY_INFORMATION sec_info = 0;
    if (owner_sid != nullptr) sec_info |= OWNER_SECURITY_INFORMATION;
    if (group_sid != nullptr) sec_info |= GROUP_SECURITY_INFORMATION;

    // Called while impersonating the AFP user; Windows enforces that the
    // caller may assign the requested owner/group (WRITE_OWNER, or being
    // the current owner assigning it back to itself).
    const auto error = SetNamedSecurityInfo(
        const_cast<WCHAR*>(obj_path.c_str()),
        SE_FILE_OBJECT,
        sec_info,
        owner_sid,
        group_sid,
        nullptr,
        nullptr
    );

    if (error != ERROR_SUCCESS)
    {
        DBGWRITE(dbg_level_info, L"SetNamedSecurityInfo(owner/group) failed on %s (%d)\n",
                 obj_path.c_str(), error);
    }

    return error;
}

DWORD set_directory_afp_access(
    IN const std::wstring& obj_path,
    IN const DWORD afp_rights,
    IN const bool is_directory
)
{
    PSID obj_owner_sid = nullptr;
    PSID obj_group_sid = nullptr;
    PSECURITY_DESCRIPTOR merged_psd = nullptr;

    auto error = GetNamedSecurityInfo(
        const_cast<WCHAR*>(obj_path.c_str()),
        SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION,
        &obj_owner_sid,
        &obj_group_sid,
        nullptr,
        nullptr,
        &merged_psd
    );

    if (error != ERROR_SUCCESS)
    {
        DBGWRITE(dbg_level_info, L"GetNamedSecurityInfo failed in set_directory_afp_access (%d)\n", error);
        LocalFree(merged_psd);
        return error != ERROR_SUCCESS ? error : ERROR_ACCESS_DENIED;
    }

    // The *explicit* DACL of this object (UNPROTECTED view): the ACEs we
    // are allowed to rewrite. Inherited ACEs stay owned by the parent.
    PACL explicit_dacl = nullptr;
    PSECURITY_DESCRIPTOR explicit_psd = nullptr;

    error = GetNamedSecurityInfo(
        const_cast<WCHAR*>(obj_path.c_str()),
        SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION,
        nullptr,
        nullptr,
        &explicit_dacl,
        nullptr,
        &explicit_psd
    );

    if (error != ERROR_SUCCESS)
    {
        DBGWRITE(dbg_level_info, L"GetNamedSecurityInfo(unprotected) failed (%d)\n", error);
        LocalFree(explicit_psd);
        LocalFree(merged_psd);
        return error != ERROR_SUCCESS ? error : ERROR_ACCESS_DENIED;
    }

    BYTE world_sid_buffer[SECURITY_MAX_SID_SIZE];
    DWORD world_sid_length = sizeof(world_sid_buffer);
    const auto world_sid = reinterpret_cast<PSID>(world_sid_buffer);

    if (!CreateWellKnownSid(WinWorldSid, nullptr, world_sid, &world_sid_length))
    {
        const DWORD world_error = GetLastError();
        DBGWRITE(dbg_level_info, L"CreateWellKnownSid(WinWorldSid) failed (%d)\n", world_error);
        LocalFree(explicit_psd);
        LocalFree(merged_psd);
        return world_error != ERROR_SUCCESS ? world_error : ERROR_ACCESS_DENIED;
    }

    // The three ACEs the AFP dirAccess triplets translate to. The "user
    // actual" (UA*) bits report the requester's real access and are not a
    // permission class, so they never produce an ACE here.
    const BYTE inherit_flags = is_directory
        ? static_cast<BYTE>(OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE)
        : static_cast<BYTE>(0);

    // Worst-case size: every existing explicit ACE kept plus three new ones.
    const ULONG existing_size = explicit_dacl != nullptr ? explicit_dacl->AclSize : 0;
    constexpr ULONG new_ace_room = 3 * (sizeof(ACCESS_ALLOWED_ACE) + SECURITY_MAX_SID_SIZE);
    const ULONG new_dacl_size = existing_size + new_ace_room;

    auto dacl_buffer = std::make_unique<BYTE[]>(new_dacl_size);
    auto* new_dacl = reinterpret_cast<PACL>(dacl_buffer.get());

    bool built = InitializeAcl(new_dacl, static_cast<DWORD>(new_dacl_size), ACL_REVISION) != FALSE;

    // The principal of an access ACE: SidStart follows the fixed-size
    // header and mask (ACCESS_ALLOWED_ACE / ACCESS_DENIED_ACE layout).
    const auto ace_sid_of = [](const ACE_HEADER* ace) -> PSID
    {
        return reinterpret_cast<PSID>(
            &reinterpret_cast<PACCESS_ALLOWED_ACE>(const_cast<ACE_HEADER*>(ace))->SidStart);
    };

    const auto ace_is_managed = [&](const ACE_HEADER* ace)
    {
        if (ace->AceType != ACCESS_ALLOWED_ACE_TYPE && ace->AceType != ACCESS_DENIED_ACE_TYPE)
        {
            return false; // object ACEs etc.: keep verbatim
        }
        const PSID sid = ace_sid_of(ace);
        return (obj_owner_sid != nullptr && EqualSid(sid, obj_owner_sid)) ||
            (obj_group_sid != nullptr && EqualSid(sid, obj_group_sid)) ||
            EqualSid(sid, world_sid);
    };

    // Keep every explicit ACE that is not for one of the AFP-managed
    // principals (owner / group / everyone); those are rebuilt below.
    if (built && explicit_dacl != nullptr)
    {
        for (ULONG i = 0; i < explicit_dacl->AceCount && built; i++)
        {
            ACE_HEADER* ace = nullptr;
            if (!GetAce(explicit_dacl, i, reinterpret_cast<LPVOID*>(&ace)))
            {
                continue;
            }

            if (ace_is_managed(ace))
            {
                continue;
            }

            if (!AddAce(new_dacl, ACL_REVISION, MAXDWORD, ace, ace->AceSize))
            {
                DBGWRITE(dbg_level_info, L"AddAce failed (%d)\n", GetLastError());
                built = false;
            }
        }
    }

    // Add one AFP class ACE; a class with no rights simply gets no ACE.
    const auto add_class_ace = [&](PSID sid, DWORD s_bit, DWORD r_bit, DWORD w_bit,
                                   ACCESS_MASK extra) -> bool
    {
        if (sid == nullptr)
        {
            return true;
        }

        ACCESS_MASK mask = rw_bits_to_mask(afp_rights, s_bit, r_bit, w_bit) | extra;

        // A readable directory stays traversable (Finder's "read only"
        // means read+execute for folders).
        if (is_directory && (mask & GENERIC_READ) != 0)
        {
            mask |= GENERIC_EXECUTE;
        }

        if (mask == 0)
        {
            return true;
        }

        return AddAccessAllowedAceEx(new_dacl, ACL_REVISION, inherit_flags, mask, sid) != FALSE;
    };

    // The owner always keeps READ_CONTROL | WRITE_DAC so it can reopen the
    // Sharing panel and restore rights even after a chmod 000.
    built = built &&
        add_class_ace(obj_owner_sid, kAccess_OS, kAccess_OR, kAccess_OW,
            READ_CONTROL | WRITE_DAC) &&
        add_class_ace(obj_group_sid, kAccess_GS, kAccess_GR, kAccess_GW, 0) &&
        add_class_ace(world_sid, kAccess_WS, kAccess_WR, kAccess_WW, 0);

    if (!built)
    {
        DBGWRITE(dbg_level_info, L"Building the AFP DACL for %s failed (%d)\n",
                 obj_path.c_str(), GetLastError());
        LocalFree(explicit_psd);
        LocalFree(merged_psd);
        return ERROR_GEN_FAILURE;
    }

    // Replaces the object's explicit DACL. Inherited ACEs stay in effect:
    // a share whose root grants, say, Users:Modify keeps granting it -
    // the AFP permission model layers on top of, and cannot undercut,
    // inherited Windows ACLs.
    error = SetNamedSecurityInfo(
        const_cast<WCHAR*>(obj_path.c_str()),
        SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION,
        nullptr,
        nullptr,
        new_dacl,
        nullptr
    );

    LocalFree(explicit_psd);
    LocalFree(merged_psd);

    if (error != ERROR_SUCCESS)
    {
        DBGWRITE(dbg_level_info, L"SetNamedSecurityInfo(DACL) failed on %s (%d)\n",
                 obj_path.c_str(), error);
    }

    return error;
}

void ensure_token_user_owns(
    IN const std::wstring& obj_path,
    IN HANDLE token
)
{
    if (token == nullptr)
    {
        return;
    }

    BYTE token_user_buffer[64 * 1024]; /* generous TOKEN_USER buffer (SecurityMaxTokenSize) */
    DWORD returned_length = 0;
    const auto token_user = reinterpret_cast<PTOKEN_USER>(token_user_buffer);

    if (!GetTokenInformation(token, TokenUser, token_user, sizeof(token_user_buffer), &returned_length))
    {
        DBGWRITE(dbg_level_info, L"GetTokenInformation(TokenUser) failed (%d)\n", GetLastError());
        return;
    }

    PSID current_owner = nullptr;
    PSECURITY_DESCRIPTOR psd = nullptr;

    const auto error = GetNamedSecurityInfo(
        const_cast<WCHAR*>(obj_path.c_str()),
        SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION,
        &current_owner,
        nullptr,
        nullptr,
        nullptr,
        &psd
    );

    if (error == ERROR_SUCCESS && current_owner != nullptr &&
        !EqualSid(current_owner, token_user->User.Sid))
    {
        // Objects created with a full administrator token default to
        // BUILTIN\Administrators as their owner. Hand ownership to the
        // AFP user so Finder reports the creator - requires
        // SeTakeOwnershipPrivilege, which administrator tokens hold.
        adjust_privilege(const_cast<wchar_t*>(L"SeTakeOwnershipPrivilege"), TRUE);

        const auto set_error = SetNamedSecurityInfo(
            const_cast<WCHAR*>(obj_path.c_str()),
            SE_FILE_OBJECT,
            OWNER_SECURITY_INFORMATION,
            token_user->User.Sid,
            nullptr,
            nullptr,
            nullptr
        );

        if (set_error != ERROR_SUCCESS)
        {
            DBGWRITE(dbg_level_info,
                     L"Could not hand ownership of %s to the creating AFP user (%d)\n",
                     obj_path.c_str(), set_error);
        }
    }

    LocalFree(psd);
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

        // Whatever the error, if we created the token handle locally we
        // must close it - leaving it out on one error path was a leak.
        if (h_token == INVALID_HANDLE_VALUE)
        {
            CloseHandle(token);
        }

        return FALSE;
    }

    // AdjustTokenPrivileges returns TRUE even when the privilege is not
    // held by the token; the real outcome is in GetLastError.
    if (GetLastError() == ERROR_NOT_ALL_ASSIGNED)
    {
        DBGWRITE(dbg_level_warning, L"AdjustTokenPrivileges(): privilege %s is not assigned to the token\n", privilege);

        if (h_token == INVALID_HANDLE_VALUE)
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

    return RevertToSelf();
}
