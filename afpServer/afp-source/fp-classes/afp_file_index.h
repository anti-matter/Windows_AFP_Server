#pragma once

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

//
//This is a structure to keep track of a single directory or file.
//We use this to map AFP dirID's to windows directories and file
//ID's to files.
//
typedef struct
{
	DWORD par_id;
	PVOID par_entry;
	DWORD object_id;
	WCHAR sz_object_name[MAX_AFP30_NAME];
	WCHAR sz_path[MAX_PATH];
	WORD flags;
	DWORD path_hash;
} afp_index, *p_afp_index;

class afp_file_index  // NOLINT(cppcoreguidelines-special-member-functions)
{
public:
	afp_file_index();
	~afp_file_index();

	p_afp_index add_entry(
		uint32 par_id,
		PWCHAR p_object_name,
		PWCHAR p_object_path,
		bool is_directory,
		uint32* p_new_dir_id = nullptr
	);
	p_afp_index get_entry(uint32 object_id);

	void remove_entry(p_afp_index entry, bool delete_directory_sub_tree = false);
	void remove_entry(uint32 object_id, bool delete_directory_sub_tree = false);

	void update_entry(
		p_afp_index p_entry,
		p_afp_index p_new_parent,
		PWCHAR p_new_name
	);

	static void update_entry_flags(
		p_afp_index p_entry,
		uint16 flags
	);

	static size_t hash_string(PWCHAR string);
	p_afp_index find_entry_with_full_path(PWCHAR pFullPath);
	uint32 get_offspring(uint32 par_id, std::vector<p_afp_index>** new_index = nullptr);

	uint32 next_id() { return m_next_id_++; }

	void inc_ref() { m_ref_count_++; }
	void dec_ref() { m_ref_count_--; }
	uint32 get_ref() const { return m_ref_count_; }

#ifdef _DEBUG
	void dump_index();
#endif

private:
	CRITICAL_SECTION m_index_critical_section_;

	std::vector<std::unique_ptr<afp_index>> m_index_;

	uint32 m_next_id_;

	//
	//This is needed as we pass some operations off to a worker thread.
	//We don't want to get deleted when a worked may be trying to call us.
	//
	uint32 m_ref_count_;
};
