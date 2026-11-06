#pragma once

#include <algorithm>
#include <mutex>

#include "afpdebug.h"

static DWORD g_next_user_group_id = 1;

enum users_and_group_name_type : BYTE
{
    afp_user_name_type = 0,
    afp_group_name_type
};

struct afp_user_group_name
{
    afp_user_group_name() : type(afp_user_name_type), id(0) { }

    BYTE type;
    DWORD id;
    std::wstring name;
};

class users_and_groups_cache
{
public:

    users_and_groups_cache() = default;

    DWORD add(const users_and_group_name_type type, const std::wstring& name)
    {
        // The whole check-then-insert must be under the lock (recursive,
        // because find_by_name takes it too); otherwise concurrent adds
        // can duplicate ids or corrupt the vector.
        std::lock_guard lock(mutex_);

        if (const auto result = find_by_name(type, name); result != nullptr)
        {
            DBGWRITE(dbg_level_trace, L"User/group already in cache! (%s)\n", name.c_str());
            return result->id;
        }

        DBGWRITE(dbg_level_info, L"Adding user or group %s to the cache.\n", name.c_str());

        const auto new_id = g_next_user_group_id++;
        auto user_or_group = std::make_unique<afp_user_group_name>();
        user_or_group->id = new_id;
        user_or_group->name = name;
        user_or_group->type = type;

        users_and_groups_.push_back(std::move(user_or_group));

        return new_id;
    }

    afp_user_group_name* find_by_name(const users_and_group_name_type type, const std::wstring& name)
    {
        std::lock_guard lock(mutex_);

        const auto it = std::ranges::find_if(users_and_groups_, [=](const std::unique_ptr<afp_user_group_name>& ug)
            {
                return ug->type == type && ug->name == name;
            });

        if (it != users_and_groups_.end())
        {
            DBGWRITE(dbg_level_trace, L"Found user by name (%s)(%d)\n", it->get()->name.c_str(), it->get()->id);
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
            DBGWRITE(dbg_level_trace, L"Found user/group name by id (%s)(%d)(%d)\n", it->get()->name.c_str(), it->get()->id, it->get()->type);
            return it->get();
        }

        return nullptr;
    }

private:
    std::vector<std::unique_ptr<afp_user_group_name>> users_and_groups_;
    std::recursive_mutex mutex_;
};

