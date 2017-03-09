/*
 *	LArrayIterator.h
 *	Created 12/21/09
 *
 *	Copyright © 2003-09, Michael J. Conrad. All Rights Reserved.
 */


#ifndef __LArrayIterator__
#define __LArrayIterator__

#include "afphelp.h"
#include "afpdebug.h"
#include "LArray.h"

class LArrayIterator
{
	LArrayIterator(
		const LArray& inArray,
		array_index inPosition = LArray::array_index_firstitem
		);

	virtual void	ResetTo(array_index inPosition);
	virtual bool	Current(void* data);
	virtual bool	Next(void* data);
	virtual void*	PtrToCurrent();
	virtual void*	PtrToNext();

	virtual void	CalcNextIndex();

private:
	LArray	mArray;
	uint32  mCurrentIndex;
};


#endif //__LArrayIterator__