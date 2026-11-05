#pragma once
#include <filesystem>
#include <mutex>

#include "afp_desk_item.h"

constexpr UINT32 desktop_file_error = MAXUINT32;
constexpr UINT32 desktop_file_not_found = MAXUINT32 - 1;

const std::wstring desktop_file_name = L"afp_desktop.db";
constexpr BYTE desktop_file_version = 1;

#ifdef _DEBUG
#define DUMP_ITEM(i) dump_item(i, true)
#else
#define DUMP_ITEM(i)
#endif

class afp_volume;

// This class manages an open desktop. It points to a specific desktop database
// file and serves client requests to modifications to it.
class afp_desktop  // NOLINT(cppcoreguidelines-special-member-functions)
{
public:
    // A desktop file is associated with a specific volume. A volume
    // can have only one.
    explicit afp_desktop(afp_volume* volume);

    // Most important thing done here is to update the desktop.db on disk.
    ~afp_desktop();

    // Read in the desktop db and store in memory. Returns the
    // number of desktop items read, or desktop_file_error
    // if file did not exist.
    UINT32 load_from_file();

    // Writes out the desktop db from memory to a file. Returns
    // the number of items written, or error.
    UINT32 export_to_file();

    void add(const afp_desk_item* item);
    void add(std::unique_ptr<afp_desk_item>& item);

    // Removes an item from the desktop db.
    bool remove(const afp_desk_item* item);

    afp_desk_item* find(const afp_desk_item* match_item, const UINT16 search_index = 0);

    [[nodiscard]] INT16 get_volume_id() const { return volume_id_; }

private:
    std::recursive_mutex desk_items_mutex_;
    std::vector<std::unique_ptr<afp_desk_item>> desk_items_;
    std::filesystem::path desktop_file_path_;

    INT16 volume_id_;

#ifdef _DEBUG
    void dump_db();
    static void dump_item(afp_desk_item* item, bool include_data = true);
#endif
};