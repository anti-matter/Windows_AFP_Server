#pragma once
#include <filesystem>
#include "afphelp.h"

constexpr INT32 hash_multiplier = 1024;

// These flags are used to keep state of file objects in the index.
enum
{
	flag_index_is_dir = 0x01,
	flag_index_resource_fork_open = 0x02,
	flag_index_data_fork_open = 0x04,
	flag_index_has_charmap = 0x08,
	flag_index_attribute_open = 0x10,
	flag_index_set_item = 0x80
};

// This is a structure to keep track of a single directory or file.
// We use this to map AFP dirID's to windows directories and file
// ID's to files.
struct afp_index
{
	afp_index()
	    : par_id(0)
	    , par_entry(nullptr)
	    , object_id(0)
	    , flags(0)
	    , path_hash(0)
	{
	}

	INT32 par_id;
	afp_index* par_entry;
	INT32 object_id;
	std::wstring sz_object_name;
	std::filesystem::path sz_path;
	WORD flags;
	DWORD path_hash;
};

class afp_file_index  // NOLINT(cppcoreguidelines-special-member-functions)
{
public:
	afp_file_index();
	~afp_file_index();

	afp_index* add_entry(
		UINT32 par_id,
        const std::wstring& p_object_name,
        const std::filesystem::path& p_object_path,
		bool is_directory,
		INT32* p_new_dir_id = nullptr
	);

	void remove_entry(const afp_index* entry);
	void remove_entry(INT32 object_id);

	void update_entry(afp_index* entry, afp_index* new_parent, const std::wstring& new_name);

	static void update_entry_flags(afp_index* p_entry, UINT16 flags);
	static size_t hash_string(PWCHAR string);

	afp_index* find(INT32 object_id);
	afp_index* find(const std::wstring& full_path);
	afp_index* find(INT32 directory_id, const std::wstring& path);

	UINT32 get_offspring(INT32 par_id, std::vector<afp_index*>& offspring);
	UINT32 get_offspring(INT32 par_id);

	UINT32 next_id() { return m_next_id_++; }

	[[nodiscard]] std::reference_wrapper<std::recursive_mutex> get_mutex() { return std::ref(mutex_); }

#ifdef _DEBUG
	void dump_index();
    static void dump_entry(const afp_index* entry);
#endif

private:
	std::recursive_mutex mutex_;
	std::vector<std::unique_ptr<afp_index>> m_index_;

	UINT32 m_next_id_;
};
