#include "stdafx.h"
#include "afp_char_map.h"

#include <filesystem>

#include "afpdebug.h"
#include "afphelp.h"


static UINT code_page_for_path_type(const UINT path_type)  // NOLINT(misc-use-anonymous-namespace)
{
	return path_type == kLongNames ? CP_MACCP : CP_UTF8;
}

//
// The char map is serialized to the Afp_CharMap alternate data stream with
// fwrite(&struct)/fread(&struct), so its on-disk layout must never depend on
// the target bitness. The in-memory char_map_data stores the mapping
// position in std::wstring::size_type, which is 4 bytes on x86 and 8 bytes
// on x64; on disk the position is always stored as a UINT32. The layout
// below is bitness-independent and byte-compatible with the 260-byte
// streams written by older 32-bit builds.
//
#pragma pack(push, 4)
struct disk_char_mapping
{
	UINT32 position;
	wchar_t replaced;
};

struct disk_char_map_data
{
	UINT8 num_mappings;
	disk_char_mapping map[char_map_size];
};
#pragma pack(pop)

static_assert(sizeof(disk_char_mapping) == 8, "Afp_CharMap on-disk mapping layout changed");
static_assert(sizeof(disk_char_map_data) == 260, "Afp_CharMap on-disk layout changed");

std::wstring afp_char_map::generate_map_from_mac_path(const std::string& mac_path, UINT8 path_type, char_map_data& map_data)
{
	auto win_path = string_to_wide_string(mac_path, code_page_for_path_type(path_type));
	if (win_path.empty())
	{
		map_data.num_mappings = 0;
		return win_path;
	}

	// ':' cannot legally appear in a Macintosh name - it is the classic
	// HFS/AFP path separator - and it is dangerous on NTFS, where a name
	// containing ':' that gets joined onto a folder addresses an
	// ALTERNATE DATA STREAM: FPCreateFile "parent:child" would silently
	// create a 0-byte stream on the parent folder instead of a file.
	// Do what Finder does when the user types ':' in a name: substitute
	// a dash. The substitution is length-preserving and runs before the
	// illegal-character mapping below, so recorded char-map positions
	// stay valid. No char-map entry is recorded for it: ':' was never a
	// legal Mac name character, so there is nothing to restore on the
	// way back to the client - the dash is the name.
	for (auto& ch : win_path)
	{
		if (ch == L':')
		{
			ch = L'-';
		}
	}

	UINT8 map_index = 0;

	// The map only holds char_map_size entries; stop recording once it is
	// full instead of walking off the end of map[]. (Extra modified
	// characters beyond the map capacity simply will not be restored on
	// the way back to the Mac client; recording them was never possible.)
	auto add_mapping = [&map_data, &map_index](const wchar_t replaced_char, const std::wstring::size_type pos)
	{
		if (map_index >= char_map_size)
		{
			return false;
		}

		map_data.map[map_index++].set(replaced_char, pos);
		return true;
	};

	// First take care of windows illegal path characters.
	for (const wchar_t win_illegal_path_character : win_illegal_path_characters)
	{
		size_t position;

		do
		{
			position = win_path.find(win_illegal_path_character);
			if (position != std::wstring::npos)
			{
				add_mapping(win_path[position], position);
				win_path[position] += 128;
			}
		} while (position != std::wstring::npos);
	}

    for (std::wstring::size_type i = 0; i < win_path.size(); i++)
	{
		if (constexpr wchar_t min_char_code = 32; win_path[i] < min_char_code)
		{
			add_mapping(win_path[i], i);
			win_path[i] += 128;
		}

		if (win_path[i] == '.' && i + 1 < win_path.size() && win_path[i+1] == '.')
		{
			add_mapping(win_path[i], i);
			win_path[i] += 128;
		}
	}

	// Windows explorer does not like file names to end with a '.' or a ' '.
	if (win_path.back() == L'.' || win_path.back() == L' ')
	{
		add_mapping(win_path[win_path.size() - 1], win_path.size() - 1);
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

	const UINT8 num_mappings = map_data->num_mappings > char_map_size ? char_map_size : map_data->num_mappings;
	for (UINT8 mapping = 0; mapping < num_mappings; mapping++)
	{
		// The mapping data comes from an on-disk stream; ignore entries that
		// point outside the path instead of corrupting memory.
		if (map_data->map[mapping].position < wide_path.size())
		{
			wide_path[map_data->map[mapping].position] = map_data->map[mapping].replaced;
		}
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

	std::filesystem::path path = file_path;
	path.concat(map_stream_name);

	FILE* raw_file = nullptr;
	const auto success = _wfopen_s(&raw_file, path.c_str(), L"rb");
	if (success != 0 || raw_file == nullptr)
	{
		//DBGWRITE(dbg_level_error, L"Failed to read char_map_data from file (%s)\n", file_path.c_str());
		return nullptr;
	}

	// Close the stream on every exit path. A leaked FILE* here keeps an OS
	// handle open on the object's Afp_CharMap stream, and an open stream
	// handle prevents the parent file or directory from ever being deleted.
	const std::unique_ptr<FILE, decltype(&fclose)> file(raw_file, fclose);

	// Determine the stream format from its size:
	//   260 bytes: stable on-disk layout (also written by legacy 32-bit builds).
	//   sizeof(char_map_data) == 520 on x64: written by the first x64 build
	//   (2.3.0) before the layout was pinned; still readable.
	if (fseek(file.get(), 0, SEEK_END) != 0)
	{
		return nullptr;
	}

	const long stream_size = ftell(file.get());
	fseek(file.get(), 0, SEEK_SET);

	auto char_map = std::make_shared<char_map_data>();

	if (stream_size == static_cast<long>(sizeof(disk_char_map_data)))
	{
		disk_char_map_data disk_map{};
		if (fread(&disk_map, sizeof(disk_map), 1, file.get()) != 1)
		{
			DBGWRITE(dbg_level_error, L"Reading char map failed, empty file (%s)\n", file_path.c_str());
			return nullptr;
		}

		char_map->num_mappings = disk_map.num_mappings > char_map_size ? char_map_size : disk_map.num_mappings;
		for (UINT8 mapping = 0; mapping < char_map->num_mappings; mapping++)
		{
			char_map->map[mapping].position = disk_map.map[mapping].position;
			char_map->map[mapping].replaced = disk_map.map[mapping].replaced;
		}
	}
	else if (stream_size == static_cast<long>(sizeof(char_map_data)))
	{
		// Stream written in the native layout of the current architecture
		// (only possible for the transitional 2.3.0 x64 build).
		if (fread(char_map.get(), sizeof(char_map_data), 1, file.get()) != 1)
		{
			DBGWRITE(dbg_level_error, L"Reading char map failed, empty file (%s)\n", file_path.c_str());
			return nullptr;
		}

		if (char_map->num_mappings > char_map_size)
		{
			char_map->num_mappings = char_map_size;
		}
	}
	else
	{
		DBGWRITE(dbg_level_warning, L"Unexpected char map stream size (%ld) for %s\n",
		         stream_size, file_path.c_str());
		return nullptr;
	}

	return char_map;
}

void afp_char_map::write_char_map(const std::wstring& file_path, const char_map_data& map)
{
	if (!std::filesystem::exists(file_path))
	{
		DBGWRITE(dbg_level_error, L"Path not found writing char map! (%s)\n", file_path.c_str());

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

	// Serialize through the bitness-independent on-disk layout.
	disk_char_map_data disk_map{};
	disk_map.num_mappings = map.num_mappings > char_map_size ? char_map_size : map.num_mappings;
	for (UINT8 mapping = 0; mapping < disk_map.num_mappings; mapping++)
	{
		disk_map.map[mapping].position = static_cast<UINT32>(map.map[mapping].position);
		disk_map.map[mapping].replaced = map.map[mapping].replaced;
	}

	const auto wrote = fwrite(&disk_map, sizeof(disk_map), 1, file);
	if (wrote != 1)
	{
		DBGWRITE(dbg_level_error, L"Failed to write char_map_data to file. (%s)\n", file_path.c_str());
	}

	fclose(file);  // NOLINT(cert-err33-c)
}
