//===================================================================
// volumes.c
//===================================================================

#include "stdafx.h"
#include "afpdebug.h"
#include "afphelp.h"
#include "volumes.h"
#include "fdparms.h"
#include "afp_net_buffer.h"
#include "afp_registry.h"
#include "afp_volumes.h"


DWORD AFPAddNewVolumeFromUI(
    IN WCHAR* share_name,
    IN WCHAR* share_path,
    IN WCHAR* share_password
)
{
    afp_registry the_registry;

    const DWORD dw_error = the_registry.add_volume(share_name, share_path, share_password);
    if (SUCCEEDED(dw_error))
    {
        std::filesystem::path path = share_path;
        afp_volumes::current()->add_volume(share_name, path, 0);
    }

    return dw_error;
}


DWORD AFPRemoveVolumeFromUI(
    IN WCHAR* share_name
)
{
    const auto volume = afp_volumes::current()->find(share_name);
    if (volume != nullptr)
    {
        afp_volumes::current()->remove_volume(volume->get_volume_id());
    }

    return afp_registry::remove_volume(share_name);
}


//---------------------------------------------------------------------
// AFPGetVolumeParameters()
//---------------------------------------------------------------------
// Returns volume parameters according to the provided bitmap. pBuffer
// needs to be big enough to handle all the parameters requested.
//
// This call is used primarily by the FPOpenVol and FPGetVolParms AFP
// API calls.
//

