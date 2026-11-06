#pragma once
#include <filesystem>
#include <mutex>

#include "afp_desk_item.h"

constexpr UINT32 desktop_file_error = MAXUINT32;
constexpr UINT32 desktop_file_not_found = MAXUINT32 - 1;

const std::wstring desktop_file_name = L"afp_desktop.db";

// Desktop database file format (on-disk):
//   [desktop_db_header] 20 bytes - magic + version + per-type counts
//   [afp_desk_item]     N entries, contiguous
//
// The per-type counts let search-index range checks be done without
// scanning the database.
//
// Legacy databases (created before the header was added) start with a
// one-byte version followed by a native item count, then the entries.
// They are read as-is and migrated to the current format on the next
// write.
constexpr UINT32 desktop_db_magic = 0x61667064u; // "afpd"
constexpr UINT32 desktop_db_version = 2;

// Version stamped by the legacy (pre-header) format.
constexpr BYTE desktop_file_version = 1;

struct desktop_db_header
{
    UINT32 magic;
    UINT32 version;
    UINT32 icon_count;
    UINT32 appl_count;
    UINT32 cmnt_count;
};

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

    // Desktop items are persisted on every change; the destructor
    // just releases the in-memory items.
    ~afp_desktop();

    // Read in the desktop db and store in memory. Returns the
    // number of desktop items read, or desktop_file_error
    // if file did not exist.
    UINT32 load_from_file();

    // Writes out the desktop db from memory to a file. Returns
    // the number of items written, or error.
    UINT32 export_to_file();

    // Creates an empty desktop database file if one does not
    // already exist for this volume.
    void ensure_file_exists();

    // Adds an item to the desktop db. Per the AFP spec, if an equal
    // entry already exists it is replaced in place. Returns
    // afpIconTypeError if a matching icon is being replaced with an
    // icon of a different data size. Any modification is persisted to
    // the database file immediately so no data is lost if the server
    // stops unexpectedly.
    AFPERROR add(const afp_desk_item* item);
    AFPERROR add(std::unique_ptr<afp_desk_item>& item);

    // Removes an item from the desktop db. Returns true if at least
    // one item was removed.
    bool remove(const afp_desk_item* item);

    afp_desk_item* find(const afp_desk_item* match_item, const UINT16 search_index = 0);

    [[nodiscard]] INT16 get_volume_id() const { return volume_id_; }

private:
    std::recursive_mutex desk_items_mutex_;
    std::vector<std::unique_ptr<afp_desk_item>> desk_items_;
    std::filesystem::path desktop_file_path_;

    INT16 volume_id_;

    // Per-type entry counts, kept in sync with desk_items_ so search
    // index range checks don't have to scan the whole database.
    UINT32 icon_count_ = 0;
    UINT32 appl_count_ = 0;
    UINT32 cmnt_count_ = 0;

    void adjust_counts(const afp_desk_item* item, INT32 delta);

#ifdef _DEBUG
    void dump_db();
    static void dump_item(afp_desk_item* item, bool include_data = true);
#endif
};
