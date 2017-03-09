/*
 *	LArray.cpp
 *
 *	Copyright © Michael J. Conrad. All Rights Reserved.
 *
 *	This class is used to track a list of items, usually objects.
 */

#include "StdAfx.h"
#include "LArray.h"


/*
 * LArray()
 *
 * Description:
 *		Initializer for the LArray class. You can pass in the optional size
 *		of each element in the array. All elements must be the same size
 *		that are added to the array.
 *
 * Returns: None
 */

LArray::LArray(uint32 growSize, uint32 inElementSize)
	: mSlots(growSize)
	  , mGrowSize(growSize)
	  , mElementSize(inElementSize)
	  , mItemCount(0)
	  , mIterationCurrent(array_index_firstitem)
{
	//
	//Allocate the initial buffer for holding the elements of the array.
	//
	mDataBuffer = std::unique_ptr<uint8>(new uint8[growSize * inElementSize + sizeof(LARRAY_SERIALIZATION_HEADER)]);

	InitializeCriticalSection(&mMutex);
}


/*
 * LArray()
 *
 * Description:
 *		Initialize an array class with an existing buffer that is the result
 *		of a serialization call. Used only for deserialization.
 *
 * Returns: None
 */

LArray::LArray(std::unique_ptr<uint8>& serializedBuffer)
{
	mDataBuffer = std::move(serializedBuffer);
	mIterationCurrent = 1;

	InitializeCriticalSection(&mMutex);

	Deserialize();
}


/*
 * ~LArray()
 *
 * Description:
 *		Destructor, deallocate the memory used for the buffer.
 *
 * Returns: None
 */

LArray::~LArray()
{
	DeleteCriticalSection(&mMutex);
}


/*
 * AddItem()
 *
 * Description:
 *		Add a new item to the array to track.
 *
 * Returns: None
 */

void LArray::AddItem(void* data)
{
	AutoLock lock(&mMutex);

	//
	//Check to make sure we haven't hit the buffer space limit. If so,
	//reallocate the buffer to hold more items.
	//
	if (mItemCount >= mSlots)
	{
		//
		//No more room in the buffer to hold the new data.
		//

		if (Grow() == false)
		{
			//
			//Reallocation failed, we have no choice but to bail out here.
			//
			return;
		}
	}

	auto arrayPointer = MutableBuffer();

	memcpy(arrayPointer + mItemCount * mElementSize, data, mElementSize);

	mItemCount++;
}


/*
 * RemoveItem()
 *
 * Description:
 *		Remove an item from the array.
 *
 * Returns: None
 */

void LArray::RemoveItem(void* data)
{
	auto index = FindIndex(data);

	if (index != array_index_invalid)
	{
		RemoveItem(index);
	}
}


/*
 * RemoveItem()
 *
 * Description:
 *		Remove an item from the array.
 *
 * Returns: None
 */

void LArray::RemoveItem(array_index index)
{
	AutoLock lock(&mMutex);

	uint8* tempPtr;
	auto arrayPointer = MutableBuffer();

	ASSERT(index != array_index_invalid);
	ASSERT(arrayPointer != nullptr);

	if (index <= mItemCount && index >= 1)
	{
		tempPtr = arrayPointer + (index - 1) * mElementSize;

		mItemCount--;

		memcpy(tempPtr, tempPtr + mElementSize, (mItemCount - index + 1) * mElementSize);

		//
		//Readjust the iteration counter if an element was taken from the middle
		//of the array so if someone is in the middle of an iteration they won't
		//miss an item.
		//
		if (mIterationCurrent > 1)
		{
			mIterationCurrent--;
		}
	}
}


/*
* ReplaceItem()
*
* Description:
*		Replace the data at array index with new data.
*
* Returns: void
*/

void LArray::ReplaceItem(array_index index, uint8* data)
{
	AutoLock lock(&mMutex);

	ASSERT(data != nullptr);

	auto item = ItemPointer(index);
	if (item == nullptr)
	{
		return;
	}

	memcpy_s(item, mElementSize, data, mElementSize);
}


/*
 * FindIndex() [PRIVATE]
 *
 * Description:
 *		Finds the index # for an item within the array.
 *
 * Returns: None
 */

uint32 LArray::FindIndex(void* data)
{
	AutoLock lock(&mMutex);

	auto arrayPointer = MutableBuffer();

	if (arrayPointer == nullptr)
	{
		//
		//The array is empty, or likely there is a critical unexpected
		//failure since it should never be null.
		//

		ASSERT(0);

		return array_index_invalid;
	}

	auto tempPtr = arrayPointer;

	//
	//Cycle through all the elements stored in the array buffer
	//and compare supplied data with that stored.
	//
	for (uint32 i = 0; i < mItemCount; i++ , tempPtr += mElementSize)
	{
		if (!memcmp(data, tempPtr, mElementSize))
		{
			return i + 1;
		}
	}

	return array_index_invalid;
}


/*
 * Grow() [PRIVATE]
 *
 * Description:
 *		Reallocates the buffer, increasing it in size to hold more items.
 *
 * Returns: true if grow succeeded.
 */

