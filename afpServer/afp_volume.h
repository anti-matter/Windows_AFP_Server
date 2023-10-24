#pragma once
#include <mutex>
#include <filesystem>

#include "afp_file_index.h"
#include "desktop_database.h"

constexpr UINT8 max_volume_levels_deep = 128;
constexpr UINT8 max_volumes = MAXBYTE;

enum volume_flags
{
    flag_volume_marked_for_deletion = 0x01,
    flag_volume_indexing_complete = 0x02,
    flag_volume_readonly = 0x04,
    flag_volume_has_password = 0x08,
    flag_volume_shared = 0x10
};

class afp_volume
{
public:
    afp_volume(std::wstring share_name, std::filesystem::path share_path, UINT16 volume_id, BYTE afp_flags, UINT32 internal_flags);
    ~afp_volume();

    afp_index* find(const std::wstring& full_path) const { return index_->find(full_path); }
    afp_index* find(const DWORD directory_id, const std::wstring& path) const { return index_->find(directory_id, path); }

    [[nodiscard]] afp_file_index* get_index() const { return index_.get(); }

    void set_create_date(const FILETIME date) { creation_date_ = date; }
    void set_modified_date(const FILETIME date) { modification_date_ = date; }
    void set_internal_flag(const UINT32 flags) { internal_flags_ |= flags; }
    void set_is_dirty() { is_dirty_ = true; }

    UINT16 get_volume_id() const { return id_; }

    std::filesystem::path& get_share_path() { return share_path_; }
    std::wstring& get_share_name() { return share_name_; }
    UINT32 get_internal_flags() const { return internal_flags_; }

    HANDLE get_notify_handle() const { return notify_event_; }
    void set_notify_handle(const HANDLE handle) { notify_event_ = handle; }

private:
    UINT16 id_;

    std::wstring share_name_;
    std::filesystem::path share_path_;

    BYTE afp_flags_;
    UINT32 internal_flags_;

    FILETIME creation_date_;
    FILETIME modification_date_;
    FILETIME backup_date_;

    bool is_dirty_;

    // Each volume has its own directory and file index.
    std::unique_ptr<afp_file_index> index_;

    // Desktop array/index associated with this volume.
    std::unique_ptr<desktop_database> desktop_;

    HANDLE notify_event_;

    std::mutex volumes_mutex_;
};

