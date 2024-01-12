#pragma once

#include <filesystem>

#include "afp.h"

constexpr auto days_from_1980_2000 = 7305L; // (20*365 + 5);
constexpr auto seconds_per_day = 86400L; // 24*60*60;
constexpr auto seconds_from_1980_2000 = seconds_per_day * days_from_1980_2000;
constexpr auto num_100_ns_per_second = 10000000L;
constexpr LARGE_INTEGER seconds_to_start_of_1980 = {{0xc8df3700 , 0x00000002}};

std::string wide_string_to_string(const std::wstring & wide_string, UINT code_page);
std::wstring string_to_wide_string(const std::string & string, UINT code_page);

std::wstring get_root_drive_of_file_path(const std::wstring& file_path);

void afp_extract_domain_and_user_name(
	IN std::wstring& user_name,
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
