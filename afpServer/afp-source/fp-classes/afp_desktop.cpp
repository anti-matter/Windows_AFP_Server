#include "stdafx.h"
#include "afpdebug.h"
#include "afp_desktop.h"
#include "afp_volume.h"
#include "afp_volumes.h"


afp_desktop::afp_desktop(afp_volume* volume) : volume_id_(volume->get_volume_id())
{
    desktop_file_path_ = volume->get_share_path() / desktop_file_name;

    // If this fails, it's okay, it will be generated later.
    load_from_file();
}
 
afp_desktop::~afp_desktop() = default;

UINT32 afp_desktop::load_from_file()
{
    std::lock_guard guard(desk_items_mutex_);

    if (!std::filesystem::exists(desktop_file_path_))
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

    DBGWRITE(dbg_level_info, L"Desktop file version: %d\n", version);

    if (version < desktop_file_version)
    {
        return 0;
    }

    std::vector<std::unique_ptr<afp_desk_item>>::size_type item_count;
    if (fread(&item_count, sizeof(item_count), 1, file) == 0)
    {
        DBGWRITE(dbg_level_error, L"Failed to read desktop item count.\n", errno);
        return desktop_file_error;
    }

    DBGWRITE(dbg_level_trace, L"Desktop file contains %d items.\n", item_count);

    for (std::vector<std::unique_ptr<afp_desk_item>>::size_type i = 0; i < item_count; ++i)
    {
        afp_desk_item item;

        if (fread(&item, sizeof(item), 1, file) != 0)
        {
            auto ep = std::make_unique<afp_desk_item>();
            memcpy(ep.get(), &item, sizeof(afp_desk_item));

            add(ep);
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
    if (written == 1)
    {
        for (auto it = desk_items_.begin(); it < desk_items_.end(); ++it)
        {
            const auto wrote = fwrite(it->get(), sizeof(afp_desk_item), 1, file);
            if (wrote == 0)
            {
                DBGWRITE(dbg_level_warning, L"Failed to write desktop entry! (%d)\n", errno);
                break;
            }

            return_written++;
        }
    }

    DBGWRITE(dbg_level_trace, L"Desktop exported %d items to the desktop file.\n", return_written);

    fclose(file); // NOLINT(cert-err33-c)

    return return_written;
}

void afp_desktop::add(const afp_desk_item* item)
{
    auto n_item = std::make_unique<afp_desk_item>();

    // See afp_desk_item.h for operator.
    *n_item = item;

    DBGWRITE(dbg_level_trace, L"Adding desktop item:\n");
    DUMP_ITEM(n_item.get());

    add(n_item);
}

void afp_desktop::add(std::unique_ptr<afp_desk_item>& item)
{
    std::lock_guard guard(desk_items_mutex_);
    desk_items_.push_back(std::move(item));
}

bool afp_desktop::remove(const afp_desk_item* item)
{
    std::lock_guard guard(desk_items_mutex_);

    const auto it = std::ranges::remove_if(desk_items_, [=](const std::unique_ptr<afp_desk_item>& i)
    {
        switch(item->item_type)
        {
        case entry_type_comment:
            return item->equal_comment(i.get());
        case entry_type_empty:
            break;
        case entry_type_icon:
            return item->equal_icon(i.get());
        case entry_type_appl:
            return item->equal_aapl(i.get());
        case entry_type_reserved:
            break;
        }

        return false;
    });

    desk_items_.erase(it.begin(), it.end());

    return AFP_OK;
}

afp_desk_item* afp_desktop::find(const afp_desk_item* match_item, const UINT16 search_index)
{
    std::lock_guard guard(desk_items_mutex_);
    UINT16 index = 0;

    if (match_item->item_type == entry_type_icon)
    {
        const auto num_icons = std::ranges::count_if(desk_items_, [=](const std::unique_ptr<afp_desk_item>& i2)
            {
                // When using index, only use file_creator to find matching icons.
                return i2->item_type == entry_type_icon && i2->file_creator == match_item->file_creator;
            });

        if (search_index > num_icons)
        {
            DBGWRITE(dbg_level_warning, L"Icon search index out of range! (%d vs %d)\n", search_index, num_icons);
            return nullptr;
        }
    }

    if (match_item->item_type == entry_type_appl)
    {
        const auto num_appl = std::ranges::count_if(desk_items_, [=](const std::unique_ptr<afp_desk_item>& i2)
            {
                return i2->equal_aapl(match_item);
            });

        if (search_index > num_appl)
        {
            DBGWRITE(dbg_level_warning, L"APPL search index out of range! (%d vs %d)\n", search_index, num_appl);
            return nullptr;
        }
    }

    const auto it = std::ranges::find_if(desk_items_, [&](const std::unique_ptr<afp_desk_item>& i)
    {
        switch (match_item->item_type)
        {
        case entry_type_empty:
            ASSERT(0);
            break;
        case entry_type_icon:
            {
                // data_size will be 0 if performing a search for FPGetIconInfo. Otherwise,
                // we're looking for FPGetIcon.
                if (match_item->data_size != 0)
                {
                    return match_item->equal_icon(i.get());
                }

                ++index;

                return i->file_creator == match_item->file_creator && index == search_index;
            }
        case entry_type_appl:
            {
                ++index;

                if (i->equal_aapl(match_item) && index == search_index)
                {
                    DBGWRITE(dbg_level_trace, L"Found APPL for: %s\n", i->path_name);
                    return true;
                }

                return false;
            }
        case entry_type_comment:
            if (i->equal_comment(match_item))
            {
                DBGWRITE(dbg_level_trace, L"Found comment:\n");
                return true;
            }
            break;
        case entry_type_reserved:
            // ignore
            break;
        }

        return false;
    });

    if (it != desk_items_.end())
    {
        DUMP_ITEM(it->get());
        return it->get();
    }

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
    DBGWRITE(dbg_level_trace, L"item_type:    %d\n", item->item_type);
    DBGWRITE(dbg_level_trace, L"file_type:    %s\n", item->file_type_as_string().c_str());
    DBGWRITE(dbg_level_trace, L"file_creator: %s\n", item->creator_as_string().c_str());
    DBGWRITE(dbg_level_trace, L"tag:          %d\n", item->tag);
    DBGWRITE(dbg_level_trace, L"dir_id:       %d\n", item->dir_id);
    DBGWRITE(dbg_level_trace, L"icon_type:    %d\n", item->icon_type);
    DBGWRITE(dbg_level_trace, L"data_size:    %d\n", item->data_size);
    DBGWRITE(dbg_level_trace, L"path_name:    %s\n", item->path_name);

    if (include_data)
    {
        DBG_DUMP_BUFFER(item->data, item->data_size);
    }
}
#endif