AFPERROR AFPGetVolumeParameters(
    IN const INT16 afp_volume_id,
    IN const INT16 afp_bitmap,
    IN afp_net_buffer& afp_reply
)
{
    UINT16* volume_name_offset = nullptr;
    const std::shared_ptr<afp_volume> afp_volume = afp_volumes::current()->find(afp_volume_id);
    ULARGE_INTEGER free_bytes_available{};
    ULARGE_INTEGER num_bytes_in_disk{};
    ULARGE_INTEGER num_free_bytes{};

    if (afp_volume == nullptr)
    {
        DBGWRITE(dbg_level_warning, L"Volume ID not found in AFPGetVolumeParameters()!\n");
        return afpIDNotFound;
    }

    // Always send back the bitmap we were provided.
    afp_reply.push_num(afp_bitmap);

    if (afp_bitmap & (kFPVolBytesFreeBit | kFPVolBytesTotalBit |
        kFPVolExtBytesFree | kFPVolExtBytesTotal | kFPVolBlockSize))
    {
        free_bytes_available.QuadPart = 0;
        num_bytes_in_disk.QuadPart = 0;
        num_free_bytes.QuadPart = 0;

        if (GetDiskFreeSpaceEx(
            afp_volume->get_share_path().c_str(),
            &free_bytes_available,
            &num_bytes_in_disk,
            &num_free_bytes) == FALSE)
        {
            const DWORD dw_error = GetLastError();

            if ((dw_error != ERROR_IO_PENDING) && (dw_error != NO_ERROR))
            {
                DBGWRITE(dbg_level_warning, L"GetFreeDiskSpaceEx() failed! (%d)\n", dw_error);
                return afpMiscErr;
            }
        }
    }

    if (afp_bitmap & kFPVolAttributeBit)
    {
        UINT16 attributes = kFPVolSupportsUnicodeNames | kFPVolSupportsFileIDs | kNoExchangeFiles | kSupportsExtAttrs;
        WCHAR volume_name[32]{};
        WCHAR file_system_name[32]{};
        DWORD max_file_name_len = 0;
        DWORD system_flags = 0;

        if (GetVolumeInformation(
            afp_volume->get_share_path().root_name().c_str(),
            volume_name,
            sizeof(volume_name),
            nullptr,
            &max_file_name_len,
            &system_flags,
            file_system_name,
            sizeof(file_system_name)
        ))
        {
            if (system_flags & FILE_READ_ONLY_VOLUME || afp_volume->get_internal_flags() & flag_volume_readonly)
            {
                attributes |= kFPVolReadOnly;
            }
        }
        else if (afp_volume->get_internal_flags() & flag_volume_readonly)
        {
            attributes |= kFPVolReadOnly;
        }

        if (afp_volume->get_internal_flags() & flag_volume_has_password)
        {
            attributes |= kFPVolHasVolumePassword;
        }

        afp_reply.push_num(attributes);
    }

    if (afp_bitmap & kFPVolSignatureBit)
    {
        afp_reply.push_num<UINT16>(kFixedDirectoryID);
    }
    if (afp_bitmap & kFPVolCreateDateBit)
    {
        afp_reply.push_num(afp_windows_to_mac_time(afp_volume->get_create_date()));
    }
    if (afp_bitmap & kFPVolModDateBit)
    {
        afp_reply.push_num(afp_windows_to_mac_time(afp_volume->get_modified_date()));
    }
    if (afp_bitmap & kFPVolBackupDateBit)
    {
        afp_reply.push_num(afp_windows_to_mac_time(afp_volume->get_backup_date()));
    }
    if (afp_bitmap & kFPVolIDBit)
    {
        afp_reply.push_num(afp_volume->get_volume_id());
    }

    if (afp_bitmap & kFPVolBytesFreeBit)
    {
        // The max allowable on AFP 2.2 clients is 2GB.
        if (free_bytes_available.QuadPart > MAXLONG)
        {
            afp_reply.push_num(MAXLONG);
        }
        else
        {
            afp_reply.push_num(free_bytes_available.LowPart);
        }
    }

    if (afp_bitmap & kFPVolBytesTotalBit)
    {
        // The max allowable on AFP 2.2 clients is 2GB. As per the AFP 3.0 spec,
        // if the volume is greater than 4GB, we return the total size of the volume
        // minus 4GB.
        if (num_bytes_in_disk.QuadPart > MAXLONG)
        {
            afp_reply.push_num(MAXLONG);
        }
        else
        {
            afp_reply.push_num(num_bytes_in_disk.LowPart);
        }
    }

    // Save the pointer to where we need to write the offset
    // to the volume name.
    if (afp_bitmap & kFPVolNameBit)
    {
        volume_name_offset = reinterpret_cast<UINT16*>(afp_reply.get_current_pos_ptr());
        afp_reply.advance_pos_ptr(sizeof(UINT16));
    }

    // The following 3 bits will only be set if the caller
    // is using AFP 2.2 or later.
    if (afp_bitmap & kFPVolExtBytesFree)
    {
        afp_reply.push_num(num_free_bytes.QuadPart);
    }
    if (afp_bitmap & kFPVolExtBytesTotal)
    {
        afp_reply.push_num(num_bytes_in_disk.QuadPart);
    }

    if (afp_bitmap & kFPVolBlockSize)
    {
        DWORD allocation_size;

        // Since we only work on NTFS volumes, we use the NTFS
        // file allocation table to determine what the block
        // size is.
        //
        // BUGBUG: Find API that will get us the real number if it exists!

        if (const auto capacity = num_bytes_in_disk.QuadPart; capacity <= 512000000)
        {
            allocation_size = 512;
        }
        else if (capacity <= 1024000000)
        {
            allocation_size = 1024;
        }
        else if (capacity <= 2048000000)
        {
            allocation_size = 2048;
        }
        else
        {
            allocation_size = 4096;
        }

        afp_reply.push_num(allocation_size);

        DBGWRITE(dbg_level_trace, L"Allocation block size = %d (0x%x)\n", allocation_size, allocation_size);
    }

    if (afp_bitmap & kFPVolNameBit)
    {
        *volume_name_offset = htons(static_cast<u_short>(afp_reply.get_data_length() - sizeof(UINT16)));
        afp_reply.push_string(afp_volume->get_share_name());
    }

    return AFP_OK;
}

