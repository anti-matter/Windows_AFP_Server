#pragma once

#include <filesystem>

#include "afp.h"

constexpr auto days_from_1980_2000 = 7305L; // (20*365 + 5);
constexpr auto seconds_per_day = 86400L; // 24*60*60;
constexpr auto seconds_from_1980_2000 = seconds_per_day * days_from_1980_2000;
constexpr auto num_100_ns_per_second = 10000000L;

std::string wide_string_to_string(const std::wstring & wide_string, UINT code_page);
std::wstring string_to_wide_string(const std::string & string, UINT code_page);

enum users_and_group_name_type : BYTE
{
	afp_user_name_type = 0,
	afp_group_name_type
};

typedef struct
{
	BYTE type;
	DWORD id;
	std::wstring name;
} afp_user_group_name;

// Returns the SID for the logged in access token
std::shared_ptr<PSID> afp_get_logon_sid(
	IN HANDLE h_token
);

// This routine returns TRUE if the caller's process
// is a member of the Administrators local group.Caller is NOT expected to
// be impersonating anyone and is expected to be able to open their own process
// and process token.
BOOL is_user_admin(
	IN HANDLE token
);

PSID afp_get_sid_for_user(
	IN PWCHAR user_name,
	IN PWCHAR domain_name
);

DWORD afp_get_directory_rights(
	IN HANDLE token, //login token
	IN PWCHAR p_dir_win_path, //directory to check
	IN PWCHAR p_user_name,
	IN PWCHAR p_domain_name,
	IN DWORD* afp_rights
);

AFPERROR afp_get_user_name_by_id(
	IN users_and_group_name_type type,
	IN DWORD id,
	IN PWCHAR name
);

DWORD afp_get_directory_owner_and_group_id(
	IN const std::filesystem::path& dir_win_path,
	OUT DWORD* owner_id,
	OUT DWORD* group_id
);

void afp_extract_domain_and_user_name(
	OUT std::wstring& user_name,
	OUT std::wstring& domain_name
);

ULONG afp_windows_to_mac_time(
	IN PFILETIME file_time
);

ULONG afp_windows_to_mac_local_time(
	IN PFILETIME file_time
);

void afp_mac_to_windows_time(
	IN ULONG mac_time,
	IN PFILETIME file_time
);

void afp_mac_to_windows_local_time(
	IN ULONG mac_time,
	IN PFILETIME file_time
);

BOOL open_current_token(
	IN OUT PHANDLE token
);

BOOL afp_adjust_privilege(
	IN PWCHAR privilege,
	IN BOOL enable,
	IN HANDLE h_token = INVALID_HANDLE_VALUE
);

BOOL afp_adjust_account_right(
	IN HANDLE h_user_token,
	PWCHAR privileges,
	USHORT cb_privileges //count in bytes the size of the privileges buffer
);

BOOL afp_impersonate_user(
	IN HANDLE h_user_token
);

BOOL afp_revert_to_self(
	IN HANDLE h_user_token
);

bool is_white_space(char c);

void afp_get_object_name_from_path(
	IN PWCHAR p_path,
	OUT PWCHAR p_name_buff,
	IN size_t cb_name_buff
);

void afp_get_object_dot3_extension(
	IN PWCHAR p_path,
	OUT PWCHAR p_name_buff,
	IN size_t cb_name_buff
);

void afp_get_object_path(
	IN PWCHAR p_path,
	OUT PWCHAR p_path_buff
);

AFPERROR afp_map_win_error_to_afp(
	IN DWORD win_error,
	IN AFPERROR default_error
);
