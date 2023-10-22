#pragma once
#include <mutex>

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
    afp_volume();
    ~afp_volume();

private:
    UINT16 id_;
    BYTE afp_flags_;
    UINT32 internal_flags_;

    FILETIME creation_date_;
    FILETIME modification_date_;
    FILETIME backup_date_;

    std::wstring share_name_;
    std::wstring path_;

    bool is_dirty_;

    std::unique_ptr<afp_file_index> index_;
    std::unique_ptr<desktop_database> desktop_;

    HANDLE notifyEvent_;

    std::mutex send_mutex_;
};

