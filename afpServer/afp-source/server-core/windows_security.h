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

// Membership check for the built-in Administrators alias (S-1-5-32-544)
// against a connection's logon token. Used to grant Windows admins the
// AFP owner-equivalent right (kAccess_Owner), mirroring the fact that
// Windows lets administrators edit any object's DACL/owner.
bool is_admin_token(
    IN HANDLE token
);

// user_token: the connection's logon token (may be nullptr for
// pre-authentication checks). A token belonging to a member of the
// built-in Administrators group receives the owner-equivalent rights.
bool get_directory_rights(
    const std::wstring& dir_path,
    const std::wstring& logged_on_user_name,
    DWORD* afp_rights,
    IN HANDLE user_token = nullptr
);

// Copies the cached name for an AFP id into a caller-supplied buffer of
// cch_name WCHARs (always null-terminated; a longer cached name is
// truncated rather than overrun).
AFPERROR get_user_name_by_id(
    IN DWORD id,
    IN OUT PWCHAR name,
    IN size_t cch_name
);

DWORD get_directory_owner_and_group_id(
    IN const std::filesystem::path& dir_win_path,
    IN const std::wstring& logged_on_user_name,
    OUT DWORD* owner_id,
    OUT DWORD* group_id
);

// Assigns or retrieves the AFP user/group id for an account name
// (the cache dedupes by type+name). Wraps the internal cache so
// protocol code (FPMapName) can share the very same id space that
// get_directory_owner_and_group_id() reports.
DWORD afp_id_for_account_name(
    IN users_and_group_name_type type,
    IN const std::wstring& name
);

// Sets the object's owner and/or group (either SID may be nullptr to
// leave it unchanged). Caller must be impersonating a principal that
// holds WRITE_OWNER (or is the current owner); Windows enforces this.
DWORD set_directory_owner_and_group(
    IN const std::wstring& obj_path,
    IN PSID owner_sid,
    IN PSID group_sid
);

// Applies the coarse AFP rights (O*/G*/W*/U* bit triples in afp_rights)
// to the object's *explicit* DACL: existing explicit ACEs for the
// object's owner, its primary group and Everyone are replaced with a
// single ALLOW ACE per class carrying the requested generic rights (the
// owner additionally keeps READ_CONTROL | WRITE_DAC). Inherited ACEs and
// ACEs of other principals are preserved; inherited grants remain a
// floor the AFP layer cannot undercut. Returns ERROR_SUCCESS or the
// Win32 error (never rely on GetLastError afterwards - logging resets it).
DWORD set_directory_afp_access(
    IN const std::wstring& obj_path,
    IN DWORD afp_rights,
    IN bool is_directory
);

// Makes the user behind the token the owner of the object when the
// object's current owner is different (e.g. Windows defaults the owner
// of objects created with a full administrator token to
// BUILTIN\Administrators). Enables SeTakeOwnershipPrivilege on the
// impersonated thread when present; silently does nothing when the
// object is already owned correctly or the privilege is unavailable.
void ensure_token_user_owns(
    IN const std::wstring& obj_path,
    IN HANDLE token
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

