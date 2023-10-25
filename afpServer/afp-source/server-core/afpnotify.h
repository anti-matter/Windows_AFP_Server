#pragma once
#include "afp_volume.h"

void afp_notify_thread(
    IN const std::shared_ptr<afp_volume>& param
);

void afp_read_dir_changes(
    IN std::shared_ptr<afp_volume>& volume,
    IN HANDLE h_directory,
    IN PBYTE result_buffer
);

void afp_change_notify_add(
    IN FILE_NOTIFY_INFORMATION * file_notify_info,
    IN std::shared_ptr<afp_volume>& volume
);

void afp_change_notify_remove(
    IN const FILE_NOTIFY_INFORMATION * file_notify_info,
    IN const std::shared_ptr<afp_volume>& volume
);
