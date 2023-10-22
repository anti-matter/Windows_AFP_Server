#include "stdafx.h"
#include "afp_volume.h"

afp_volume::afp_volume()
    : id_(0)
    , afp_flags_(0)
    , internal_flags_(0)
    , creation_date_()
    , modification_date_()
    , backup_date_()
    , is_dirty_(false)
    , index_(nullptr)
    , desktop_(nullptr)
    , notifyEvent_(nullptr)
{
    index_ = std::make_unique<afp_file_index>();
}

afp_volume::~afp_volume()
{
}
