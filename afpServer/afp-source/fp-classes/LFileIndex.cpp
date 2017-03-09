/*
 *	LFileIndex.cpp
 *
 *	Copyright © Michael J. Conrad. All Rights Reserved.
 */

#include "StdAfx.h"

#include "afp.h"
#include "afpworker.h"
#include "LFileIndex.h"

#define FIRST_DIR_ID			1
#define INDEX_BLOCK_GROW_SIZE	256

/*
 *	LFileIndex()
 *
 *	Description
 *		Constructor for index class.
 *
 *	Returns
 *		None
 */

LFileIndex::LFileIndex(bool ownsIndexEntries) : LArray(INDEX_BLOCK_GROW_SIZE)
{
	mNextID = FIRST_DIR_ID;
	mOwnsEntries = ownsIndexEntries;
	mRefCount = 0;
}


/*
 *	~LFileIndex()
 *
 *	Description
 *		Destructor for index class.
 *
 *	Returns
 *		None
 */

LFileIndex::~LFileIndex()
{
	LPAFPINDEX lpi;

	//
	//Another thread is still relying on us being around, wait for
	//the thread to be done with us.
	//
	while (mRefCount > 0)
	{
		DBGWRITE(DbgLevelInfo, L"[LFileIndex::~LFileIndex]Waiting for ref count before delete\n");
		ASSERT(0); //Remove if you ever hit this

		Sleep(500);
	}

	//
	//If this instance owns the entries in the array, then we free all
	//memory used by the entries as well.
	//
	if (mOwnsEntries)
	{
		DBGWRITE(DbgLevelInfo, L"[LFileIndex::~LFileIndex]Deleting index entries...\n");

		Rewind();

		while (Next(&lpi))
		{
			DBG_FREE(lpi);
		}
	}
}


/*
 *	AddEntry()
 *
 *	Description
 *		Add a new index entry to the array, this method is for
 *		when the index entry is already created.
 *
 *	Returns
 *		None
 */

void LFileIndex::AddEntry(LPAFPINDEX lpi)
{
	AddItem(&lpi);
}


/*
 *	AddEntry()
 *
 *	Description
 *		Add a new index entry to the array.
 *
 *	Returns
 *		LPAFPINDEX
 */

LPAFPINDEX LFileIndex::AddEntry(
	uint32 parID,
	PWCHAR pObjectName,
	PWCHAR pObjectPath,
	bool isDirectory,
	uint32* pNewDirID
)
{
	auto entry = static_cast<LPAFPINDEX>(DBG_ALLOC(sizeof(AFPINDEX)));

	if (entry == nullptr)
	{
		DBGWRITE(
			DbgLevelWarning,
			L"[LFileIndex::AddEntry]Allocation of index element failed!\n", GetLastError()
		);
		ASSERT(0);

		return nullptr;
	}

	//
	//Set the correct flags if it's a directory.
	//
	if (isDirectory)
	{
		entry->flags |= FLAG_INDEX_ISDIR;
	}

	//
	//Copy the name and path into the index entry.
	//
	StringCbCopy(entry->szObjectName, sizeof(entry->szObjectName), pObjectName);
	StringCbCopy(entry->szPath, sizeof(entry->szPath), pObjectPath);

	entry->objectID = mNextID++;
	entry->parID = parID;
	entry->parEntry = GetEntry(parID);

	//
	//We get a hash to the path string so that we can perform faster,
	//more efficient searches for full paths.
	//
	entry->pathHash = HashString(pObjectPath);

	AddItem(&entry);

	//
	//If the caller wants us to return the new directory ID, then do so.
	//
	if (isDirectory && pNewDirID != nullptr)
	{
		*pNewDirID = entry->objectID;
	}

	return entry;
}


/*
 *	GetEntry()
 *
 *	Description
 *		Get an entry from the array based on its object ID. Optionally
 *		returns the actual position in the array in pIndex.
 *
 *	Returns
 *		LPAFPINDEX
 */

