#include "stdafx.h"
#include "afp_volume.h"
#include "change_notify.h"

afp_volume::afp_volume(std::wstring share_name, std::filesystem::path share_path, const UINT16 volume_id, const BYTE afp_flags, const UINT32 internal_flags)
    : id_(volume_id)
    , share_name_(std::move(share_name))
    , share_path_(std::move(share_path))
    , afp_flags_(afp_flags)
    , internal_flags_(internal_flags)
    , creation_date_()
    , modification_date_()
    , backup_date_()
    , is_dirty_(false)
    , index_(nullptr)
    , notify_event_(nullptr)
    , desktop_(nullptr)
{
    index_ = std::make_unique<afp_file_index>();
    notify_ = std::make_unique<change_notify>(id_);

    make_desktop_database();
}

afp_volume::~afp_volume()
{
    SetEvent(notify_event_);
}

void afp_volume::make_desktop_database()
{
    desktop_ = std::make_unique<desktop_database>();

    const std::filesystem::path desktop_path = share_path_ / DESKTOP_FILE_NAME;
    const auto desktop_file_exists = GetFileAttributes(desktop_path.c_str()) == MAXDWORD ? false : true;

    if (desktop_file_exists)
    {
        desktop_->from_file(desktop_path);
        return;
    }

    desktop_->to_file(desktop_path);
}

void afp_volume::notify_ignore_path(const std::filesystem::path& path) const
{
    notify_->ignore_path(path);
}
