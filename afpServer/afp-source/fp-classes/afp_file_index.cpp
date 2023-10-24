#include "StdAfx.h"
#include <algorithm>

#include "afp.h"
#include "afpworker.h"
#include "afp_file_index.h"

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

afp_file_index::afp_file_index()
	: m_next_id_(first_directory_id)
	  , m_ref_count_(0)
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

afp_file_index::~afp_file_index()
{
	//
	//Another thread is still relying on us being around, wait for
	//the thread to be done with us.
	//
	while (m_ref_count_ > 0)
	{
		DBGWRITE(dbg_level_info, L"Waiting for ref count before delete\n");
		ASSERT(0); //Remove if you ever hit this

		Sleep(500);
	}
}


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
    const uint32 par_id,
    const std::wstring& p_object_name,
    const std::filesystem::path& p_object_path,
    const bool is_directory,
	UINT32* p_new_dir_id
)
{
	std::lock_guard guard(mutex_);
	auto entry = std::make_unique<afp_index>();

	if (is_directory)
	{
		entry->flags |= flag_index_is_dir;
	}

	entry->sz_object_name = p_object_name;
	entry->sz_path = p_object_path;
	entry->object_id = m_next_id_++;
	entry->par_id = par_id;
	entry->par_entry = get_entry(par_id);

	// We get a hash to the path string so that we can perform faster,
	// more efficient searches for full paths.
	entry->path_hash = std::hash<std::wstring>{}(p_object_path);

	// Save as the std::move() makes the entry confusing.
	const auto result = entry.get();

	m_index_.push_back(std::move(entry));

	// If the caller wants us to return the new directory ID, then do so.
	if (is_directory && p_new_dir_id != nullptr)
	{
		*p_new_dir_id = result->object_id;
	}

	return result;
}


/*
 *	get_entry()
 *
 *	Description
 *		Get an entry from the array based on its object ID.
 *
 *	Returns
 *		afp_index*
 */

