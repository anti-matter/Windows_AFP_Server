/*
 *	LAFPLock.cpp
 *	Created 12/27/2002
 *
 *	Copyright © 2003, Michael J. Conrad. All Rights Reserved.
 */

#include "afp.h"
#include "afphelp.h"
#include "LList.h"
#include "LAFPLock.h"

LList	gLockList;

/*
 * LAFPLock()
 *
 * Description:
 *
 * Returns:
 */

LAFPLock::LAFPLock(OPEN_FORK_ITEM* forkItem)
{
	mStart		= 0;
	mEnd		= 0;
	
	gLockList.AddItem(this);
	
	//
	//We add this locker object to the forkItem struct so
	//it can be referenced on a per-fork basis.
	//
	mForkRef 	= forkItem;
	mEntry		= forkItem->entry;
	
	forkItem->brlList->AddItem(this);
}


/*
 * ~LAFPLock()
 *
 * Description:
 *
 * Returns:
 */

LAFPLock::~LAFPLock()
{
	gLockList.RemoveItem(this);
	mForkRef->brlList->RemoveItem(this);
}


/*
 * RangeLocked() [STATIC]
 *
 * Description:
 *		Returns whether or not the range provided is already locked by 
 *		another session. There can be no overlapping locks and we return
 *		true even if only part of the requested range is locked by another
 *		user.
 *
 * Returns:
 *		TRUE if the range is locked, FALSE otherwise.
 */

bool LAFPLock::RangeLocked(
	int64 		rangeStart,
	int64 		rangeEnd,
	BEntry* 	entry
	)
{
	bool		result	= false;
	int			i		= 0;
	LAFPLock*	lock	= NULL;
	
	while((lock = (LAFPLock*)gLockList.ItemAt(i)) != NULL)
	{
		int64	start	= 0;
		int64	end		= 0;
		
		if (*lock->GetLockEntry() == *entry)
		{
			lock->GetLockRange(&start, &end);
						
			if (((rangeStart >= start) && (rangeStart <= end))	||
				((rangeEnd >= start) && (rangeEnd <= end))		||
				((rangeStart <= start) && (rangeEnd >= end))	)
			{
				//
				//We fall within the lock range, so we'll return TRUE.
				//
				result = true;
				break;
			}
		}
		
		i++;
	}
	
	return( result );
}


/*
 * SessionRangeLocked() [STATIC]
 *
 * Description:
 *		Returns whether or not the range provided is locked by a session.
 *		There can be no overlapping locks and we return true even if only
 *		part of the requested range is locked by another user.
 *
 * Returns:
 *		TRUE if the range is locked, FALSE otherwise.
 */

LAFPLock* LAFPLock::SessionRangeLocked(
	int64 				rangeStart,
	int64 				rangeLength,
	OPEN_FORK_ITEM* 	forkItem
	)
{
	LAFPLock*	result	= NULL;
	LAFPLock*	lock	= NULL;
	int			i		= 0;
	
	while((lock = (LAFPLock*)forkItem->brlList->ItemAt(i)) != NULL)
	{
		int64	start	= 0;
		int64	end		= 0;
		
		lock->GetLockRange(&start, &end);
		
		if ((rangeStart == start) && (rangeLength == ((end-start)+1)))
		{
			//
			//We fall within the lock range, so we'll return TRUE.
			//
			result = lock;
			break;
		}
		
		i++;
	}
	
	return( result );
}


/*
 * Lock()
 *
 * Description:
 *
 * Returns:
 */

AFPERROR LAFPLock::Lock(int64 offset, int64 len)
{
	int64		rangeStart	= offset;
	int64		rangeEnd	= (offset + (len-1));
	
	if (RangeLocked(rangeStart, rangeEnd, mForkRef->entry))
	{
		return( afpLockErr );
	}
	else
	{
		mStart	= rangeStart;
		mEnd	= rangeEnd;
	}
	
	return( AFP_OK );
}


/*
 * GetLockRange()
 *
 * Description:
 *
 * Returns:
 */

void LAFPLock::GetLockRange(int64* start, int64* end)
{
	*start	= mStart;
	*end	= mEnd;
}