LPAFPINDEX LFileIndex::GetEntry(uint32 objectID, array_index* pIndex)
{
	array_index index;

	if (pIndex != nullptr)
	{
		*pIndex = array_index_invalid;
	}

	index = min(objectID, NumItems());
	auto afp_index = GetEntryByIndex(index);

	//
	//The array index should always(?) be lower than the object ID
	//if it wasn't found in the first attempt above.
	//
	while (afp_index && afp_index->objectID != objectID)
	{
		afp_index = GetEntryByIndex(--index);
	}

#ifdef _DEBUG
	if (afp_index == nullptr || afp_index->objectID != objectID)
	{
		DBGWRITE(
			DbgLevelInfo,
			L"[LFileIndex::GetEntry]Failed to find objectID! (lpi->objectID=%d,objectID=%d,index=%d)\n",
			afp_index != nullptr ? afp_index->objectID, objectID, index : 0, objectID, index
		);
	}
#endif

	//
	//If not NULL, return the actual index position in the array
	//in pIndex.
	//
	if (pIndex != nullptr && afp_index)
	{
		*pIndex = index;
	}

	return afp_index;
}


/*
 *	GetEntryByIndex() [private]
 *
 *	Description
 *		Get an entry from the array based on its index in the array.
 *
 *	Returns
 *		None
 */

LPAFPINDEX LFileIndex::GetEntryByIndex(array_index index)
{
	LPAFPINDEX afp_index = nullptr;

	if (GetItem(index, &afp_index))
	{
		return afp_index;
	}

	return nullptr;
}


/*
 *	RemoveEntry()
 *
 *	Description
 *		Remove an entry from the array based on a pointer to the entry data.
 *
 *	Returns
 *		None
 */

void LFileIndex::RemoveEntry(LPAFPINDEX entry, bool deleteDirectorySubTree)
{
	RemoveEntry(entry->objectID, deleteDirectorySubTree);
}


/*
 *	RemoveEntry()
 *
 *	Description
 *		Remove an entry from the array based on the object's id.
 *
 *	Returns
 *		None
 */

void LFileIndex::RemoveEntry(uint32 objectID, bool deleteDirectorySubTree)
{
	array_index ai;
	auto afp_index = GetEntry(objectID, &ai);

	if (afp_index == nullptr || ai == array_index_invalid)
	{
		DBGWRITE(
			DbgLevelInfo,
			L"[LFileIndex::RemoveEntry]ID not found! (%d)\n", objectID
		);

		ASSERT(0);

		return;
	}

	RemoveItem(ai);

	//
	//If the entry is a directory, then that means we have to delete all entries
	//in the index that are sub-files/folders to this directory.
	//
	if ((afp_index->flags & FLAG_INDEX_ISDIR) && (deleteDirectorySubTree))
	{
		auto tempIndex = std::make_unique<LFileIndex>();

		DBGWRITE(
			DbgLevelAll,
			L"[LFileIndex::RemoveEntry]Deleting items in sub-tree\n"
		);

		//
		//Get a temporary index object that holds all of the offspring
		//in the dir. This has the advantage that items can now be added
		//or removed from the main array with no effect on this operation.
		//
		if (GetOffspring(afp_index->objectID, tempIndex.get()) > 0)
		{
			LPAFPINDEX sub_lpi = nullptr;

			while (tempIndex->Next(&sub_lpi))
			{
				auto pWorkerElement = static_cast<PAFP_WORKER_ELEMENT>(DBG_ALLOC(sizeof(AFP_WORKER_ELEMENT)));
				if (pWorkerElement != nullptr)
				{
					//
					//Increment the ref counter so we wait before getting deleted.
					//
					IncRef();

					pWorkerElement->pParam1 = this;
					pWorkerElement->pParam2 = sub_lpi;
					pWorkerElement->dwWorkerFunction = AFP_WORKER_DELETE_INDEX_ENTRY;

					AFPWorkerAddElementToWorkerQueue(pWorkerElement);
				}
			}
		}
	}
}


/*
 *	UpdateEntry()
 *
 *	Description
 *		Update the entry's parID and/or name.
 *
 *	Returns
 *		None
 */