bool LArray::Grow()
{
	auto newSize = mSlots + mGrowSize;

	DBGWRITE(DbgLevelInfo, L"[LArray::Grow]Growing the array, new block count = %d\n", newSize);

	auto bufferSize = newSize * mElementSize + sizeof(LARRAY_SERIALIZATION_HEADER);
	auto temp = std::unique_ptr<uint8>(new uint8[bufferSize]);

	if (temp == nullptr)
	{
		DBGWRITE(DbgLevelWarning, L"[LArray::Grow]****Growing the array failed!\n");
		ASSERT(0);

		return false;
	}

	memcpy_s(temp.get(), bufferSize, mDataBuffer.get(), mItemCount * mElementSize + sizeof(LARRAY_SERIALIZATION_HEADER));

	mSlots = newSize;
	mDataBuffer = std::move(temp);

	return true;
}


/*
 * Next()
 *
 * Description:
 *		Get the next item in the array during iteration.
 *
 * Returns: None
 */

bool LArray::Next(void* data)
{
	ASSERT(data != nullptr);
	return GetItem(mIterationCurrent++, data);
}


/*
 * GetItem()
 *
 * Description:
 *		Return a COPY of the item at index in the array. The items in the
 *		array begin at 1.
 *
 * Returns: None
 */

bool LArray::GetItem(array_index index, void* data)
{
	AutoLock lock(&mMutex);
	auto arrayPointer = MutableBuffer();

	ASSERT(data != nullptr);
	ASSERT(arrayPointer != nullptr);

	if (index >= array_index_firstitem && index <= mItemCount)
	{
		memcpy(data, arrayPointer + (index - 1) * mElementSize, mElementSize);
		return true;
	}

	return false;
}


/*
 * ItemPointer() [PRIVATE]
 *
 * Description:
 *		Return a ptr the actual place in the buffer where the data at
 *		index is stored.
 *
 *		NOTE: we don't do this within a critical section as we want to
 *		rely on the caller to set one. This is so the caller can reliably
 *		get a ptr to the data and modify it without the opportunity for
 *		data corruption.
 *
 * Returns: VOIDPTR
 */

void* LArray::ItemPointer(array_index index) const
{
	auto arrayPointer = MutableBuffer();

	if ((index >= array_index_firstitem) && (index <= mItemCount))
	{
		return static_cast<void*>(arrayPointer + (index - 1) * mElementSize);
	}

	return nullptr;
}

/*
 * Serialize()
 *
 * Description:
 *		Serialize all the necessary data into one continuous buffer. This
 *		is used for sending all the array data or saving it to disk for
 *		later restoration.
 *
 * Returns: None
 */

bool LArray::Serialize() const
{
	LARRAY_SERIALIZATION_HEADER header;

	//
	//All we really do here is update the header information.
	//

	header.growSize = mGrowSize;
	header.elementSize = mElementSize;
	header.itemCount = mItemCount;

	memcpy(mDataBuffer.get(), &header, sizeof(LARRAY_SERIALIZATION_HEADER));

	return true;
}


/*
 * Deserialize()
 *
 * Description:
 *		Deserializes a serialized LArray buffer.
 *
 * Returns: None
 */

bool LArray::Deserialize()
{
	LARRAY_SERIALIZATION_HEADER header;

	memcpy(&header, mDataBuffer.get(), sizeof(LARRAY_SERIALIZATION_HEADER));

	mGrowSize = header.growSize;
	mElementSize = header.elementSize;
	mItemCount = header.itemCount;
	mSlots = mItemCount;

	return true;
}

/*
* ToFile()
*
* Description:
*		Saves the array contents to a file.
*
* Returns: None
*/

void LArray::ToFile(std::wstring path)
{
	auto hFile = CreateFile(
		path.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_HIDDEN,
		nullptr
	);

	if (hFile == INVALID_HANDLE_VALUE)
	{
		DBGWRITE(DbgLevelInfo, L"[LArray]CreateFile() failed opening DT! (%d)\n", GetLastError());
	}
	else
	{
		AutoLock lock(&mMutex);

		Serialize();

		uint32 bytesWritten = 0;

		auto success = WriteFile(
			hFile,
			mDataBuffer.get(),
			GetSerializedSize(),
			&bytesWritten,
			nullptr
		);

		if (!success)
		{
			DBGWRITE(DbgLevelInfo, L"[LArray]WriteFile() failed synchronizing DB (%d)\n", GetLastError());
		}
		else
		{
			DBGWRITE(DbgLevelInfo, L"[LArray]Synchronized, %d bytes written\n", bytesWritten);
		}

		CloseHandle(hFile);
	}
}


/*
 * Search()
 *
 * Description:
 *		Uses a provided search proc to search through the array
 *		looking for a matching element.
 *
 * Returns: a ptr to the element.
 */

array_index LArray::Search(void* compareParam, search_proc searchProc)
{
	auto data = std::unique_ptr<uint8>(new uint8[mElementSize]);

	if (data == nullptr)
	{
		return array_index_invalid;
	}

	Rewind();

	while (Next(data.get()))
	{
		if (searchProc(data.get(), compareParam) == true)
		{
			//
			//The compare proc returned true which means we found a match
			//

			return mIterationCurrent - 1;
		}
	}

	return array_index_invalid;
}
