#include "StdAfx.h"
#include <algorithm>

#include "afpdebug.h"
#include "afp_file_index.h"

#include "afp_volumes.h"

constexpr int first_directory_id = 1;

/*
 *	afp_file_index()
 *
 *	Description
 *		Constructor for index class.
 *
 *	Returns
 *		None
 */

afp_file_index::afp_file_index(const INT16 volume_id)
    : volume_id_(volume_id), m_next_id_(first_directory_id)
{
}


/*
 *	~afp_file_index()
 *
 *	Description
 *		Destructor for index class.
 *
 *	Returns
 *		None
 */

afp_file_index::~afp_file_index() = default;


/*
 *	add_entry()
 *
 *	Description
 *		Add a new index entry to the array.
 *
 *	Returns
 *		afp_index*
 */

afp_index* afp_file_index::add_entry(
    const INT32 par_id,
    const std::wstring& p_object_name,
    const std::filesystem::path& p_object_path,
    const bool is_directory,
	const char_map_data* char_map,
	INT32* p_new_dir_id
)
{
	auto entry = std::make_unique<afp_index>();

	if (is_directory)
	{
		entry->flags |= flag_index_is_dir;
	}

	entry->sz_object_name = p_object_name;
	entry->sz_path = p_object_path;
	entry->object_id = m_next_id_++;
	entry->par_id = par_id;

	// The parent of root doesn't have a parent entry.
	entry->par_entry = par_id == kNoParent ? nullptr : find(par_id);

	// We get a hash to the path string so that we can perform faster,
	// more efficient searches for full paths.
	entry->path_hash = std::hash<std::wstring>{}(p_object_path);

	// Take this opportunity to save or read in the char map data.
	if (char_map != nullptr)
    {
        entry->char_map = *char_map;
		memcpy(&entry->char_map, char_map, sizeof(entry->char_map));
        afp_char_map::write_char_map(entry->sz_path, entry->char_map);
    }
	else
	{
	    // Try reading it from the file stream. It's okay if this fails,
		// it just means there are no mappings.
        if (const auto temp_map = afp_char_map::read_char_map(entry->sz_path); temp_map != nullptr)
		{
			entry->char_map = *temp_map;
		}
	}

	// Save as the std::move() makes the entry confusing.
	const auto result = entry.get();

	std::lock_guard guard(mutex_);

    m_index_.push_back(std::move(entry));

	// If the caller wants us to return the new directory ID, then do so.
	if (p_new_dir_id != nullptr)
	{
		*p_new_dir_id = result->object_id;
	}

	return result;
}


/*
 *	remove_entry()
 *
 *	Description
 *		Remove an entry from the array based on a pointer to the entry data.
 *
 *	Returns
 *		None
 */

void afp_file_index::remove_entry(const afp_index* entry)
{
	remove_entry(entry->object_id);
}


/*
 *	remove_entry()
 *
 *	Description
 *		Remove an entry from the array based on the object's id.
 *
 *	Returns
 *		None
 */

void afp_file_index::remove_entry(const INT32 object_id)
{
	std::lock_guard guard(mutex_);

	const auto it = std::ranges::find_if(m_index_, [=](const std::unique_ptr<afp_index>& i)
    {
        return object_id == i->object_id;
    });

	if (it == m_index_.end())
	{
		// This can happen if something is deleted, then the
		// change notify picks it up and tries to remove
		// it again.

		DBGWRITE(
			dbg_level_trace,
			L"Failed to find objectID to remove! (%d)\n", object_id);

		return;
	}

	// Remove children if it's a directory.
	if (it->get()->flags & flag_index_is_dir)
	{
		DBGWRITE(dbg_level_trace, L"Removing children of directory: %s.\n", it->get()->sz_path.c_str());

		for (auto children_it = m_index_.begin(); children_it < m_index_.end(); ++children_it)
		{
			const auto sub_entry = children_it->get();
			if (sub_entry->par_id != object_id)
				continue;

			DBGWRITE(dbg_level_trace, L"Removing item in directory: %s\n", sub_entry->sz_path.c_str());

			// Directories could be huge, so we add this child directory to a queue
			// to be process later in a worker thead.

			const auto volumes = afp_volumes::current();
			const auto volume = volumes->find(volume_id_);
			std::filesystem::path temp;
				
			volumes->index_queue_task(task_index_remove_dir, volume, sub_entry->object_id, temp);
		}
	}

	m_index_.erase(it);
}

