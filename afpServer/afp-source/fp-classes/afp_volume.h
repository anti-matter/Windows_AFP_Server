#pragma once
#include <mutex>
#include <filesystem>

#include "afp_file_index.h"
#include "desktop_database.h"

class change_notify;

constexpr UINT8 max_volume_levels_deep = 128;
constexpr UINT8 max_volumes = MAXBYTE;
constexpr FILETIME default_backup_time = { 627916800 , 34316075 };

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

    void make_desktop_database();

    [[nodiscard]] afp_index* find(const std::wstring& full_path) const { return index_->find(full_path); }
    [[nodiscard]] afp_index* find(const DWORD directory_id, const std::wstring& path) const { return index_->find(directory_id, path); }

    [[nodiscard]] afp_file_index* get_index() const { return index_.get(); }

    void set_create_date(const FILETIME date) { creation_date_ = date; }
    void set_modified_date(const FILETIME date) { modification_date_ = date; }
    void set_backup_date(const FILETIME date) { backup_date_ = date; }
    void set_internal_flags(const UINT32 flags) { internal_flags_ |= flags; }

    void set_is_dirty(const bool is_dirty = true) { is_dirty_ = is_dirty; }
    [[nodiscard]] bool is_dirty() const { return is_dirty_; }

    FILETIME* get_create_date() { return &creation_date_; }
    FILETIME* get_modified_date() { return &modification_date_; }
    FILETIME* get_backup_date() { return &backup_date_; }

    [[nodiscard]] UINT16 get_volume_id() const { return id_; }

    std::filesystem::path& get_share_path() { return share_path_; }
    std::wstring& get_share_name() { return share_name_; }
    [[nodiscard]]UINT32 get_internal_flags() const { return internal_flags_; }
    [[nodiscard]] BYTE get_afp_flags() const { return afp_flags_; }

    [[nodiscard]] HANDLE get_notify_handle() const { return notify_event_; }
    void set_notify_handle(const HANDLE handle) { notify_event_ = handle; }

    [[nodiscard]] desktop_database* get_desktop() const { return desktop_.get(); }

    void notify_ignore_path(const std::filesystem::path& path) const;

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
    std::unique_ptr<change_notify> notify_;
    HANDLE notify_event_;

    // Desktop array/index associated with this volume.
    std::unique_ptr<desktop_database> desktop_;
    std::mutex volumes_mutex_;
};

