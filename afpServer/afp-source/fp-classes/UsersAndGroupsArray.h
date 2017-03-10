/*
*	UsersAndGroupsArray.h
*
*	Copyright © Michael J. Conrad. All Rights Reserved.
*/

#pragma once

#include "LArray.h"
#include "AutoLock.h"
#include <algorithm>

class UsersAndGroupsArray : public LArray<AFP_USERGROUP_NAME>
{
public:

    UsersAndGroupsArray()
    {
    }

    /*
    *	FindByName()
    *
    *	Description
    *		Finds a user and groups structure by name.
    *
    *	Returns
    *		AFP_USERGROUP_NAME*
    */

    AFP_USERGROUP_NAME* FindByName(PWCHAR name, BYTE nameType)
    {
        AutoLock lock(&mMutex);

        std::vector<std::unique_ptr<AFP_USERGROUP_NAME>>::iterator it = std::find_if(mData.begin(), mData.end(), [=](const std::unique_ptr<AFP_USERGROUP_NAME>& ug)
        {
            DBGWRITE(DbgLevelInfo, L"[UsersAndGroupsArray:FindByName]Found user %s (%d)\n", ug->name, ug->id);
            return lstrcmp(ug->name, name) == 0 && ug->type == nameType;
        });

        if (it != mData.end())
        {
            DBGWRITE(DbgLevelInfo, L"[UsersAndGroupsArray:FindByName]Found user by name (%s)(%d)\n", it->get()->name, it->get()->id);
            return it->get();
        }

        return nullptr;
    }

    /*
    *	FindById()
    *
    *	Description
    *		Finds a user and groups structure by id.
    *
    *	Returns
    *		AFP_USERGROUP_NAME*
    */

    AFP_USERGROUP_NAME* FindById(DWORD id)
    {
        AutoLock lock(&mMutex);

        std::vector<std::unique_ptr<AFP_USERGROUP_NAME>>::iterator it = std::find_if(mData.begin(), mData.end(), [=](const std::unique_ptr<AFP_USERGROUP_NAME>& ug)
        {
            DBGWRITE(DbgLevelInfo, L"[UsersAndGroupsArray:FindById]Found user %s (%d)\n", ug->name, ug->id);
            return ug->id == id;
        });

        if (it != mData.end())
        {
            DBGWRITE(DbgLevelInfo, L"[UsersAndGroupsArray:FindById]Found user name by id (%s)(%d)\n", it->get()->name, it->get()->id);
            return it->get();
        }

        return nullptr;
    }
};

