#include "stdafx.h"
#include "auto_lock.h"


auto_lock::auto_lock(CRITICAL_SECTION* to_lock)
    : cs_(to_lock)
{
    this->lock();
}


auto_lock::~auto_lock()
{
    this->unlock();
}


void auto_lock::lock() const
{
    EnterCriticalSection(cs_);
}


void auto_lock::unlock() const
{
    LeaveCriticalSection(cs_);
}