void afp_file_index::update_entry(const INT32 object_id, const INT32 new_parent_id)
{
    const auto entry = find(object_id);
	if (entry == nullptr)
	{
		DBGWRITE(dbg_level_error, L"Failed to find entry for update (%d)\n", object_id);
		ASSERT(0);

		return;
	}

    const auto parent = find(new_parent_id);
	if (parent == nullptr)
	{
		DBGWRITE(dbg_level_error, L"Failed to find parent for update (%d)\n", new_parent_id);
		ASSERT(0);

		return;
	}

	update_entry(entry, parent, L"", nullptr);
}

void afp_file_index::update_entry(afp_index* entry, afp_index* new_parent, const std::wstring& new_name, const char_map_data* char_map)
{
	DBGWRITE(
		dbg_level_info,
		L"Updating entry:\n\toldName=%s\n\tnewName=%s\n\tnewParID=%d, oldParID=%d\n",
		entry->sz_object_name.c_str(),
		new_name.c_str(),
		entry->par_id,
		new_parent->object_id
	);

	std::lock_guard guard(mutex_);

	// First, update the object's name and full path to the new one.
	if (!new_name.empty())
	{
		entry->sz_object_name = new_name;
	}

	// Update the new full path and full path hash
	entry->sz_path = new_parent->sz_path / entry->sz_object_name;
	entry->path_hash = std::hash<std::wstring>{}(entry->sz_path);

	// Only if the object moved do we update the parent id and entry
	if (entry->par_entry->object_id != new_parent->object_id)
	{
		entry->par_id = new_parent->object_id;
		entry->par_entry = new_parent;
	}

	// Take this opportunity to save or read in the char map data.
	if (char_map != nullptr)
	{
		entry->char_map = *char_map;
		afp_char_map::write_char_map(entry->sz_path, *char_map);
	}

	// If the entry that was moved is a directory, then we have to update the
	// full path of every object below it.
	if (entry->flags & flag_index_is_dir)
	{
		for (auto sub_it = m_index_.begin(); sub_it < m_index_.end(); ++sub_it)
		{
			const auto sub_entry = sub_it->get();
			if (sub_entry->par_id != entry->object_id)
				continue;

			const auto volumes = afp_volumes::current();
			const auto volume = volumes->find(volume_id_);
			volumes->index_queue_task(task_index_update_parent, volume, sub_entry->object_id, entry->object_id);
		}
	}
}


/*
 *	update_entry_flags()
 *
 *	Description
 *		Update the entry's flags field.
 *
 *	Returns
 *		None
 */

void afp_file_index::update_entry_flags(afp_index* p_entry, const UINT16 flags)
{
	//
	//If the "set item" flag is set, then we are setting the flags. Otherwise
	//we are clearing the bits.
	//
	if (flags & flag_index_set_item)
	{
		if (flags & flag_index_resource_fork_open)
		{
			p_entry->flags |= flag_index_resource_fork_open;
		}

		if (flags & flag_index_data_fork_open)
		{
			p_entry->flags |= flag_index_data_fork_open;
		}

		if (flags & flag_index_attribute_open)
		{
			p_entry->flags |= flag_index_attribute_open;
		}
	}
	else
	{
		if (flags & flag_index_resource_fork_open)
		{
			p_entry->flags &= ~flag_index_resource_fork_open;
		}

		if (flags & flag_index_data_fork_open)
		{
			p_entry->flags &= ~flag_index_data_fork_open;
		}

		if (flags & flag_index_attribute_open)
		{
			p_entry->flags &= ~flag_index_attribute_open;
		}
	}
}


