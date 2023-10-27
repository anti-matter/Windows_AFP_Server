#pragma once
#include <filesystem>
#include <mutex>

class afp_volume;

// This class implements the change notification mechanism that updates
// the afp server when a windows client changes files/folders on
// the afp volume.
class change_notify
{
public:
    explicit change_notify(UINT16 volume_id);

    // When the server is making changes on the filesystem, we don't
    // want the change notify mechanism to collide with changes
    // being made.
    void ignore_path(const std::filesystem::path& path);

    void afp_notify_thread();

    void afp_read_dir_changes(
        IN const std::shared_ptr<afp_volume>& volume,
        IN HANDLE h_directory,
        IN PBYTE result_buffer
    );

    void afp_change_notify_add(
        IN const FILE_NOTIFY_INFORMATION* file_notify_info,
        IN const std::shared_ptr<afp_volume>& volume
    );

    static void afp_change_notify_remove(
        IN const FILE_NOTIFY_INFORMATION* file_notify_info,
        IN const std::shared_ptr<afp_volume>& volume
    );

private:
    UINT16 volume_id_;

    std::vector<std::filesystem::path> ignore_paths_;
    std::mutex ignore_paths_mutex_;
};
