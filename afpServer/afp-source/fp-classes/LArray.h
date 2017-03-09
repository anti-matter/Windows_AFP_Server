/*
 *	LArray.h
 *	Created 03/11/2003
 *
 *	Copyright © 2003, Michael J. Conrad. All Rights Reserved.
 */

#pragma once

#include "AutoLock.h"
#include "afphelp.h"

#define LARRAY_DEFAULT_GROW_SIZE		16
#define LARRAY_DEFAULT_ELEMENT_SIZE		sizeof(uint32*)
#define LARRAY_MAKE_THREADSAFE			true
#define LARRAY_DONTMAKE_THREADSAFE		false

typedef struct
{
	uint32 growSize;
	uint32 elementSize;
	uint32 itemCount;
} LARRAY_SERIALIZATION_HEADER;

typedef bool (*search_proc)(void* currentData, void* compareParam);
typedef uint32 array_index;

class LArray
{
public:
	enum
	{
		array_index_invalid = 0,
		array_index_firstitem = 1,

		array_index_died = -1,
		array_index_after_end = -2
	};

	explicit LArray(
		uint32 growSize = LARRAY_DEFAULT_GROW_SIZE,
		uint32 inElementSize = LARRAY_DEFAULT_ELEMENT_SIZE
	);

	explicit LArray(std::unique_ptr<uint8>& serializedBuffer);

	virtual ~LArray();

	void AddItem(void* data);
	void RemoveItem(void* data);
	void RemoveItem(array_index index);
	void ReplaceItem(array_index index, uint8* data);
	bool GetItem(array_index index, void* data);

	bool Next(void* data);
	void Rewind()
	{
		AutoLock lock(&mMutex);
		mIterationCurrent = 1;
	}

	uint32 NumItems() const { return mItemCount; }
	uint32 ItemSize() const { return mElementSize; }

	void ToFile(std::wstring path);

	uint32 GetSerializedSize() const
	{
		return (mItemCount * mElementSize) + sizeof(LARRAY_SERIALIZATION_HEADER);
	}

	array_index Search(
		void* inCompareParam,
		search_proc inSearchProc
	);

private:

	bool Grow();

	array_index FindIndex(void* data);
	void* ItemPointer(array_index index) const;

	bool Serialize() const;
	bool Deserialize();

	/*
	 * MutableBuffer()
	 * 
	 * Description:
	 *		Get a pointer to use when accessing the buffer.
	 */
	uint8* MutableBuffer() const
	{
		return mDataBuffer.get() + sizeof(LARRAY_SERIALIZATION_HEADER);
	}

	//
	//This is the buffer that holds our array items.
	//
	std::unique_ptr<uint8> mDataBuffer;

	//
	//Maximum number of items the buffer can currently hold based on the
	//current size of the buffer and how big to grow the buffer when
	//reallocation is necessary.
	//
	uint32 mSlots;
	uint32 mGrowSize;
	uint32 mElementSize;

	//
	//This is how many items are currently in the array buffer.
	//
	uint32 mItemCount;

	//
	//This keeps track of the current array location during iteration.
	//
	uint32 mIterationCurrent;

	//
	//Need this critical section so the array is thread safe.
	//
	CRITICAL_SECTION mMutex;
};
