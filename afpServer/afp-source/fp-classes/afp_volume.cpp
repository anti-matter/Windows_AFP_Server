#include "stdafx.h"
#include "afp_volume.h"
#include "afp_desktop.h"
#include "change_notify.h"

afp_volume::afp_volume(std::wstring share_name, std::filesystem::path share_path, const INT16 volume_id, const BYTE afp_flags, const UINT32 internal_flags)
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
    , desktop_(nullptr)
{
    index_ = std::make_unique<afp_file_index>(id_);
    notify_ = std::make_unique<change_notify>(id_);
    desktop_ = std::make_unique<afp_desktop>(this);
}

afp_volume::~afp_volume() = default;

void afp_volume::stop_sharing()
{
    // set_internal_flags() ORs bits in, so it cannot clear the shared
    // flag; the flag used to survive "stop sharing" forever.
    clear_internal_flags(flag_volume_shared);

    notify_->suspend();
    desktop_->export_to_file();
}

