/*
 *	LList.cpp
 *	Created 03/09/2003
 *
 *	Copyright © 2003, Michael J. Conrad. All Rights Reserved.
 */

#include "StdAfx.h"
#include "LList.h"


/*
 * LList()
 *
 * Description:
 *		Initializer for the LList object. If fOwnsItems is true, then this
 *		class will free the data pointer in each element in addition to the
 *		element itself when the object is deleted.
 *
 * Returns: None
 */

LList::LList(bool fOwnsItems)
{
    mHead				= NULL;
    mItemCount			= 0;
    mOwnsItems			= fOwnsItems;
    mIterationCurrent	= NULL;

    InitializeCriticalSection(&mMutex);
}


/*
 * ~LList()
 *
 * Description:
 *		Destructor for the LList object.
 *
 * Returns: None
 */

LList::~LList()
{
    EnterCriticalSection(&mMutex);

    //
    //If this class "owns" the links in the list, we need to dispose
    //of the memory they were using.
    //
    if (mOwnsItems)
    {
        LLIST_ELEMENT*	pCurrent	= mHead;
        LLIST_ELEMENT*	pNext		= NULL;

        while(pCurrent)
        {
            //
            //Save off the next element in the list so we can pick up
            //where we left off.
            //
            pNext = (LLIST_ELEMENT*)pCurrent->next;

            //
            //Dispose of the memory we allocated and that makes up this
            //link in the list.
            //
            LocalFree(pCurrent);
            pCurrent = pNext;
        }
    }

    LeaveCriticalSection(&mMutex);

    //
    //We don't need the critical section anymore, release the
    //resource it was using.
    //
    DeleteCriticalSection(&mMutex);
}


/*
 * AddItem()
 *
 * Description:
 *		Adds a new element to the link list. The caller only need provide
 *		a pointer to the data. If fCopyData==true, then the data is copied
 *		into the new element and it is assumed that the caller did not use
 *		AllocateNewElement() to allocate the memory for the buffer.
 *
 *		NOTE: dataSize is only used if fCopyData is true.
 *
 * Returns: None
 */

bool LList::AddItem(void* data, uint32 dataSize, bool fCopyData)
{
    LLIST_ELEMENT*	element = NULL;

    if (data == NULL)	return( false );

    //
    //The caller can optionally ask us to copy the data provided into
    //a new element structure.
    //
    if (fCopyData)
    {
        if (dataSize == 0)
        {
            //
            //We cannot have a zero sized data ptr!
            //
            return( false );
        }

        //
        //Use our home-grown allocation routine that allocated memory for
        //the data as well as the new element data structure.
        //
        element = (LLIST_ELEMENT*)AllocateNewElement(dataSize);

        if (element == NULL)
        {
            //
            //We failed to allocate the buffer for the new element.
            //
            return( false );
        }

        element -= sizeof(char*);

        //
        //Last step; copy the new data into the new element's buffer.
        //
        memcpy(element->data, data, dataSize);
    }
    else
    {
        element = ((LLIST_ELEMENT*)data) - sizeof(char*);
    }

    //
    //Now, we add the new element to the end of the linked list
    //
    EnterCriticalSection(&mMutex);

    LLIST_ELEMENT*	pCurrent = mHead;

    if (pCurrent == NULL) {

        mHead				= element;
        mIterationCurrent	= mHead;
    }
    else
    {
        while(pCurrent->next)
            pCurrent = (LLIST_ELEMENT*)pCurrent->next;
        
        pCurrent->next = element;
    }

    LeaveCriticalSection(&mMutex);

    return( true );
}


/*
 * RemoveItem()
 *
 * Description:
 *		Removes an element from the linked list. If the data is owned by
 *		this class, it is destroyed as well.
 *
 * Returns: None
 */

void LList::RemoveItem(void* data)
{
    LLIST_ELEMENT*	pCurrent	= NULL;
    LLIST_ELEMENT*	pPrev		= NULL;

    EnterCriticalSection(&mMutex);

    pCurrent = mHead;

    if (pCurrent == NULL)
    {
        //
        //If the list is 0 length, then there's nothing to remove.
        //
        LeaveCriticalSection(&mMutex);
        return;
    }

    while((&pCurrent->data != data) && (pCurrent))
    {
        pPrev	 = pCurrent;
        pCurrent = (LLIST_ELEMENT*)pCurrent->next;
    }

    //
    //Make sure we found the pointer by checking to see if we
    //have a valid pointer in pCurrent.
    //
    if (pCurrent)
    {
        if (pPrev != NULL) {
            
            pPrev->next = pCurrent->next;
        }

        //
        //If pPrev is NULL then the link at the head of the list
        //is being removed.
        //
        mHead = (LLIST_ELEMENT*)pCurrent->next;

        //
        //If we "own" the memory in the link we need to dispose of it.
        //
        if (mOwnsItems) {

            LocalFree(pCurrent);
        }
    }

    LeaveCriticalSection(&mMutex);
}


/*
 * RemoveItem()
 *
 * Description:
 *		Removes an element from the linked list. If the data is owned by
 *		this class, it is destroyed as well.
 *
 * Returns: None
 */

void LList::RemoveItem(uint32 index)
{
    UNREFERENCED_PARAMETER(index);
    ASSERT(0);
}


/*
 * AllocateNewElement()
 *
 * Description:
 *		Allocate memory for a new buffer and this element structure as well. Use
 *		this routine for allocating memory if you're going to store the buffer
 *		in this linked list.
 *
 * Returns: None
 */

void* LList::AllocateNewElement(uint32 dataSize)
{
    LLIST_ELEMENT*	newElement	= NULL;
    uint32			s_size		= dataSize + sizeof(LLIST_ELEMENT);

    newElement = (LLIST_ELEMENT*)LocalAlloc(LMEM_ZEROINIT, s_size);

    return( &newElement->data );
}


/*
 * Next()
 *
 * Description:
 *		Returns the next link in the list during iteration. Use Rewind to reset
 *		iteration to the beginning.
 *
 *		NOTE: Caller should Lock() the list before iterating throught the list!
 *
 * Returns: void*
 */

void* LList::Next()
{
    LLIST_ELEMENT*	pPtr = mIterationCurrent;

    //
    //Advance the saved iteration pointer to the next link in the list.
    //
    if (mIterationCurrent != NULL) {

        mIterationCurrent = (LLIST_ELEMENT*)mIterationCurrent->next;
    }

    return( &pPtr->data );
}


/*
 * Find()
 *
 * Description:
 *		Find function
 *
 * Returns: void* or NULL if find failed.
 */

void* LList::Find(void* findData, find_func ff)
{
    LLIST_ELEMENT*	pCurrent = NULL;

    if (findData == NULL) {

        //
        //Cannot have null search criteria.
        //
        return( NULL );
    }

    Lock();
    Rewind();

    while((pCurrent = (LLIST_ELEMENT*)Next()) != NULL)
    {
        if (ff(pCurrent))
        {
            //
            //The find was successful, exit and return the pointer.
            //
            break;
        }
    }

    Unlock();

    return( pCurrent );
}









