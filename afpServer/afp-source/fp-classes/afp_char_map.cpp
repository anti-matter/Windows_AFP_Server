#include "stdafx.h"
#include "afp_char_map.h"

#include <filesystem>

#include "afpdebug.h"
#include "afphelp.h"


static UINT code_page_for_path_type(const UINT path_type)  // NOLINT(misc-use-anonymous-namespace)
{
	return path_type == kLongNames ? CP_MACCP : CP_UTF8;
}

std::wstring afp_char_map::generate_map_from_mac_path(const std::string& mac_path, UINT8 path_type, char_map_data& map_data)
{
	auto win_path = string_to_wide_string(mac_path, code_page_for_path_type(path_type));
	UINT8 map_index = 0;

	// First take care of windows illegal path characters.
	for (const wchar_t win_illegal_path_character : win_illegal_path_characters)
	{
		size_t position;

		do
		{
			position = win_path.find(win_illegal_path_character);
			if (position != std::wstring::npos)
			{
				map_data.map[map_index++].set(win_path[position], position);
				win_path[position] += 128;
			}
		} while (position != std::wstring::npos);
	}

    for (std::wstring::size_type i = 0; i < win_path.size(); i++)
	{
		if (constexpr wchar_t min_char_code = 32; win_path[i] < min_char_code)
		{
			map_data.map[map_index++].set(win_path[i], i);
			win_path[i] += 128;
		}

		if (win_path[i] == '.' && i + 1 < win_path.size() && win_path[i+1] == '.')
		{
			map_data.map[map_index++].set(win_path[i], i);
			win_path[i] += 128;
		}
	}

	// Windows explorer does not like file names to end with a '.' or a ' '.
	if (win_path.back() == L'.' || win_path.back() == L' ')
	{
		map_data.map[map_index++].set(win_path[win_path.size() - 1], win_path.size() - 1);
		win_path[win_path.size() - 1] += 128;
	}

	DBGWRITE(dbg_level_trace, L"String in: %s\n", string_to_wide_string(mac_path, code_page_for_path_type(path_type)).c_str());
    DBGWRITE(dbg_level_trace, L"String out: %s\n", win_path.c_str());
	DBGWRITE(dbg_level_trace, L"Mappings count: %d\n", map_index);

	map_data.num_mappings = map_index;

	return win_path;
}

std::string afp_char_map::win_path_to_mac_path(const std::wstring& win_path, const UINT8 path_type)
{
	const auto map_data = read_char_map(win_path);
	if (map_data == nullptr)
	{
		return wide_string_to_string(win_path, code_page_for_path_type(path_type));
	}

	return win_path_to_mac_path(win_path, path_type, map_data.get());
}

std::string afp_char_map::win_path_to_mac_path(const std::wstring& win_path, UINT8 path_type, const char_map_data* map_data)
{
	auto wide_path = win_path;

	for (UINT8 mapping = 0; mapping < map_data->num_mappings; mapping++)
	{
		wide_path[map_data->map[mapping].position] = map_data->map[mapping].replaced;
	}

	return wide_string_to_string(wide_path, code_page_for_path_type(path_type));
}

std::shared_ptr<char_map_data> afp_char_map::read_char_map(const std::wstring& file_path)
{
	if (!std::filesystem::exists(file_path))
	{
		DBGWRITE(dbg_level_error, L"Path not found reading char map! (%s)\n", file_path.c_str());
		return nullptr;
	}

	auto char_map = std::make_shared<char_map_data>();

	std::filesystem::path path = file_path;
	path.concat(map_stream_name);

	FILE* file;
	const auto success = _wfopen_s(&file, path.c_str(), L"rb");
	if (success != 0 || file == nullptr)
	{
		//DBGWRITE(dbg_level_error, L"Failed to read char_map_data from file (%s)\n", file_path.c_str());
		return nullptr;
	}

	if (fread(char_map.get(), sizeof(char_map_data), 1, file) == 0)
	{
		DBGWRITE(dbg_level_error, L"Reading char map failed, empty file (%s)\n", file_path.c_str());
		return nullptr;
	}

	fclose(file);  // NOLINT(cert-err33-c)

	return char_map;
}

void afp_char_map::write_char_map(const std::wstring& file_path, const char_map_data& map)
{
	if (!std::filesystem::exists(file_path))
	{
		DBGWRITE(dbg_level_error, L"Path not found writing char map! (%s)\n", file_path.c_str());
		ASSERT(0);

		return;
	}

	std::filesystem::path path = file_path;
	path.concat(map_stream_name);

	FILE* file;
	const auto success = _wfopen_s(&file, path.c_str(), L"wb");
	if (success != 0 || file == nullptr)
	{
		return;
	}

	const auto wrote = fwrite(&map, sizeof(char_map_data), 1, file);
	if (wrote != 1)
	{
		DBGWRITE(dbg_level_error, L"Failed to write char_map_data to file. (%s)\n", file_path.c_str());
	}

	fclose(file);  // NOLINT(cert-err33-c)
}
