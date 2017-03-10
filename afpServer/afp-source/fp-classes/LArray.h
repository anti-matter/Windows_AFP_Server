/*
 *	LArray.h
 *
 *	Copyright © Michael J. Conrad. All Rights Reserved.
 */

#pragma once

#include <fstream>
#include <iterator>
#include <vector>
#include <windows.h>
#include "AutoLock.h"
#include "afpdebug.h"

template <class T> class LArray
{
public:

    /*
    * LArray()
    *
    * Description:
    *		Initializer for the LArray class.
    *
    * Returns: None
    */
    LArray()
    {
        InitializeCriticalSection(&mMutex);
    }


    /*
    * ~LArray()
    *
    * Description:
    *		Destructor, deallocate the memory used for the buffer.
    *
    * Returns: None
    */
    virtual ~LArray()
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
    void AddItem(std::unique_ptr<T>& data)
    {
        AutoLock lock(&mMutex);
        mData.push_back(std::move(data));
    }
    

    /*
    * RemoveItem()
    *
    * Description:
    *		Remove an item from the array.
    *
    * Returns: None
    */
    void RemoveItem(T* data)
    {
        AutoLock lock(&mMutex);

        typename std::vector<std::unique_ptr<T>>::iterator it =
            std::find_if(mData.begin(), mData.end(), [=](const std::unique_ptr<T>& item)
        {
            return memcmp(item.get(), data, sizeof(T)) == 0;
        });

        if (it != mData.end())
        {
            DBGWRITE(DbgLevelInfo, L"[LArray<T>::RemoveItem]Removing item from array.\n");
            mData.erase(it);
        }
    }

	UINT32 NumItems() const { return mData.size(); }
    static UINT32 ItemSize() { return sizeof(T); }

    // So you can iterate through the vector array.
    typename std::vector<std::unique_ptr<T>>::iterator begin() { return mData.begin(); }
    typename std::vector<std::unique_ptr<T>>::iterator end() { return mData.end(); }

    CRITICAL_SECTION* Mutex() { return &mMutex; }

protected:

    std::vector<std::unique_ptr<T>> mData;

	CRITICAL_SECTION mMutex;
};
