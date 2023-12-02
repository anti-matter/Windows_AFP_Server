#pragma once

#include <authz.h>

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

