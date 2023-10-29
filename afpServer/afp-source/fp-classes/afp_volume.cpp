#include "stdafx.h"
#include "afp_volume.h"
#include "afp_desktop.h"
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
    desktop_ = std::make_unique<afp_desktop>(id_);
}

afp_volume::~afp_volume()
{
    SetEvent(notify_event_);
}

void afp_volume::notify_ignore_path(const std::filesystem::path& path) const
{
    notify_->ignore_path(path);
}
