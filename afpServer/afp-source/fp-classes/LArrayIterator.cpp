/*
 *	LArrayIterator.cpp
 *	Created 12/21/09
 *
 *	Copyright © 2003-09, Michael J. Conrad. All Rights Reserved.
 */

#include "StdAfx.h"
#include "LArrayIterator.h"


/*
 * LArrayIterator()
 *
 * Description:
 *		Constructor for the iterator class. Pass in a pointer to LArray
 *      class as the only parameter.
 *
 * Returns: None
 */

LArrayIterator::LArrayIterator(const LArray& inArray, array_index inPosition)
{
	mArray = inArray;
	mCurrentIndex = inPosition;
}


/*
 * Next()
 *
 * Description:
 *		Returns the next item in the array. Returns TRUE if an element exists.
 *
 * Returns: None
 */

bool LArrayIterator::Next(void* data)
{
	bool success;

	//
	//Note: GetItemAtIndex() returns a COPY of the data in the element
	//

	success = mArray.GetItemAtIndex(mCurrentIndex++, data);
	CalcNextIndex();

	return( success );
}


/*
 * CalcNextIndex()
 *
 * Description:
 *		Calculate the next index position in the array.
 *
 * Returns: None
 */

void LArrayIterator::CalcNextIndex()
{
	mCurrentIndex++;

	if (mCurrentIndex > mArray.NumItems())
	{
		mCurrentIndex = LArray::array_index_after_end;
	}
}

