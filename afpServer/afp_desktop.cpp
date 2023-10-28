#include "stdafx.h"
#include "afpdebug.h"
#include "afp_desktop.h"
#include "afp_volume.h"
#include "afp_volumes.h"


afp_desktop::afp_desktop(const UINT16 volume_id)
{
    const auto volume = afp_volumes::current()->find(volume_id);
    desktop_file_path_ = volume->get_share_path() / desktop_file_name;

    // If this fails, it's okay, it will be generated later.
    load_from_file();
}

UINT32 afp_desktop::load_from_file()
{
    std::lock_guard guard(desk_items_mutex_);

    if (exists(desktop_file_path_))
    {
        return desktop_file_not_found;
    }

    FILE* file;
    const auto success = _wfopen_s(&file, desktop_file_path_.c_str(), L"r+b");
    if (success != 0 || file == nullptr)
    {
        ASSERT(0);
        return desktop_file_error;
    }

    BYTE version = 0;
    if (fread(&version, sizeof(version), 1, file) == 0)
    {
        DBGWRITE(dbg_level_error, L"Failed to read desktop file version!\n", errno);
        return desktop_file_error;
    }

    std::vector<std::unique_ptr<afp_desk_item>>::size_type item_count;
    if (fread(&item_count, sizeof(item_count), 1, file) == 0)
    {
        DBGWRITE(dbg_level_error, L"Failed to read desktop item count.\n", errno);
        return desktop_file_error;
    }

    DBGWRITE(dbg_level_all, L"Desktop file contains %d items.\n", item_count);

    for (std::vector<std::unique_ptr<afp_desk_item>>::size_type i = 0; i < item_count; ++i)
    {
        afp_desk_item item;

        if (fread(&item, sizeof(item), 1, file) != 0)
        {
            auto ep = std::make_unique<afp_desk_item>();
            memcpy(ep.get(), &item, sizeof(afp_desk_item));

            add(std::move(ep));
        }
    }

    fclose(file); // NOLINT(cert-err33-c)

    return item_count;
}

UINT32 afp_desktop::export_to_file()
{
    std::lock_guard guard(desk_items_mutex_);

    FILE* file;

    const auto success = _wfopen_s(&file, desktop_file_path_.c_str(), L"wb");
    if (success != 0 || file == nullptr)
    {
        return desktop_file_error;
    }

    auto written = fwrite(&desktop_file_version, sizeof(desktop_file_version), 1, file);
    if (written == 0)
    {
        DBGWRITE(dbg_level_error, L"Failed to write version to desktop db.\n", errno);
        return desktop_file_error;
    }

    UINT32 return_written = 0;
    const auto size = desk_items_.size();
    written = fwrite(&size, sizeof(size), 1, file);
    if (written == sizeof(size))
    {
        for (auto it = desk_items_.begin(); it < desk_items_.end(); ++it)
        {
            const auto wrote = fwrite(it->get(), sizeof(afp_desk_item), 1, file);
            if (wrote != sizeof(afp_desk_item))
            {
                DBGWRITE(dbg_level_warning, L"Failed to write desktop entry! (%d)\n", errno);
                break;
            }

            return_written++;
        }
    }

    fclose(file); // NOLINT(cert-err33-c)

    return return_written;
}

void afp_desktop::add(std::unique_ptr<afp_desk_item> item)
{
    std::lock_guard guard(desk_items_mutex_);
    desk_items_.push_back(std::move(item));
}

bool afp_desktop::remove(const afp_desk_item* item)
{
    std::lock_guard guard(desk_items_mutex_);

    const auto it = std::ranges::remove_if(desk_items_, [=](const std::unique_ptr<afp_desk_item>& i)
    {
        return item == i.get();
    });

    desk_items_.erase(it.begin(), it.end());

    return AFP_OK;
}

