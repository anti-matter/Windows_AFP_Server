/*
 *	LFileIndex.cpp
 *
 *	Copyright © Michael J. Conrad. All Rights Reserved.
 */

#include "StdAfx.h"

#include "afp.h"
#include "afpworker.h"
#include "LFileIndex.h"
#include <algorithm>

const int FirstDirectoryId = 1;

/*
 *	LFileIndex()
 *
 *	Description
 *		Constructor for index class.
 *
 *	Returns
 *		None
 */

LFileIndex::LFileIndex(bool ownsIndexEntries)
    : mNextID(FirstDirectoryId)
    , mOwnsEntries(ownsIndexEntries)
    , mRefCount(0)
{
    InitializeCriticalSection(&mIndexCriticalSection);
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
    AutoLock lock(&mIndexCriticalSection);
    auto entry = std::make_unique<AFPINDEX>();

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

    // Save as the std::move() ambiguates entry.
    auto result = entry.get();

    mIndex.push_back(std::move(entry));

	//
	//If the caller wants us to return the new directory ID, then do so.
	//
	if (isDirectory && pNewDirID != nullptr)
	{
		*pNewDirID = result->objectID;
	}

    return result;
}


/*
 *	GetEntry()
 *
 *	Description
 *		Get an entry from the array based on its object ID.
 *
 *	Returns
 *		LPAFPINDEX
 */

LPAFPINDEX LFileIndex::GetEntry(uint32 objectID)
{
    AutoLock lock(&mIndexCriticalSection);

    std::vector<std::unique_ptr<AFPINDEX>>::iterator it = std::find_if(mIndex.begin(), mIndex.end(),[=](const std::unique_ptr<AFPINDEX>& i)
    {
        return objectID == i->objectID;
    });

    if (it != mIndex.end())
    {
        return it->get();
    }
    else
    {
        DBGWRITE(
            DbgLevelInfo,
            L"[LFileIndex::GetEntry]Failed to find objectID! (%d)\n", objectID);
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
    AutoLock lock(&mIndexCriticalSection);

    std::vector<std::unique_ptr<AFPINDEX>>::iterator it = std::find_if(mIndex.begin(), mIndex.end(), [=](const std::unique_ptr<AFPINDEX>& i)
    {
        return objectID == i->objectID;
    });

    if (it == mIndex.end())
    {
        DBGWRITE(
            DbgLevelInfo,
            L"[LFileIndex::RemoveEntry]Failed to find objectID! (%d)\n", objectID);

        ASSERT(0);
        return;
    }

    auto isDirectory = (it->get()->flags & FLAG_INDEX_ISDIR) != 0;

    mIndex.erase(it);

	//
	//If the entry is a directory, then that means we have to delete all entries
	//in the index that are sub-files/folders to this directory.
	//
	if ((isDirectory) && (deleteDirectorySubTree))
	{
        // Find elements in the vector who's parent dir id is the same
        // as the object we just removed.

        for (auto subIt = mIndex.begin(); subIt < mIndex.end();  ++subIt)
        {
            auto element = subIt->get();

            if (element->parID != objectID)
                continue;

            // We use a worker thread to remove the indexes so we don't spend
            // too much time here blocking the thread.

            auto pWorkerElement = static_cast<PAFP_WORKER_ELEMENT>(DBG_ALLOC(sizeof(AFP_WORKER_ELEMENT)));
            if (pWorkerElement != nullptr)
            {
                //
                //Increment the ref counter so we wait before getting deleted.
                //
                IncRef();

                pWorkerElement->pParam1 = this;
                pWorkerElement->pParam2 = element;
                pWorkerElement->dwWorkerFunction = AFP_WORKER_DELETE_INDEX_ENTRY;

                AFPWorkerAddElementToWorkerQueue(pWorkerElement);
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
        AutoLock lock(&mIndexCriticalSection);

        for (auto subIt = mIndex.begin(); subIt < mIndex.end(); ++subIt)
        {
            auto subEntry = subIt->get();
            if (subEntry->parID != pEntry->objectID)
                continue;

            UpdateEntry(subEntry, pEntry, nullptr);
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

    AutoLock lock(&mIndexCriticalSection);

    std::vector<std::unique_ptr<AFPINDEX>>::iterator it = std::find_if(mIndex.begin(), mIndex.end(), [=](const std::unique_ptr<AFPINDEX>& i)
    {
        return lstrcmp(i->szPath, fullPath) == 0;
    });

    if (it == mIndex.end())
    {
        DBGWRITE(
            DbgLevelInfo,
            L"[LFileIndex::FindEntryWithFullPath]Failed to find object! (%s)\n", fullPath);

        return nullptr;
    }

    return it->get();
}


/*
 *	GetOffspring()
 *
 *	Description
 *		Returns a new std::vector<> that contains only the offspring
 *		for a particular directory.
 *
 *		newIndex must already be allocated by caller but can be null if
 *		you just want the count.
 *
 *	Returns
 *		LFileIndex*
 */

uint32 LFileIndex::GetOffspring(uint32 parID, std::vector<LPAFPINDEX>** newIndex)
{
    auto subIndex = new std::vector<LPAFPINDEX>();

    AutoLock lock(&mIndexCriticalSection);

    for (auto it = mIndex.begin(); it < mIndex.end(); ++it)
    {
        auto entry = it->get();
        if (entry->parID != parID)
            continue;

        subIndex->push_back(it->get());
    }

    if (newIndex != nullptr)
    {
        *newIndex = subIndex;
        return subIndex->size();
    }

    uint32 count = subIndex->size();
    delete subIndex;

    return count;
}


#ifdef _DEBUG
/*
*	DumpIndex()
*
*	Description
*		Debug function to dump the AFP index.
*
*	Returns
*		None
*/

void LFileIndex::DumpIndex()
{
    DBGWRITE(DbgLevelInfo, L"Dumping index...\n");

    for (auto it = mIndex.begin(); it < mIndex.end(); ++it)
    {
        auto entry = it->get();

        DBGWRITE(DbgLevelInfo,
            L"ID = %d parID = %d\tPath = %s\n", entry->objectID, entry->parID, entry->szPath);
    }
}

#endif
