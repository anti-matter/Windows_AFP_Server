#include "stdafx.h"
#include "afpdebug.h"
#include "afp_desktop.h"
#include "afp_volume.h"


afp_desktop::afp_desktop(afp_volume* volume) : volume_id_(volume->get_volume_id())
{
    desktop_file_path_ = volume->get_share_path() / desktop_file_name;

    // If this fails, it's okay, it will be generated later.
    load_from_file();
}

afp_desktop::~afp_desktop() = default;

/*
 * adjust_counts()
 *
 * Description:
 *		Keeps the per-type entry counts in sync with the in-memory
 *		item list. These counts back the O(1) search-index range
 *		checks in find().
 */
void afp_desktop::adjust_counts(const afp_desk_item* item, const INT32 delta)
{
    switch (item->item_type)
    {
    case entry_type_icon:
        icon_count_ = static_cast<UINT32>(static_cast<INT32>(icon_count_) + delta);
        break;
    case entry_type_appl:
        appl_count_ = static_cast<UINT32>(static_cast<INT32>(appl_count_) + delta);
        break;
    case entry_type_comment:
        cmnt_count_ = static_cast<UINT32>(static_cast<INT32>(cmnt_count_) + delta);
        break;
    case entry_type_empty:
    case entry_type_reserved:
    default:
        break;
    }
}

UINT32 afp_desktop::load_from_file()
{
    std::lock_guard guard(desk_items_mutex_);

    if (!exists(desktop_file_path_))
    {
        return desktop_file_not_found;
    }

    FILE* file;
    const auto success = _wfopen_s(&file, desktop_file_path_.c_str(), L"rb");
    if (success != 0 || file == nullptr)
    {
        DBGWRITE(dbg_level_error, L"Failed to open desktop file! (%d)\n", errno);
        return desktop_file_error;
    }

    // Clear anything loaded previously so a reload can't duplicate entries.
    desk_items_.clear();
    icon_count_ = 0;
    appl_count_ = 0;
    cmnt_count_ = 0;

    size_t data_offset = 0;
    UINT32 header_count = 0;

    desktop_db_header header{};
    if (fread(&header, sizeof(header), 1, file) == 1 &&
        header.magic == desktop_db_magic &&
        header.version == desktop_db_version)
    {
        // Current format: header carries the per-type counts.
        data_offset = sizeof(header);
        header_count = header.icon_count + header.appl_count + header.cmnt_count;
        DBGWRITE(dbg_level_info, L"Desktop db version: %d\n", header.version);
    }
    else
    {
        //
        // Legacy database with no header; entries follow the old
        // one-byte version and native item count. They are migrated
        // to the current format on the next write.
        //
        fseek(file, 0, SEEK_SET);

        BYTE version = 0;
        size_t legacy_count = 0;
        if (fread(&version, sizeof(version), 1, file) != 1)
        {
            DBGWRITE(dbg_level_error, L"Failed to read desktop file version!\n");
            fclose(file); // NOLINT(cert-err33-c)
            return desktop_file_error;
        }

        if (version < desktop_file_version)
        {
            // An empty/stub file; it will be written out in the
            // current format on the next change.
            fclose(file); // NOLINT(cert-err33-c)
            return 0;
        }

        if (version != desktop_file_version ||
            fread(&legacy_count, sizeof(legacy_count), 1, file) != 1)
        {
            DBGWRITE(dbg_level_error, L"Unrecognized desktop file version! (%d)\n", version);
            fclose(file); // NOLINT(cert-err33-c)
            return desktop_file_error;
        }

        data_offset = 1 + sizeof(legacy_count);
        header_count = static_cast<UINT32>(legacy_count);
        DBGWRITE(dbg_level_info, L"Legacy desktop file version: %d\n", version);
    }

    // Derive the true entry count from the file size; this is robust
    // against a bogus count in the header.
    fseek(file, 0, SEEK_END);
    const long file_size = ftell(file);
    fseek(file, static_cast<long>(data_offset), SEEK_SET);

    UINT32 item_count = 0;
    if (file_size > static_cast<long>(data_offset))
    {
        item_count = static_cast<UINT32>((file_size - static_cast<long>(data_offset)) / sizeof(afp_desk_item));
    }

    if (item_count != header_count)
    {
        DBGWRITE(dbg_level_warning, L"Desktop db header count (%d) does not match file size count (%d)!\n",
                 header_count, item_count);
    }

    DBGWRITE(dbg_level_trace, L"Desktop file for %s contains %d items.\n", desktop_file_path_.c_str(), item_count);

    for (UINT32 i = 0; i < item_count; ++i)
    {
        afp_desk_item item;

        if (fread(&item, sizeof(item), 1, file) != 1)
        {
            DBGWRITE(dbg_level_warning, L"Failed reading desktop entry %d; file is truncated.\n", i);
            break;
        }

        auto ep = std::make_unique<afp_desk_item>();
        memcpy(ep.get(), &item, sizeof(afp_desk_item));

        adjust_counts(ep.get(), 1);
        desk_items_.push_back(std::move(ep));
    }

    fclose(file); // NOLINT(cert-err33-c)

    return static_cast<UINT32>(desk_items_.size());
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

    // Compute the per-type counts from the in-memory items so the
    // header always reflects what is actually written.
    desktop_db_header header{};
    header.magic = desktop_db_magic;
    header.version = desktop_db_version;
    header.icon_count = 0;
    header.appl_count = 0;
    header.cmnt_count = 0;

    for (const auto& it : desk_items_)
    {
        switch (it->item_type)
        {
        case entry_type_icon:
            header.icon_count++;
            break;
        case entry_type_appl:
            header.appl_count++;
            break;
        case entry_type_comment:
            header.cmnt_count++;
            break;
        case entry_type_empty:
        case entry_type_reserved:
        default:
            break;
        }
    }

    auto written = fwrite(&header, sizeof(header), 1, file);
    if (written != 1)
    {
        DBGWRITE(dbg_level_error, L"Failed to write header to desktop db.\n");
        fclose(file); // NOLINT(cert-err33-c)
        return desktop_file_error;
    }

    UINT32 return_written = 0;
    for (const auto& it : desk_items_)
    {
        const auto wrote = fwrite(it.get(), sizeof(afp_desk_item), 1, file);
        if (wrote == 0)
        {
            DBGWRITE(dbg_level_warning, L"Failed to write desktop entry! (%d)\n", errno);
            break;
        }

        return_written++;
    }

    DBGWRITE(dbg_level_trace, L"Desktop exported %d items to the desktop file.\n", return_written);

    fclose(file); // NOLINT(cert-err33-c)

    return return_written;
}

