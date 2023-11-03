#pragma once
#include <filesystem>

#include "afp_volume.h"

constexpr UINT32 change_notify_result_buffer_size = 64 * 1024;

class afp_volume;

// This class implements the change notification mechanism that updates
// the afp server when a windows client changes files/folders on
// the afp volume.
class change_notify
{
public:
    explicit change_notify(INT16 volume_id);

    void afp_notify_thread();

    void afp_read_dir_changes(
        IN const std::shared_ptr<afp_volume>& volume,
        IN HANDLE h_directory,
        IN PBYTE result_buffer 
    );

    static void afp_change_notify_add(
        IN const FILE_NOTIFY_INFORMATION* file_notify_info,
        IN const std::shared_ptr<afp_volume>& volume
    );

    static void afp_change_notify_remove(
        IN const FILE_NOTIFY_INFORMATION* file_notify_info,
        IN const std::shared_ptr<afp_volume>& volume
    );

    void suspend()
    {
        notify_suspended_ = true;
        SetEvent(notify_overlapped_.hEvent);
    }

    void resume()
    {
        RtlZeroMemory(result_buffer_.get(), change_notify_result_buffer_size);
        notify_suspended_ = false;
    }

private:
    INT16 volume_id_;
    std::atomic_bool notify_suspended_;
    OVERLAPPED notify_overlapped_;

    std::unique_ptr<BYTE[]> result_buffer_;
};


// Convenience class for suspending the notify thread, so that the thread
// will resume when this class is destructed.
class notify_suspend  // NOLINT(cppcoreguidelines-special-member-functions)
{
public:
    explicit notify_suspend(const afp_volume* volume) : notify_(volume->get_change_notify())
    {
        notify_->suspend();
    }

    ~notify_suspend()
    {
        notify_->resume();
    }

private:
    change_notify* notify_;
};
