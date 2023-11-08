#include "StdAfx.h"
#include <cstring>

#include "afphelp.h"
#include "afp_net_buffer.h"


afp_net_buffer::afp_net_buffer(_In_ char* afp_buffer, _In_ const size_t cb_buffer)
	: buffer_(afp_buffer)
	  , current_buffer_ptr_(afp_buffer)
	  , buffer_size_(cb_buffer)
	  , dsi_return_value_(0)
	  , dsi_data_offset_(0)
{
}

std::wstring afp_net_buffer::pull_path(const UINT8 path_type)
{
	std::wstring result_str;

	switch(path_type)
	{
	case kLongNames:
		result_str = convert_to_windows_path(pull_multi_byte_string<UINT8>());
		return result_str;
	case kUnicodeNames:
		result_str = convert_to_windows_path(pull_multi_byte_string<UINT16>(true));
		return result_str;
	default:
		break;
	}

	return L"";
}

AFPERROR afp_net_buffer::push_path(const UINT8 path_type, const std::wstring& path)
{
	if (not_enough_space(path.length()) || path.length() > 255)
	{
		return afpParmErr;
	}

	switch (path_type)
	{
	case kLongNames:
		push_multi_byte_string<UINT8>(convert_to_mac_path(path));
		break;
	case kUnicodeNames:
		push_multi_byte_string<INT16>(convert_to_mac_path(path));
		break;
	default:
		return afpParmErr;
	}

	return AFP_OK;
}

std::wstring afp_net_buffer::pull_string(const UINT8 path_type)
{
	switch (path_type)
	{
	case kLongNames:
		return pull_wide_string();
	case kUnicodeNames:
		return string_to_wide_string(pull_multi_byte_string<UINT16>());
	default:
		break;
	}

	return L"";
}

void afp_net_buffer::push_string(const std::wstring& str, const UINT8 path_type)
{
	push_string(wide_string_to_string(str), path_type);
}

void afp_net_buffer::push_string(const std::string& str, const UINT8 path_type)
{
	if (path_type == kLongNames)
	{
		push_multi_byte_string<UINT8>(str);
	}
	else // unicode
	{
		push_multi_byte_string<UINT16>(str);
	}
}

std::wstring afp_net_buffer::convert_to_windows_path(const std::string& mac_path)
{
	if (mac_path.empty())
	{
		return L"";
	}

	auto win_path = string_to_wide_string(mac_path);
	auto replacement = first_replacement_character;

	for (auto win_illegal_path_character : win_illegal_path_characters)
	{
		std::ranges::replace(win_path, win_illegal_path_character, replacement);
		replacement++;
	}

	// Mac uses ':' as the path separator, convert to windows.
	std::ranges::replace(win_path, L':', L'\\');

	return win_path;
}

std::string afp_net_buffer::convert_to_mac_path(const std::wstring& win_path)
{
	if (win_path.empty())
	{
		return "";
	}

	auto wide_path = win_path;
	wchar_t replacement = first_replacement_character;

	for (auto win_illegal_path_character : win_illegal_path_characters)
	{
		std::ranges::replace(wide_path, replacement, win_illegal_path_character);
		replacement++;
	}

	return wide_string_to_string(wide_path);
}

std::wstring afp_net_buffer::pull_wide_string()
{
	const auto mb_string = pull_multi_byte_string<UINT8>();
    return string_to_wide_string(mb_string);
}

template <class T>
std::string afp_net_buffer::pull_multi_byte_string(const bool is_afp_path)
{
	// T is the type of the value that denotes the length of the string.
	if (is_afp_path && sizeof(T) > 1)
	{
		// Ignore the hint in an afp path.
		pull_num<UINT32>();
	}

	const int string_length = sizeof(T) == 1 ? pull_num<UINT8>() : pull_num<UINT16>();
	if (string_length == 0)
	{
		return "";
	}

	if (string_length >= (sizeof(T) == 1 ? MAXBYTE : MAX_AFP30_PATH))
    {
        DBGWRITE(dbg_level_error, L"String length error: path type=kLongNames, length=%d\n", string_length);
        return "";
    }

    char raw[MAX_AFP30_PATH];

    pull_raw_data(raw, string_length);
    raw[string_length] = '\0';

    std::string mb_string = raw;
    return mb_string;
}

template <class T>
void afp_net_buffer::push_multi_byte_string(const std::string& mac_string)
{
	sizeof(T) == sizeof(UINT8)
        ? push_num(static_cast<UINT8>(mac_string.length()))
        : push_num(static_cast<UINT16>(mac_string.length()));

	push_raw_data(mac_string.data(), mac_string.size());
}

void afp_net_buffer::pull_raw_data(void* buffer, const size_t amount)
{
	if (current_buffer_ptr_ + amount > buffer_ + buffer_size_)
	{
		DBGWRITE(dbg_level_error, L"Reading past end of buffer!\n");
		ASSERT(0);

		return;
	}

	memcpy(buffer, current_buffer_ptr_, amount);
	current_buffer_ptr_ += amount;
}

bool afp_net_buffer::check_space_needed(const size_t space_needed) const
{
	if (get_current_pos_ptr() - get_buffer() + space_needed > buffer_size_)
	{
		return false;
	}

	return true;
}

bool afp_net_buffer::not_enough_space(const size_t space_needed) const
{
	return !check_space_needed(space_needed);
}

bool afp_net_buffer::will_overflow(const UINT32 value) const
{
	return buffer_ + value > buffer_ + buffer_size_;
}
