#include "stdafx.h"
#include "afp_volume.h"

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
    , desktop_(nullptr)
    , notify_event_(nullptr)
{
    index_ = std::make_unique<afp_file_index>();
}

afp_volume::~afp_volume()
{
}
