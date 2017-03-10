/*
 *	LArray.cpp
 *
 *	Copyright © Michael J. Conrad. All Rights Reserved.
 *
 *	This class is used to track a list of items, usually objects.
 */

#include "StdAfx.h"
#include <fstream>
#include <iterator>
#include "AutoLock.h"
#include "LArray.h"
#include "afpdebug.h"


/*
 * ~LArray()
 *
 * Description:
 *		Destructor, deallocate the memory used for the buffer.
 *
 * Returns: None
 */

template <class T>
LArray<T>::~LArray()
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

template <class T>
void LArray<T>::AddItem(std::unique_ptr<T>& data)
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

template <class T>
void LArray<T>::RemoveItem(T* data)
{
    AutoLock lock(&mMutex);

    auto it = std::find(mData.begin(), mData.end(), data);
    if (it != mData.end())
    {
        DBGWRITE(DbgLevelInfo, L"[LArray<T>::RemoveItem]Removing item from array.\n");
        mData.erase(it);
    }
}


/*
* ToFile()
*
* Description:
*		Saves the array contents to a file.
*
* Returns: None
*/

template <class T>
bool LArray<T>::ToFile(std::wstring path)
{
    AutoLock lock(&mMutex);

    std::ofstream outFile(path.c_str(), std::ios::out | std::ofstream::binary);
    if (outFile)
    {
        std::copy(mData.begin(), mData.end()), std::ostreambuf_iterator<T>(outFile);

        return true;
    }
    else
    {
        DBGWRITE(DbgLevelInfo, L"[LArray::ToFile]Failed to open file for writing\n");
    }

    return false;
}


/*
* FromFile()
*
* Description:
*		Reads in the array data from a file.
*
* Returns: None
*/

template <class T>
bool LArray<T>::FromFile(std::wstring path)
{
    AutoLock lock(&mMutex);

    std::ifstream inFile(path.c_str(), std::ios::in | std::ifstream::binary);
    if (inFile)
    {
        std::istreambuf_iterator<T> it(inFile);
        std::copy(it, std::istreambuf_iterator<T>{}, std::back_inserter(mData));

        DBGWRITE(DbgLevelInfo, L"[LArray::FromFile]Successfully read in desktop database (%s)\n", path.c_str());

        return true;
    }

    DBGWRITE(DbgLevelWarning, L"[LArray::FromFile]FAILED to read in desktop database (%s)\n", path.c_str());

    return false;
}
