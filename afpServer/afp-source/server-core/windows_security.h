#pragma once

#include <authz.h>
#include <filesystem>

#include "afp.h"
#include "users_and_groups_cache.h"

PSID get_sid_for_user(const std::wstring& user_name);

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

