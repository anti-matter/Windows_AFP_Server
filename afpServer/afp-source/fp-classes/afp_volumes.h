#pragma once
#include <queue>

#include "afp_volume.h"

enum index_queue_tasks : UINT8
{
    task_delete_index_entry = 0x01,
    task_index_directory,
    task_index_complete
};

struct index_queue_element
{
    index_queue_tasks task;
    std::shared_ptr<afp_volume> volume;
    UINT32 item_id;
    std::filesystem::path path;
};

class afp_volumes
{
public:
    afp_volumes();
    ~afp_volumes();

    static afp_volumes* current();

    // The volume indexer thread will traverse the entire volume's tree
    // and add its contents to the index for that volume. This occurs
    // at startup, or when a new volume is shared.
    void volume_indexer();

    // Share a new volume, get it indexed, and then setup the change
    // notify system on the file system.
    void add_volume(const std::wstring& share_name, const std::filesystem::path& share_path, UINT32 flags);

    // Safely removes a volume from our list.
    void remove_volume(const UINT16 id);

    // Removes all volumes for our tracking list which will cause each
    // of them to be destructed.
    void remove_all_volumes();

    // Build the AFP index for this volume. This index keeps track
    // of dir id's and dir names. The 'root' of this volume is the
    // share point. We pass kRootDirID as that will be the starting
    // dir id for the root directory of our volume.
    bool index_build(const std::filesystem::path& share_path, const UINT32 parent_dir_id, std::shared_ptr<afp_volume>& volume);

    // Thread to handle (avoid) recursive calls into directories when
    // indexing a volume and handling notify events.
    void index_queue_thread();

    // Add a new task element to the queue for thread processing.
    void index_queue_task(index_queue_tasks task, const std::shared_ptr<afp_volume>& volume, UINT32 item_id, std::filesystem::path& path);

    std::shared_ptr<afp_volume> find(const std::wstring& name);
    std::shared_ptr<afp_volume> find(UINT16 id);

    bool map_name_to_id(const std::wstring& share_name, UINT16* id);

    // Return the count of active volumes and optionally fill in
    // the supplied std::vector with their pointers.
    int active_volumes(std::vector<std::shared_ptr<afp_volume>>* volumes_out = nullptr);

    // Return a vector containing all "dirty" volumes.
    void dirty_volumes(std::vector<std::shared_ptr<afp_volume>>* dirty_volumes);

private:
    UINT16 next_volume_id_;

    // Array of shared volumes.
    std::vector<std::shared_ptr<afp_volume>> volumes_;
    std::mutex volumes_mutex_;

    std::queue<std::unique_ptr<index_queue_element>> index_queue_;
    std::atomic_bool index_queue_running_;
    std::mutex index_queue_mutex_;
    HANDLE index_event_;
};
