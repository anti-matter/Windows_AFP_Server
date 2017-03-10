/*
 *	LFileIndex.h
 *	Created 09/25/05
 *
 *	Copyright © 2005, Michael J. Conrad. All Rights Reserved.
 */


#pragma once

#include "afphelp.h"

#define HASH_MULTIPLIER		1024
#define INDEX_OWNS_ENTRIES	true

//
//These flags are used to keep state of file objects in the index.
//
enum
{
	FLAG_INDEX_ISDIR = 0x01,
	FLAG_INDEX_RESOPEN = 0x02,
	FLAG_INDEX_DATAOPEN = 0x04,
	FLAG_INDEX_HASCHARMAP = 0x08,
	FLAG_INDEX_ATTROPEN = 0x10,
	FLAG_INDEX_SETITEM = 0x80
};

//
//This is a structure to keep track of a single directory or file.
//We use this to map AFP dirID's to windows directories and file
//ID's to files.
//
typedef struct _AFP_INDEX
{
	DWORD parID;
	PVOID parEntry;
	DWORD objectID;
	WCHAR szObjectName[MAX_AFP30_NAME];
	WCHAR szPath[MAX_PATH];
	WORD flags;
	DWORD pathHash;
} AFPINDEX, *LPAFPINDEX;

class LFileIndex
{
public:
	LFileIndex();
	~LFileIndex();

	LPAFPINDEX AddEntry(
		uint32 parID,
		PWCHAR pObjectName,
		PWCHAR pObjectPath,
		bool isDirectory,
		uint32* pNewDirID = nullptr
	);
	LPAFPINDEX GetEntry(uint32 objectID);

	void RemoveEntry(LPAFPINDEX entry, bool deleteDirectorySubTree = false);
	void RemoveEntry(uint32 objectID, bool deleteDirectorySubTree = false);

	void UpdateEntry(
		LPAFPINDEX pEntry,
		LPAFPINDEX pNewParent,
		PWCHAR pNewName
	);

	static void UpdateEntryFlags(
		LPAFPINDEX pEntry,
		uint16 flags
	);

	static uint32 HashString(PWCHAR string);
	LPAFPINDEX FindEntryWithFullPath(PWCHAR pFullPath);
	uint32 GetOffspring(uint32 parID, std::vector<LPAFPINDEX>** newIndex = nullptr);

	uint32 NextID() { return mNextID++; }

	void IncRef() { mRefCount++; }
	void DecRef() { mRefCount--; }
	uint32 GetRef() const { return mRefCount; }

#ifdef _DEBUG
    void DumpIndex();
#endif

private:
    CRITICAL_SECTION mIndexCriticalSection;

    std::vector<std::unique_ptr<AFPINDEX>> mIndex;

	uint32 mNextID;

	//
	//This is needed as we pass some operations off to a worker thread.
	//We don't want to get deleted when a worked may be trying to call us.
	//
	uint32 mRefCount;
};
