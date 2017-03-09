/*
*	AutoLock
*	Created 11/29/15
*
*	Copyright © Michael J. Conrad. All Rights Reserved.
*/
#pragma once

class AutoLock
{
public:
    AutoLock(CRITICAL_SECTION* toLock);
    ~AutoLock();

    void Lock() const;
    void Unlock() const;

private:
    CRITICAL_SECTION* _cs;
};