afp_desk_item* afp_desktop::find(const afp_desk_item* match_item, const UINT16 search_index)
{
    std::lock_guard guard(desk_items_mutex_);

    const auto it = std::ranges::find_if(desk_items_, [=,this](const std::unique_ptr<afp_desk_item>& i)
    {
        switch (match_item->item_type)
        {
        case entry_type_empty:
            break;
        case entry_type_icon:
            {
                // data_size will be 0 if performing a search for icon info.
                if (match_item->data_size != 0)
                {
                    if (std::tie(match_item->file_creator, match_item->file_type, match_item->icon_type,
                                 match_item->data_size) ==
                        std::tie(i->file_creator, i->file_type, i->icon_type, i->data_size))
                    {
                        DBGWRITE(dbg_level_all, L"Found icon entry:\n");
                        DUMP_ITEM(i.get());
                        return true;
                    }

                    return false;
                }

                // Make sure the provided search index is in range.
                const auto num_icons = std::ranges::count_if(desk_items_, [=](const std::unique_ptr<afp_desk_item>& i2)
                {
                    return i2->item_type == entry_type_icon && i2->file_creator == match_item->file_creator;
                });

                if (search_index > num_icons)
                {
                    DBGWRITE(dbg_level_all, L"Icon search index out of range! (%d vs %d)", search_index, num_icons);
                    return false;
                }

                UINT16 index = 0;
                const auto itr = std::ranges::find_if(desk_items_, [&](const std::unique_ptr<afp_desk_item>& i3)
                {
                    ++index;
                    const auto result =
                        i3->item_type == entry_type_icon &&
                        i3->file_creator == match_item->file_creator &&
                        index == search_index;

                    return result;
                });

                if (itr != desk_items_.end())
                {
                    DBGWRITE(dbg_level_all, L"Found icon for IconInfo:\n");
                    DUMP_ITEM(itr->get());

                    return itr->get();
                }
            }
            break;
        case entry_type_appl:
            {
                const auto num_appl = std::ranges::count_if(desk_items_, [=](const std::unique_ptr<afp_desk_item>& i2)
                {
                    return i2->item_type == entry_type_appl && i2->file_creator == match_item->file_creator;
                });

                if (search_index > num_appl)
                {
                    DBGWRITE(dbg_level_all, L"APPL search index out of range! (%d vs %d)", search_index, num_appl);
                    return false;
                }

                UINT16 index = 0;
                const auto itr = std::ranges::find_if(desk_items_, [&](const std::unique_ptr<afp_desk_item>& i3)
                {
                    ++index;
                    const auto result =
                        i3->item_type == entry_type_appl &&
                        i3->file_creator == match_item->file_creator &&
                        index == search_index;

                    return result;
                });

                if (itr != desk_items_.end())
                {
                    DBGWRITE(dbg_level_all, L"Found APPL:\n");
                    DUMP_ITEM(itr->get());

                    return itr->get();
                }
            }
            break;
        case entry_type_comment:
            if (match_item->dir_id == i->dir_id && lstrcmp(match_item->path_name, i->path_name))
            {
                DBGWRITE(dbg_level_all, L"Found comment:\n");
                DUMP_ITEM(i.get());
            }
            break;
        case entry_type_reserved:
            // ignore
            break;
        }

        return false;
    });

    return nullptr;
}

#ifdef _DEBUG
void afp_desktop::dump_db()
{
    std::lock_guard guard(desk_items_mutex_);

    for (auto it = desk_items_.begin(); it < desk_items_.end(); ++it)
    {
        dump_item(it->get());
    }
}

void afp_desktop::dump_item(afp_desk_item* item, bool include_data)
{
    DBGWRITE(dbg_level_info, L"item_type:    %d\n", item->item_type);
    DBGWRITE(dbg_level_info, L"file_type:    %d\n", item->file_type);
    DBGWRITE(dbg_level_info, L"file_creator: %d\n", item->file_creator);
    DBGWRITE(dbg_level_info, L"tag:          %d\n", item->tag);
    DBGWRITE(dbg_level_info, L"dir_id:       %d\n", item->dir_id);
    DBGWRITE(dbg_level_info, L"icon_type:    %d\n", item->icon_type);
    DBGWRITE(dbg_level_info, L"data_size:    %d\n", item->data_size);
    DBGWRITE(dbg_level_info, L"path_name:    %s\n", item->path_name);

    if (include_data)
    {
        DBG_DUMP_BUFFER(item->data, item->data_size);
    }
}
#endif
