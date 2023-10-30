#pragma once
#include <filesystem>
#include "afphelp.h"

constexpr int32 hash_multiplier = 1024;

//
//These flags are used to keep state of file objects in the index.
//
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
	DWORD par_id;
	afp_index* par_entry;
	DWORD object_id;
	std::wstring sz_object_name;
	std::filesystem::path sz_path;
	WORD flags;
	DWORD path_hash;
};

struct index_wait_for_add_data
{
	HANDLE event;
	std::filesystem::path object_path;
};

class afp_file_index  // NOLINT(cppcoreguidelines-special-member-functions)
{
public:
	afp_file_index();
	~afp_file_index();

	afp_index* add_entry(
		uint32 par_id,
        const std::wstring& p_object_name,
        const std::filesystem::path& p_object_path,
		bool is_directory,
		UINT32* p_new_dir_id = nullptr
	);

	afp_index* wait_for_entry(const std::filesystem::path& path);

	afp_index* get_entry(uint32 object_id);

	void remove_entry(const afp_index* entry, bool delete_directory_sub_tree = false);
	void remove_entry(uint32 object_id, bool delete_directory_sub_tree = false);

	void update_entry(afp_index* entry, afp_index* new_parent, PWCHAR new_name);

	static void update_entry_flags(afp_index* p_entry, uint16 flags);
	static size_t hash_string(PWCHAR string);

	afp_index* find(const std::wstring& full_path);
	afp_index* find(DWORD directory_id, const std::wstring& path);

	uint32 get_offspring(uint32 par_id, std::vector<afp_index*>** offspring = nullptr);

	uint32 next_id() { return m_next_id_++; }

#ifdef _DEBUG
	void dump_index();
#endif

private:
	void check_waiters(const std::filesystem::path& path);

	std::recursive_mutex mutex_;
	std::vector<std::unique_ptr<afp_index>> m_index_;

	uint32 m_next_id_;

	std::vector<std::shared_ptr<index_wait_for_add_data>> waiting_for_add_;
};