afp_index* afp_file_index::get_entry(const uint32 object_id)
{
	std::lock_guard guard(mutex_);

	const auto it = std::find_if(m_index_.begin(), m_index_.end(), [=](const std::unique_ptr<afp_index>& i)
	{
		return object_id == i->object_id;
	});

	if (it != m_index_.end())
	{
		return it->get();
	}
	DBGWRITE(
		dbg_level_info,
		L"Failed to find objectID! (%d)\n", object_id);

	return nullptr;
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

void afp_file_index::remove_entry(const afp_index* entry, const bool delete_directory_sub_tree)
{
	remove_entry(entry->object_id, delete_directory_sub_tree);
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

void afp_file_index::remove_entry(const uint32 object_id, const bool delete_directory_sub_tree)
{
	std::lock_guard guard(mutex_);

	const auto it = std::find_if(m_index_.begin(), m_index_.end(), [=](const std::unique_ptr<afp_index>& i)
	{
		return object_id == i->object_id;
	});

	if (it == m_index_.end())
	{
		DBGWRITE(
			dbg_level_info,
			L"Failed to find objectID! (%d)\n", object_id);

		ASSERT(0);
		return;
	}

	const auto is_directory = (it->get()->flags & flag_index_is_dir) != 0;

	m_index_.erase(it);

	//
	//If the entry is a directory, then that means we have to delete all entries
	//in the index that are sub-files/folders to this directory.
	//
	if ((is_directory) && (delete_directory_sub_tree))
	{
		// Find elements in the vector who's parent dir id is the same
		// as the object we just removed.

		for (auto sub_it = m_index_.begin(); sub_it < m_index_.end(); ++sub_it)
		{
			const auto element = sub_it->get();

			if (element->par_id != object_id)
				continue;

			// We use a worker thread to remove the indexes so we don't spend
			// too much time here blocking the thread.

			const auto p_worker_element = static_cast<PAFP_WORKER_ELEMENT>(DBG_ALLOC(sizeof(AFP_WORKER_ELEMENT)));
			if (p_worker_element != nullptr)
			{
				//
				//Increment the ref counter so we wait before getting deleted.
				//
				inc_ref();

				p_worker_element->pParam1 = this;
				p_worker_element->pParam2 = element;
				p_worker_element->dwWorkerFunction = AFP_WORKER_DELETE_INDEX_ENTRY;

				AFPWorkerAddElementToWorkerQueue(p_worker_element);
			}
		}
	}
}


void afp_file_index::update_entry(afp_index* entry, afp_index* new_parent, const PWCHAR new_name)
{
	DBGWRITE(
		dbg_level_info,
		L"Updating entry:\n\toldName=%s\n\tnewName=%s\n\tnewParID=%d, oldParID=%d\n",
		entry->sz_object_name.c_str(),
		new_name,
		entry->par_id,
		new_parent->object_id
	);

	std::lock_guard guard(mutex_);

	// First, update the object's name and full path to the new one.
	if (new_name != nullptr && lstrlen(new_name) > 0)
	{
		entry->sz_object_name = new_name;
	}

	// Update the new full path and full path hash
	entry->sz_path = new_parent->sz_path / entry->sz_object_name;
	entry->path_hash = std::hash<std::wstring>{}(entry->sz_path);

	// Only if the object moved do we update the parent id and entry
	if (entry->par_entry != new_parent)
	{
		entry->par_id = new_parent->object_id;
		entry->par_entry = new_parent;
	}

	// If the entry that was moved is a directory, then we have to update the
	// full path of every object below it.
	if (entry->flags & flag_index_is_dir)
	{
		std::lock_guard guard(mutex_);

		for (auto sub_it = m_index_.begin(); sub_it < m_index_.end(); ++sub_it)
		{
			const auto sub_entry = sub_it->get();
			if (sub_entry->par_id != entry->object_id)
				continue;

			update_entry(sub_entry, entry, nullptr);
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

void afp_file_index::update_entry_flags(afp_index* p_entry, const uint16 flags)
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
		DBGWRITE(dbg_level_info, L"Failed to find indexed object! (%s)\n", full_path.c_str());
		return nullptr;
	}

	return it->get();
}

afp_index* afp_file_index::find(const DWORD directory_id, const std::wstring& path)
{
	std::lock_guard guard(mutex_);

    if (const auto it = std::ranges::find_if(m_index_, [=](const std::unique_ptr<afp_index>& i)
    {
			return i->object_id == directory_id;
    }); it != m_index_.end())
	{
        const std::filesystem::path temp_path = it->get()->sz_path / path;
		return find(temp_path);
	}

	return nullptr;
}


/*
 *	get_offspring()
 *
 *	Description
 *		Returns a new std::vector<> that contains only the offspring
 *		for a particular directory.
 *
 *		newIndex must already be allocated by caller but can be null if
 *		you just want the count.
 *
 *	Returns
 *		afp_file_index*
 */

uint32 afp_file_index::get_offspring(const uint32 par_id, std::vector<afp_index*>** new_index)
{
	const auto sub_index = new std::vector<afp_index*>();

	std::lock_guard guard(mutex_);

	for (auto it = m_index_.begin(); it < m_index_.end(); ++it)
	{
		const auto entry = it->get();
		if (entry->par_id != par_id)
			continue;

		sub_index->push_back(it->get());
	}

	if (new_index != nullptr)
	{
		*new_index = sub_index;
		return sub_index->size();
	}

	const uint32 count = sub_index->size();
	delete sub_index;

	return count;
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

void afp_file_index::dump_index()
{
	DBGWRITE(dbg_level_info, L"Dumping index...\n");

	for (auto it = m_index_.begin(); it < m_index_.end(); ++it)
	{
		const auto entry = it->get();

		DBGWRITE(dbg_level_info,
		         L"ID = %d parID = %d\tPath = %s\n", entry->object_id, entry->par_id, entry->sz_path.c_str());
	}
}
#endif
