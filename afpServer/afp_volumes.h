#pragma once
#include "afp_volume.h"

class afp_volumes
{
public:
    afp_volumes();
    ~afp_volumes();

    // The volume indexer thread will traverse the entire volume's tree
    // and add its contents to the index for that volume. This occurs
    // at startup, or when a new volume is shared.
    void volume_indexer();

    // Share a new volume, get it indexed, and then setup the change
    // notify system on the file system.
    void add_volume(const std::wstring& share_name, const std::filesystem::path& share_path, UINT32 flags);

    // Build the AFP index for this volume. This index keeps track
    // of dir id's and dir names. The 'root' of this volume is the
    // share point. We pass kRootDirID as that will be the starting
    // dir id for the root directory of our volume.
    bool index_volume(const std::filesystem::path& share_path, const UINT32 parent_dir_id, afp_volume* volume);

    afp_volume* find(const std::wstring& name);
    afp_volume* find(UINT16 id);

private:
    UINT16 next_volume_id_;

    // Array of shared volumes.
    std::vector<std::unique_ptr<afp_volume>> volumes_;
    std::mutex volumes_mutex_;
};

