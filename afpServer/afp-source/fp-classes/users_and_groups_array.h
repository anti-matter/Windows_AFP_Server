/*
* users_and_groups_array.h
*/

#pragma once

#include <algorithm>
#include "afp_array.h"
#include "auto_lock.h"
#include "afphelp.h"

class users_and_groups_array final : public afp_array<afp_user_group_name>
{
public:

    users_and_groups_array() = default;

    /*
    *	FindByName()
    *
    *	Description
    *		Finds a user and groups structure by name.
    *
    *	Returns
    *		AFP_USER_GROUP_NAME*
    */

    afp_user_group_name* find_by_name(const PWCHAR name, const BYTE name_type)
    {
        auto_lock lock(&m_mutex_);

        const auto it = std::find_if(m_data_.begin(), m_data_.end(), [=](const std::unique_ptr<afp_user_group_name>& ug)
        {
	        DBGWRITE(dbg_level_info, L"Found user %s (%d)\n", ug->name, ug->id);
	        return lstrcmp(ug->name, name) == 0 && ug->type == name_type;
        });

        if (it != m_data_.end())
        {
            DBGWRITE(dbg_level_info, L"Found user by name (%s)(%d)\n", it->get()->name, it->get()->id);
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
    *		AFP_USER_GROUP_NAME*
    */

    afp_user_group_name* find_by_id(const DWORD id)
    {
        auto_lock lock(&m_mutex_);

        const auto it = std::find_if(m_data_.begin(), m_data_.end(), [=](const std::unique_ptr<afp_user_group_name>& ug)
        {
	        DBGWRITE(dbg_level_info, L"Found user %s (%d)\n", ug->name, ug->id);
	        return ug->id == id;
        });

        if (it != m_data_.end())
        {
            DBGWRITE(dbg_level_info, L"Found user name by id (%s)(%d)\n", it->get()->name, it->get()->id);
            return it->get();
        }

        return nullptr;
    }
};

