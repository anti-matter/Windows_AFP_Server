#pragma once

#include <algorithm>
#include <mutex>

#include "afpdebug.h"

enum users_and_group_name_type : BYTE
{
    afp_user_name_type = 0,
    afp_group_name_type
};

typedef struct
{
    BYTE type;
    DWORD id;
    std::wstring name;
} afp_user_group_name;

class users_and_groups_cache
{
public:

    users_and_groups_cache() = default;

    void add(const users_and_group_name_type type, const std::wstring& name, const DWORD id)
    {
        if (find_by_id(id) != nullptr)
        {
            // Already cached.
            return;
        }

        auto user_or_group = std::make_unique<afp_user_group_name>();
        user_or_group->id = id;
        user_or_group->name = name;
        user_or_group->type = type;

        users_and_groups_.push_back(std::move(user_or_group));
    }

    afp_user_group_name* find_by_name(const std::wstring& name)
    {
        std::lock_guard lock(mutex_);

        const auto it = std::ranges::find_if(users_and_groups_, [=](const std::unique_ptr<afp_user_group_name>& ug)
            {
                return ug->name == name;
            });

        if (it != users_and_groups_.end())
        {
            DBGWRITE(dbg_level_info, L"Found user by name (%s)(%d)\n", it->get()->name.c_str(), it->get()->id);
            return it->get();
        }

        return nullptr;
    }

    afp_user_group_name* find_by_id(const DWORD id)
    {
        std::lock_guard lock(mutex_);

        const auto it = std::ranges::find_if(users_and_groups_, [=](const std::unique_ptr<afp_user_group_name>& ug)
            {
                return ug->id == id;
            });

        if (it != users_and_groups_.end())
        {
            DBGWRITE(dbg_level_info, L"Found user/group name by id (%s)(%d)(%d)\n", it->get()->name.c_str(), it->get()->id, it->get()->type);
            return it->get();
        }

        return nullptr;
    }

private:
    std::vector<std::unique_ptr<afp_user_group_name>> users_and_groups_;
    std::mutex mutex_;
};

