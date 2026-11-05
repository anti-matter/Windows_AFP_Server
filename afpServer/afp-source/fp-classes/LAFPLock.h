/*
 *	LAFPLock.h
 *	Created 12/27/2002
 *
 *	Copyright © 2003, Michael J. Conrad. All Rights Reserved.
 */

#ifndef __LAFPLock__
#define __LAFPLock__

#include "afp.h"
#include "dsi.h"


class LAFPLock
{
public:
						LAFPLock(OPEN_FORK_ITEM* forkItem);
	virtual				~LAFPLock();
	
	static bool			RangeLocked(
							int64 		rangeStart,
							int64 		rangeEnd,
							LPAFPINDEX	entry
							);
	
	static LAFPLock*	SessionRangeLocked(
							int64 				rangeStart,
							int64 				rangeLength,
							OPEN_FORK_DATA* 	forkItem
							);
	
	virtual AFPERROR	Lock(int64 offset, int64 len);
	virtual void		GetLockRange(int64* start, int64* end);
	
	virtual BEntry*		GetLockEntry()		{ return mEntry;  }
	
private:

	LPAFPINDEX		mEntry;
	int64			mStart;
	int64			mEnd;
	OPEN_FORK_DATA*	mForkRef;
};

#endif //__LAFPLock__