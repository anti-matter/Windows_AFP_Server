#pragma once

#include <algorithm>
#include "afp_array.h"
#include "auto_lock.h"
#include "volumes.h"

class volume_array final : public afp_array<AFP_VOLUME_INFO>
{
public:

    volume_array() = default;

    AFP_VOLUME_INFO* find_by_name(const PWCHAR name)
    {
        auto_lock lock(&m_mutex_);

        const auto it = std::find_if(m_data_.begin(), m_data_.end(), [=](const std::unique_ptr<AFP_VOLUME_INFO>& v)
        {
	        DBGWRITE(dbg_level_info, L"[VolumeArray:FindByName]Considering volume %s (%d)\n", v->szShareName, v->wVolID);
	        return lstrcmp(v->szShareName, name) == 0;
        });

        if (it != m_data_.end())
        {
            DBGWRITE(dbg_level_info, L"[VolumeArray:FindByName]Found volume by name (%s)(%d)\n", it->get()->szShareName, it->get()->wVolID);
            return it->get();
        }

        return nullptr;
    }


    AFP_VOLUME_INFO* find_by_id(const WORD vol_id)
    {
        auto_lock lock(&m_mutex_);

        const auto it = std::find_if(m_data_.begin(), m_data_.end(), [=](const std::unique_ptr<AFP_VOLUME_INFO>& v)
        {
	        DBGWRITE(dbg_level_info, L"[VolumeArray:FindById]Considering volume %s (%d)\n", v->szShareName, v->wVolID);
	        return v->wVolID == vol_id;
        });

        if (it != m_data_.end())
        {
            DBGWRITE(dbg_level_info, L"[VolumeArray:FindById]Found volume by name (%s)(%d)\n", it->get()->szShareName, it->get()->wVolID);
            return it->get();
        }

        return nullptr;
    }

    std::vector<WORD> find_dirty_volumes()
    {
        auto_lock lock(&m_mutex_);
        std::vector<WORD> result;

        for (const auto& volume : m_data_)
        {
            if (volume->fDirty)
            {
                DBGWRITE(dbg_level_info, L"Found dirty volume (&s)\n", volume->szShareName);
                result.push_back(volume->wVolID);
            }
        }

        return result;
    }
};

