/*
 *	AFPArray.h
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

template <class T, class D = std::default_delete<T>> class AFPArray
{
public:

    /*
    * AFPArray()
    *
    * Description:
    *		Initializer for the AFPArray class.
    *
    * Returns: None
    */
    AFPArray()
    {
        InitializeCriticalSection(&mMutex);
    }


    /*
    * ~AFPArray()
    *
    * Description:
    *		Destructor, deallocate the memory used for the buffer.
    *
    * Returns: None
    */
    virtual ~AFPArray()
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
    void AddItem(std::unique_ptr<T,D>& data)
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

        typename std::vector<std::unique_ptr<T,D>>::iterator it =
            std::find_if(mData.begin(), mData.end(), [=](const std::unique_ptr<T>& item)
        {
            return memcmp(item.get(), data, sizeof(T)) == 0;
        });

        if (it != mData.end())
        {
            DBGWRITE(DbgLevelInfo, L"[AFPArray<T>::RemoveItem]Removing item from array.\n");
            mData.erase(it);
        }
    }

    /*
    * GetItem()
    *
    * Description:
    *		Returns the item (T*) located at the position.
    *
    * Returns: T*
    */
    T* GetItem(uint32 index)
    {
        std::unique_ptr<T, D> &up = mData.at(index);
        return up.get();
    }

	UINT32 NumItems() const { return mData.size(); }
    static UINT32 ItemSize() { return sizeof(T); }

    // So you can iterate through the vector array.
    typename std::vector<std::unique_ptr<T>>::iterator begin() { return mData.begin(); }
    typename std::vector<std::unique_ptr<T>>::iterator end() { return mData.end(); }

    CRITICAL_SECTION* Mutex() { return &mMutex; }

protected:

    std::vector<std::unique_ptr<T,D>> mData;

	CRITICAL_SECTION mMutex;
};
