#pragma once

const std::wstring win_illegal_path_characters = L"/\"?*<>|";
constexpr UINT8 char_map_size = 32;
inline auto map_stream_name(L":Afp_CharMap");

struct char_mapping
{
	char_mapping() : position(0), replaced(0) {}
	void set(const wchar_t replaced_char, const std::wstring::size_type pos)
	{
		replaced = replaced_char;
		position = pos;
	}

	std::wstring::size_type position;
	wchar_t replaced;
};

struct char_map_data
{
	char_map_data()
        : num_mappings(0)
        , map{}
    {
    }

	[[nodiscard]] bool has_mappings() const { return num_mappings > 0; }

	UINT8 num_mappings;
	char_mapping map[char_map_size];
};

class afp_char_map
{
public:
	// Generate a character conversion map from mac to windows. Returns a
    // windows path that has been converted.
	static std::wstring generate_map_from_mac_path(const std::string& mac_path, UINT8 path_type, char_map_data& map_data);

	static std::string win_path_to_mac_path(const std::wstring& win_path, UINT8 path_type);
	static std::string win_path_to_mac_path(const std::wstring& win_path, UINT8 path_type, const char_map_data* map_data);

	static std::shared_ptr<char_map_data> read_char_map(const std::wstring& file_path);
	static void write_char_map(const std::wstring& file_path, const char_map_data& map);
};

