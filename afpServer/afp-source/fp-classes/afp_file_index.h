#pragma once
#include <filesystem>

#include "afp_char_map.h"
#include "afp_net_buffer.h"

constexpr INT32 hash_multiplier = 1024;

// These flags are used to keep state of file objects in the index.
enum : UINT8
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
	// Owning back-reference to the parent entry. A parent stays alive
	// (though possibly unlinked from the index) as long as any child or
	// open file still references it, so dereferences can never hit free
	// memory. Root entries keep this null.
	std::shared_ptr<afp_index> par_entry;
	INT32 object_id;
	std::wstring sz_object_name;
	std::filesystem::path sz_path;
	WORD flags;
	DWORD path_hash;

	char_map_data char_map;

    [[nodiscard]] bool is_res_file() const
	{
		return sz_path.extension() == res_file_extension;
    }
};

class afp_file_index  // NOLINT(cppcoreguidelines-special-member-functions)
{
public:
    explicit afp_file_index(INT16 volume_id);
	~afp_file_index();

	// Entries are reference-counted: find()/add_entry() hand out owning
	// shared pointers, so consumers that outlive a single locked
	// operation (afp_open_file, most notably) keep the entry object
	// alive even when a concurrent delete unlinks it from the index.
	// Unlinked entries become "orphans": still valid memory, but no
	// longer searched, and their flag updates are inert.
	std::shared_ptr<afp_index> add_entry(
		INT32 par_id,
        const std::wstring& p_object_name,
        const std::filesystem::path& p_object_path,
		bool is_directory,
		const char_map_data* char_map = nullptr,
		INT32* p_new_dir_id = nullptr
	);

	void remove_entry(const std::shared_ptr<afp_index>& entry);
	void remove_entry(INT32 object_id);

	void update_entry(INT32 object_id, INT32 new_parent_id);
	void update_entry(const std::shared_ptr<afp_index>& entry, const std::shared_ptr<afp_index>& new_parent, const std::wstring& new_name, const char_map_data* char_map = nullptr);

	static void update_entry_flags(afp_index* p_entry, UINT16 flags);
	static size_t hash_string(PWCHAR string);

	std::shared_ptr<afp_index> find(INT32 object_id);
	std::shared_ptr<afp_index> find(const std::wstring& full_path);
	std::shared_ptr<afp_index> find(INT32 directory_id, const std::wstring& path);

	UINT32 get_offspring(INT32 par_id, std::vector<afp_index*>& offspring);
	UINT32 get_offspring(INT32 par_id);

	INT32 next_id() { return m_next_id_++; }

	[[nodiscard]] std::reference_wrapper<std::recursive_mutex> get_mutex() { return std::ref(mutex_); }

#ifdef _DEBUG
	void dump_index(INT32 par_id);
    static void dump_entry(const afp_index* entry);
#endif

private:
	INT16 volume_id_;

	std::recursive_mutex mutex_;
	std::vector<std::shared_ptr<afp_index>> m_index_;

	INT32 m_next_id_;
};
