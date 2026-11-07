#pragma once

#include <authz.h>
#include <filesystem>

#include "afp.h"
#include "users_and_groups_cache.h"

PSID get_sid_for_user(const std::wstring& user_name);

// Built-in Guest account identification. The account is resolved through
// the canonical name "Guest" scoped to the local machine (canonical names
// work on every locale, unlike the display name which is localized), and
// the resolved SID is asserted to carry the built-in Guest RID (501).
// CreateWellKnownSid is unusable for built-in *account* SIDs: the
// domain-relative types fail with ERROR_INVALID_PARAMETER for a NULL
// domain on current Windows.
// get_guest_account_sid returns a LocalAlloc-allocated SID (caller LocalFrees),
// or nullptr. get_guest_account_name returns the name + machine domain to
// pass to LogonUser for the built-in guest.
PSID get_guest_account_sid();

bool get_guest_account_name(
    OUT std::wstring& name,
    OUT std::wstring& domain
);

void get_access(
    AUTHZ_CLIENT_CONTEXT_HANDLE authz_client,
    PSECURITY_DESCRIPTOR psd,
    ACCESS_MASK* access_mask
);

bool get_effective_rights_for_user(
    AUTHZ_RESOURCE_MANAGER_HANDLE manager,
    PSECURITY_DESCRIPTOR psd,
    const std::wstring& user_name,
    ACCESS_MASK* access_mask
);

bool get_effective_rights_for_sid(
    AUTHZ_RESOURCE_MANAGER_HANDLE manager,
    PSECURITY_DESCRIPTOR psd,
    PSID sid,
    ACCESS_MASK* access_mask
);

void use_authz_solution(
    PSECURITY_DESCRIPTOR psd,
    const std::wstring& user_name,
    ACCESS_MASK* access_mask
);

bool get_directory_rights(
    const std::wstring& dir_path,
    const std::wstring& logged_on_user_name,
    DWORD* afp_rights
);

AFPERROR get_user_name_by_id(
    IN DWORD id,
    IN PWCHAR name
);

DWORD get_directory_owner_and_group_id(
    IN const std::filesystem::path& dir_win_path,
    IN const std::wstring& logged_on_user_name,
    OUT DWORD* owner_id,
    OUT DWORD* group_id
);

BOOL open_current_token(
    IN OUT PHANDLE token
);

BOOL adjust_privilege(
    IN PWCHAR privilege,
    IN BOOL enable,
    IN HANDLE h_token = INVALID_HANDLE_VALUE
);

BOOL impersonate_user(
    IN HANDLE h_user_token
);

BOOL revert_to_self(
    IN HANDLE h_user_token
);