/*
 *	hash_string()
 *
 *	Description
 *		Simple hash routine to reduce a string to a number.
 *
 *	Returns
 *		hash
 */

size_t afp_file_index::hash_string(const PWCHAR string)
{
	const auto hash = std::hash<std::wstring>{}(string);
	return hash;
}


/*
 *	find()
 *
 *	Description
 *		Get an entry from the array based on its object ID.
 *
 *	Returns
 *		afp_index*
 */

afp_index* afp_file_index::find(const INT32 object_id)
{
	std::lock_guard guard(mutex_);

	const auto it = std::ranges::find_if(m_index_, [=](const std::unique_ptr<afp_index>& i)
		{
			return object_id == i->object_id;
		});

	if (it != m_index_.end())
	{
		return it->get();
	}

	DBGWRITE(
		dbg_level_warning,
		L"objectID not found! (%d)\n", object_id);

	return nullptr;
}


/*
 *	find()
 *
 *	Description
 *		Finds an entry in the array by looking for its full path.
 *
 *	Returns
 *		afp_index*
 */

afp_index* afp_file_index::find(const std::wstring& full_path)
{
	std::lock_guard guard(mutex_);

	const auto it = std::ranges::find_if(m_index_, [=](const std::unique_ptr<afp_index>& i)
    {
        return i->sz_path.compare(full_path) == 0;
    });

	if (it == m_index_.end())
	{
		DBGWRITE(dbg_level_warning, L"Indexed object not found! (%s)\n", full_path.c_str());
		return nullptr;
	}

	return it->get();
}

afp_index* afp_file_index::find(const INT32 directory_id, const std::wstring& path)
{
	std::lock_guard guard(mutex_);

    if (const auto it = std::ranges::find_if(m_index_, [=](const std::unique_ptr<afp_index>& i)
    {
		return i->object_id == directory_id;
    }); it != m_index_.end())
	{
		if (path.empty())
		{
			return it->get();
		}

        const std::filesystem::path temp_path = it->get()->sz_path / path;
		return find(temp_path);
	}

	return nullptr;
}

UINT32 afp_file_index::get_offspring(const INT32 par_id, std::vector<afp_index*>& offspring)
{
	std::lock_guard guard(mutex_);

	for (auto it = m_index_.begin(); it < m_index_.end(); ++it)
	{
        if (const auto entry = it->get(); entry->par_id != par_id)
        {
            continue;
        }

		offspring.push_back(it->get());
	}

    return offspring.size();
}

UINT32 afp_file_index::get_offspring(const INT32 par_id)
{
	std::vector<afp_index*> offspring;
	return get_offspring(par_id, offspring);
}


#ifdef _DEBUG
/*
*	dump_index()
*
*	Description
*		Debug function to dump the AFP index.
*
*	Returns
*		None
*/

void afp_file_index::dump_index(const INT32 par_id)
{
	std::vector<afp_index*> offspring;
	const auto count = get_offspring(par_id, offspring);

	DBGWRITE(dbg_level_info, L"Dumping index with %d elements.\n", count);

	for (const auto it : offspring)
	{
		dump_entry(it);
	}
}

void afp_file_index::dump_entry(const afp_index* entry)
{
    constexpr auto dbg_level = dbg_level_trace;

	DBGWRITE(dbg_level, L"Dumping entry for: %s\n", entry->sz_path.c_str());
	DBGWRITE(dbg_level, L"Par id:      %d\n", entry->par_id);
	DBGWRITE(dbg_level, L"Object id:   %d\n", entry->object_id);
	DBGWRITE(dbg_level, L"Object name: %s\n", entry->sz_object_name.c_str());
	DBGWRITE(dbg_level, L"Flags:       %d\n", entry->flags);
	DBGWRITE(dbg_level, L"Hash:        %lu\n", entry->path_hash);
	DBGWRITE(dbg_level, L"---------------\n", entry->path_hash);
}

#endif