void LFileIndex::UpdateEntry(
	LPAFPINDEX pEntry,
	LPAFPINDEX pNewParent,
	PWCHAR pNewName
)
{
	DBGWRITE(
		DbgLevelInfo,
		L"[LFileIndex::UpdateEntry]Updating entry:\n\toldName=%s\n\tnewName=%s\n\tnewParID=%d, oldParID=%d\n",
		pEntry->szObjectName,
		pNewName,
		pEntry->parID,
		pNewParent->objectID
	);

	//
	//First, update the object's name and full path to the new one.
	//
	if (pNewName != nullptr && lstrlen(pNewName) > 0)
	{
		StringCbCopy(pEntry->szObjectName, sizeof pEntry->szObjectName, pNewName);
	}

	//
	//Update the new full path and full path hash
	//
	StringCbPrintf(
		pEntry->szPath,
		sizeof(pEntry->szObjectName),
		TEXT("%s\\%s"),
		pNewParent->szPath,
		pEntry->szObjectName
	);

	pEntry->pathHash = HashString(pEntry->szPath);

	//
	//Only if the object moved do we update the parent id and entry
	//
	if (pEntry->parEntry != pNewParent)
	{
		pEntry->parID = pNewParent->objectID;
		pEntry->parEntry = pNewParent;
	}

	//
	//If the entry that was moved is a directory, then we have to update the
	//full path of every object below it.
	//
	if (pEntry->flags & FLAG_INDEX_ISDIR)
	{
		LFileIndex tempIndex;
		LPAFPINDEX lpi;

		if (GetOffspring(pEntry->objectID, &tempIndex) > 0)
		{
			while (tempIndex.Next(&lpi))
			{
				UpdateEntry(lpi, pEntry, nullptr);
			}
		}
	}
}


/*
 *	UpdateEntryFlags()
 *
 *	Description
 *		Update the entry's flags field.
 *
 *	Returns
 *		None
 */

void LFileIndex::UpdateEntryFlags(
	LPAFPINDEX pEntry,
	uint16 flags
)
{
	//
	//If the "set item" flag is set, then we are setting the flags. Otherwise
	//we are clearing the bits.
	//
	if (flags & FLAG_INDEX_SETITEM)
	{
		if (flags & FLAG_INDEX_RESOPEN)
		{
			pEntry->flags |= FLAG_INDEX_RESOPEN;
		}

		if (flags & FLAG_INDEX_DATAOPEN)
		{
			pEntry->flags |= FLAG_INDEX_DATAOPEN;
		}

		if (flags & FLAG_INDEX_ATTROPEN)
		{
			pEntry->flags |= FLAG_INDEX_ATTROPEN;
		}
	}
	else
	{
		if (flags & FLAG_INDEX_RESOPEN)
		{
			pEntry->flags &= ~FLAG_INDEX_RESOPEN;
		}

		if (flags & FLAG_INDEX_DATAOPEN)
		{
			pEntry->flags &= ~FLAG_INDEX_DATAOPEN;
		}

		if (flags & FLAG_INDEX_ATTROPEN)
		{
			pEntry->flags &= ~FLAG_INDEX_ATTROPEN;
		}
	}
}


/*
 *	HashString()
 *
 *	Description
 *		Simple hash routine to reduce a string to a number.
 *
 *	Returns
 *		hash
 */

uint32 LFileIndex::HashString(PWCHAR string)
{
	uint32 hash = 0;

	while (*string != 0)
	{
		hash = hash * HASH_MULTIPLIER + *string;
		string++;
	}

	return hash;
}


/*
 *	FindEntryWithFullPath()
 *
 *	Description
 *		Finds an entry in the array by looking for its full path.
 *
 *	Returns
 *		LPAFPINDEX
 */

LPAFPINDEX LFileIndex::FindEntryWithFullPath(PWCHAR fullPath)
{
	if (fullPath == nullptr)
	{
		return nullptr;
	}

	auto hash = HashString(fullPath);

	Rewind();

	LPAFPINDEX lpi = nullptr;

	while (Next(&lpi))
	{
		if (lpi->pathHash == hash)
		{
			//
			//Make sure we have an exact match (look out for hash collisions)
			//

			if (!lstrcmp(lpi->szPath, fullPath))
			{
				return lpi;
			}
		}
	}

	return nullptr;
}


/*
 *	GetOffspring()
 *
 *	Description
 *		Returns a new LFileIndex array that contains only the offspring
 *		for a particular directory.
 *
 *		newIndex must already be allocated by caller but can be null if
 *		you just want the count.
 *
 *	Returns
 *		LFileIndex*
 */

uint32 LFileIndex::GetOffspring(uint32 parID, LFileIndex* newIndex)
{
	LPAFPINDEX lpi;
	uint32 count = 0;
	array_index i = 1;

	while ((lpi = GetEntryByIndex(i)) != nullptr)
	{
		if (lpi->parID == parID)
		{
			if (newIndex != nullptr)
			{
				newIndex->AddEntry(lpi);
			}

			count++;
		}

		i++;
	}

	return count;
}
