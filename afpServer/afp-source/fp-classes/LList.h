/*
 *	LList.h
 *	Created 03/09/2003
 *
 *	Copyright © 2003, Michael J. Conrad. All Rights Reserved.
 */


#ifndef __LLIST__
#define __LLIST__

#include "afphelp.h"
#include "afpdebug.h"

typedef struct
{
	void*	next;
	char	data[1];
}LLIST_ELEMENT;

typedef bool find_func(LLIST_ELEMENT*);

class LList
{
public:
						LList(bool fOwnsItems=false);
	virtual				~LList();

	virtual bool		AddItem(void* data, uint32 dataSize=0, bool fCopyData=false);

	virtual void		RemoveItem(void* data);
	virtual void		RemoveItem(uint32 index);

	static void*		AllocateNewElement(uint32 dataSize);

	virtual void		Rewind()		{ mIterationCurrent = mHead; }
	virtual void*		Next();

	virtual void*		Find(void* findData, find_func ff);

	virtual uint32		CountItems()	{ return mItemCount; }
	virtual void		Lock()			{ EnterCriticalSection(&mMutex); }
	virtual void		Unlock()		{ LeaveCriticalSection(&mMutex); }

private:

	LLIST_ELEMENT*		mHead;
	uint32				mItemCount;
	bool				mOwnsItems;

	//
	//This element pointer is used for traversing the list, it points to
	//the current element in the iteration.
	//
	LLIST_ELEMENT*		mIterationCurrent;

	//
	//Need this critical section so the linked list we use
	//is thread safe.
	//
	CRITICAL_SECTION	mMutex;
};

#endif //__LLIST__