/*
 *	AFPCriticalSection.h
 *	Created 03/28/09
 *
 *	Copyright © 2009, Michael J. Conrad. All Rights Reserved.
 */

#ifndef __AFPCriticalSection__
#define __AFPCriticalSection__

#include "afpdebug.h"
#include <cassert>


class AFPCriticalSection
{
public:
    AFPCriticalSection(
        IN PCRITICAL_SECTION	cSection			= NULL,
        IN bool					fLockImmediately	= false
        )
    {
        mIsLocked = false;

        if (cSection != NULL)
        {
            mCriticalSection = cSection;
            mOwnsStructure = false;
        }
        else
        {
            mCriticalSection = (PCRITICAL_SECTION)malloc(sizeof(CRITICAL_SECTION));
            mOwnsStructure = true;

            InitializeCriticalSection( mCriticalSection );
        }

        if (fLockImmediately) {

            this->Lock();
        }
    }

    ~AFPCriticalSection()
    {
        if (mIsLocked) {

            Unlock();
        }

        if ((mOwnsStructure) && (mCriticalSection != NULL))
        {
            DeleteCriticalSection( mCriticalSection );
            delete [] mCriticalSection;
        }
    }

    virtual void Lock()
    {
        assert(mCriticalSection != NULL);

        if (mCriticalSection != NULL) {

            EnterCriticalSection( mCriticalSection );
            mIsLocked = true;
        }
    }

    virtual void Unlock()
    {
        assert(mCriticalSection != NULL);

        if ((mIsLocked) && (mCriticalSection != NULL)) {

            LeaveCriticalSection( mCriticalSection );
            mIsLocked = false;
        }
    }

private:
    PCRITICAL_SECTION	mCriticalSection;
    bool				mIsLocked;
    bool				mOwnsStructure;
};












#endif //__AFPCriticalSection
