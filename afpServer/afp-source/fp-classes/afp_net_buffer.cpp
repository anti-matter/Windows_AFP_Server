#include "StdAfx.h"
#include <cstring>

#include "afphelp.h"
#include "afp_net_buffer.h"
#include "afp_char_map.h"
#include "afp_file_index.h"


afp_net_buffer::afp_net_buffer(_In_ char* afp_buffer, _In_ const size_t cb_buffer)
	: buffer_(afp_buffer)
	  , current_buffer_ptr_(afp_buffer)
	  , buffer_size_(cb_buffer)
	  , dsi_return_value_(0)
	  , dsi_data_offset_(0)
{
}

std::wstring afp_net_buffer::pull_path(const UINT8 path_type, char_map_data* char_map)
{
	std::wstring result_str;

	switch(path_type)
	{
	case kLongNames:
		result_str = convert_to_windows_path(pull_multi_byte_string<UINT8>(), path_type, char_map);
		return result_str;
	case kUnicodeNames:
		result_str = convert_to_windows_path(pull_multi_byte_string<UINT16>(true), path_type, char_map);
		return result_str;
	default:
		break;
	}

	return L"";
}

AFPERROR afp_net_buffer::push_path(const UINT8 path_type, const afp_index* index_entry)
{
	const auto path = afp_char_map::win_path_to_mac_path(index_entry->sz_object_name, path_type, &index_entry->char_map);

	if (not_enough_space(path.length()) || path.length() > 255)
	{
		return afpParmErr;
	}

	switch (path_type)
	{
	case kLongNames:
		push_multi_byte_string<UINT8>(path);
		break;
	case kUnicodeNames:
		push_multi_byte_string<INT16>(path);
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
		return string_to_wide_string(pull_multi_byte_string<UINT8>(), path_type);
	case kUnicodeNames:
		return string_to_wide_string(pull_multi_byte_string<UINT16>(), path_type);
	default:
		break;
	}

	return L"";
}

void afp_net_buffer::push_string(const std::wstring& str, const UINT8 path_type)
{
	push_string(wide_string_to_string(str, path_type));
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

std::wstring afp_net_buffer::convert_to_windows_path(const std::string& mac_path, UINT8 path_type, char_map_data* char_map)
{
	if (mac_path.empty())
	{
		return L"";
	}

	char_map_data map_data;
    auto win_path = afp_char_map::generate_map_from_mac_path(mac_path, path_type, map_data);

	if (char_map != nullptr)
	{
		memcpy(char_map, &map_data, sizeof(char_map_data));
	}

	return win_path;
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

	const size_t string_length = sizeof(T) == 1 ? pull_num<UINT8>() : pull_num<UINT16>();
	if (string_length == 0)
	{
		return "";
	}

	if (string_length >= (sizeof(T) == 1 ? max_pascal_string_length : MAX_AFP30_PATH))
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
	if constexpr (sizeof(T) > 1)
	{
		// This is supposed to be the encoding hint. We need to
		// build an encoding mapping table before we can use this.
		// for now, 0 == MAC_ROMAN encoding.
		push_num<UINT32>(0);

		push_num(static_cast<UINT16>(mac_string.length()));
	}
	else
	{
		push_num(static_cast<UINT8>(mac_string.length()));
	}

	push_raw_data(mac_string.data(), mac_string.size());
}

void afp_net_buffer::pull_raw_data(void* buffer, const size_t amount)
{
	if (current_buffer_ptr_ + amount > buffer_ + buffer_size_)
	{
		DBGWRITE(dbg_level_error, L"Reading past end of buffer (%d bytes to read, exceeded by: %d).\n",
			amount,
			(current_buffer_ptr_ + amount) - (buffer_ + buffer_size_));
		ASSERT(0);

		return;
	}

	memcpy(buffer, current_buffer_ptr_, amount);
	current_buffer_ptr_ += amount;
}


