/*
*	VolumeArray.h
*
*	Copyright © Michael J. Conrad. All Rights Reserved.
*/

#pragma once

#include "AFPArray.h"
#include "AutoLock.h"
#include "volumes.h"
#include <algorithm>

class VolumeArray : public AFPArray<AFP_VOLUME_INFO>
{
public:

    VolumeArray()
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

    AFP_VOLUME_INFO* FindByName(PWCHAR name)
    {
        AutoLock lock(&mMutex);

        std::vector<std::unique_ptr<AFP_VOLUME_INFO>>::iterator it = std::find_if(mData.begin(), mData.end(), [=](const std::unique_ptr<AFP_VOLUME_INFO>& v)
        {
            DBGWRITE(dbg_level_info, L"[VolumeArray:FindByName]Considering volume %s (%d)\n", v->szShareName, v->wVolID);
            return lstrcmp(v->szShareName, name) == 0;
        });

        if (it != mData.end())
        {
            DBGWRITE(dbg_level_info, L"[VolumeArray:FindByName]Found volume by name (%s)(%d)\n", it->get()->szShareName, it->get()->wVolID);
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

    AFP_VOLUME_INFO* FindById(WORD volId)
    {
        AutoLock lock(&mMutex);

        std::vector<std::unique_ptr<AFP_VOLUME_INFO>>::iterator it = std::find_if(mData.begin(), mData.end(), [=](const std::unique_ptr<AFP_VOLUME_INFO>& v)
        {
            DBGWRITE(dbg_level_info, L"[VolumeArray:FindById]Considering volume %s (%d)\n", v->szShareName, v->wVolID);
            return v->wVolID == volId;
        });

        if (it != mData.end())
        {
            DBGWRITE(dbg_level_info, L"[VolumeArray:FindById]Found volume by name (%s)(%d)\n", it->get()->szShareName, it->get()->wVolID);
            return it->get();
        }

        return nullptr;
    }
};