void afp_desktop::ensure_file_exists()
{
    std::lock_guard guard(desk_items_mutex_);

    if (!exists(desktop_file_path_))
    {
        // Writes the header-only database file.
        export_to_file();
    }
}

AFPERROR afp_desktop::add(const afp_desk_item* item)
{
    auto n_item = std::make_unique<afp_desk_item>();

    // See afp_desk_item.h for operator.
    *n_item = *item;

    DBGWRITE(dbg_level_trace, L"Adding desktop item:\n");
    DUMP_ITEM(n_item.get());

    return add(n_item);
}

AFPERROR afp_desktop::add(std::unique_ptr<afp_desk_item>& item)
{
    std::lock_guard guard(desk_items_mutex_);

    //
    // Per the AFP spec, adding an entry that matches one already in
    // the database replaces it. Replacing in place keeps the database
    // from growing without bound and stops stale entries from being
    // served to clients.
    //
    const auto existing = std::ranges::find_if(desk_items_, [&](const std::unique_ptr<afp_desk_item>& i)
    {
        switch (item->item_type)
        {
        case entry_type_icon:
            return item->equal_icon(i.get());
        case entry_type_appl:
            return item->equal_aapl(i.get());
        case entry_type_comment:
            return item->equal_comment(i.get());
        case entry_type_empty:
        case entry_type_reserved:
        default:
            return false;
        }
    });

    if (existing != desk_items_.end())
    {
        // As per the spec, an icon may only be replaced by an icon
        // with the same bitmap size.
        if (item->item_type == entry_type_icon && item->data_size != existing->get()->data_size)
        {
            DBGWRITE(dbg_level_warning, L"Icon replace size mismatch! (%d vs %d)\n",
                     item->data_size, existing->get()->data_size);
            return afpIconTypeError;
        }

        DBGWRITE(dbg_level_trace, L"Replacing desktop item of type %d in database!\n", item->item_type);
        *existing = std::move(item);
    }
    else
    {
        adjust_counts(item.get(), 1);
        desk_items_.push_back(std::move(item));
    }

    // Persist the change immediately so nothing is lost if the
    // server or machine goes down before the volume is unmounted.
    export_to_file();

    return AFP_OK;
}

bool afp_desktop::remove(const afp_desk_item* item)
{
    std::lock_guard guard(desk_items_mutex_);

    const auto new_end = std::ranges::remove_if(desk_items_, [=](const std::unique_ptr<afp_desk_item>& i)
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
    }).begin();

    const auto removed_count = static_cast<INT32>(desk_items_.end() - new_end);

    if (removed_count > 0)
    {
        desk_items_.erase(new_end, desk_items_.end());

        // The predicate only ever matches entries whose type equals
        // item's, so all the decrements land on that one type. (The
        // removed entries themselves can't be inspected after
        // remove_if: the tail it marks holds moved-from husks.)
        adjust_counts(item, -removed_count);

        // Persist the change immediately.
        export_to_file();

        return true;
    }

    return false;
}

afp_desk_item* afp_desktop::find(const afp_desk_item* match_item, const UINT16 search_index)
{
    std::lock_guard guard(desk_items_mutex_);
    UINT16 index = 0;

    // The per-type counts are maintained incrementally, so this
    // range check is O(1) instead of a scan of the whole database.
    if (match_item->item_type == entry_type_icon)
    {
        if (search_index > icon_count_)
        {
            DBGWRITE(dbg_level_warning, L"Icon search index out of range! (%d vs %d)\n", search_index, icon_count_);
            return nullptr;
        }
    }

    if (match_item->item_type == entry_type_appl)
    {
        if (search_index > appl_count_)
        {
            DBGWRITE(dbg_level_warning, L"APPL search index out of range! (%d vs %d)\n", search_index, appl_count_);
            return nullptr;
        }
    }

    const auto it = std::ranges::find_if(desk_items_, [&](const std::unique_ptr<afp_desk_item>& i)
    {
        switch (match_item->item_type)
        {
        case entry_type_empty:
            break;
        case entry_type_icon:
            {
                // data_size will be 0 if performing a search for FPGetIconInfo. Otherwise,
                // we're looking for FPGetIcon.
                if (match_item->data_size != 0)
                {
                    return match_item->equal_icon(i.get());
                }

                // When using index, only use file_creator to find matching icons.
                if (i->item_type == entry_type_icon && i->file_creator == match_item->file_creator)
                {
                    ++index;
                    return index == search_index;
                }

                return false;
            }
        case entry_type_appl:
            {
	            if (i->equal_aapl(match_item))
	            {
	                ++index;
	                if (index == search_index)
	                {
	                    DBGWRITE(dbg_level_trace, L"Found APPL for: %s\n", i->path_name);
	                    return true;
	                }
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
        DBG_DUMP_BUFFER(item->data, item->data_size, dbg_level_trace);
    }
}
#endif
