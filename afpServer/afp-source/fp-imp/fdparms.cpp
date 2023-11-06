#include "StdAfx.h"
#include <winerror.h>

#include "afp.h"
#include "afpdebug.h"
#include "afphelp.h"
#include "fdparms.h"
#include "fpinfo.h"
#include "afpxattr.h"
#include "afp_open_file.h"
#include "afp_volumes.h"
#include "afp_file_index.h"

void afp_check_for_and_create_rsrc_stream(
    IN const afp_index* index_entry
)
{
    std::filesystem::path res_path = index_entry->sz_path;
    res_path.concat(":");
    res_path.concat(AFP_RESOURCE_FORK_NAME);


    if (HANDLE h_file = CreateFile(
        res_path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    ); h_file == INVALID_HANDLE_VALUE)
    {
        if (const auto error = GetLastError(); error == ERROR_FILE_NOT_FOUND)
        {
            h_file = CreateFile(
                res_path.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,
                CREATE_NEW,
                FILE_ATTRIBUTE_NORMAL,
                nullptr
            );

            if (h_file != INVALID_HANDLE_VALUE)
            {
                DBGWRITE(dbg_level_all,
                    L"Successfully created new resource stream (%s)\n",
                    res_path.c_str()
                );

                CloseHandle(h_file);
            }
            else
            {
                DBGWRITE(dbg_level_error,
                    L"Failed to create resource stream for %s! (%d)\n",
                    res_path.c_str(),
                    GetLastError()
                );
            }
        }
    }
    else
    {
        CloseHandle(h_file);
    }
}

AFPERROR afp_get_object_parms(
    IN afp_net_buffer& reply,
    IN const UINT16 bitmap,
    IN const INT16 volume_id,
    IN const INT32 dir_id,
    IN const dsi::dsi_connection* dsi_session,
    IN afp_index* entry,
    IN WIN32_FILE_ATTRIBUTE_DATA win_attributes
)
{
    LARGE_INTEGER data_size64{};
    LARGE_INTEGER rsrc_size64{};
    UINT16* long_name_offset = nullptr;
    UINT16* uni_name_offset = nullptr;
    const char* parms_start = reply.get_current_pos_ptr();

    if (bitmap & kFPFileAttributes)
    {
        auto attributes = win_to_afp_attributes(win_attributes.dwFileAttributes);

        // For files, there are a couple flags that Windows doesn't have.
        if ((win_attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
            if (entry->flags & flag_index_resource_fork_open)
            {
                attributes |= kFileRsrcForkOpen;
            }

            if (entry->flags & flag_index_data_fork_open)
            {
                attributes |= kFileDataForkOpen;
            }
        }

        reply.push_num<UINT16>(attributes);
    }

    if (bitmap & kFPParentID)
    {
        reply.push_num<UINT32>(entry->par_id);
    }

    if (bitmap & kFPCreateDate)
    {
        reply.push_num<UINT32>(afp_windows_to_mac_time(&win_attributes.ftCreationTime));
    }

    if (bitmap & kFPModDate)
    {
        reply.push_num<UINT32>(afp_windows_to_mac_time(&win_attributes.ftLastWriteTime));
    }

    if (bitmap & kFPBackupDate)
    {
        reply.push_num<UINT32>(0x80000000);
    }

    if (bitmap & kFPFinderInfo)
    {
        afp_finder_info finder_info;

        if (AFP_SUCCESS(get_finder_info(entry, &finder_info)))
        {
            if (win_attributes.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)
            {
                finder_info.fd_flags |= kFDInvisible;
            }

            finder_info.fd_flags = htons(finder_info.fd_flags);
            finder_info.fd_location.x = static_cast<SHORT>(htons(finder_info.fd_location.x));
            finder_info.fd_location.y = static_cast<SHORT>(htons(finder_info.fd_location.y));
            finder_info.fd_fldr = static_cast<SHORT>(htons(finder_info.fd_fldr));

            reply.push_raw_data(&finder_info, sizeof(afp_finder_info));
        }
    }

    if (bitmap & kFPLongName)
    {
        long_name_offset = reply.get_ptr_offset<UINT16>();
    }

    if (bitmap & kFPFileNum)
    {
        reply.push_num(entry->object_id);
    }

    // The maps are a little different here for file and directories.
    if (win_attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
        if (bitmap & kFPDirOffCount)
        {
            const auto offspring = static_cast<UINT16>(afp_volumes::current()
                ->find(volume_id)
                ->get_index()
                ->get_offspring(entry->object_id));
            reply.push_num<UINT16>(offspring);
        }

        if (bitmap & (kFPDirOwnerID | kFPDirGroupID))
        {
            if (bitmap & kFPDirOwnerID)
            {
                constexpr DWORD owner_id = 0;
                reply.push_num<UINT32>(owner_id);
            }

            if (bitmap & kFPDirGroupID)
            {
                constexpr DWORD group_id = 0;
                reply.push_num<UINT32>(group_id);
            }
        }

        if (bitmap & kFPDirAccess)
        {
            DWORD rights = 0;

            const auto user_name = _wcsdup(dsi_session->get_user_name().c_str());
            const auto domain_name = _wcsdup(dsi_session->get_domain_name().c_str());

            const auto win_error = afp_get_directory_rights(
                dsi_session->get_user_token(),
                const_cast<wchar_t*>(entry->sz_path.c_str()),
                user_name,
                lstrlen(domain_name) > 0 ? domain_name : nullptr,
                &rights
            );

            if (SUCCEEDED(win_error))
            {
                reply.push_num(rights);
            }

            free(user_name);
            free(domain_name);
        }

        if (bitmap & kFPUnicodeName)
        {
            uni_name_offset = reply.get_ptr_offset<UINT16>();
        }
    }
    else
    {
        if (bitmap & (kFPDFLen | kFPExtDataForkLen))
        {
            data_size64.LowPart = GetCompressedFileSize(entry->sz_path.c_str(), reinterpret_cast<LPDWORD>(&data_size64.HighPart));
            if (data_size64.LowPart == INVALID_FILE_SIZE && GetLastError() != NO_ERROR)
            {
                data_size64.LowPart = 0;
                DBGWRITE(dbg_level_warning, L"GetCompressedFileSize() failed! (%d)\n", GetLastError());
            }
            else
            {
                DBGWRITE(dbg_level_all, L"Data fork size =  (%d)\n", data_size64.LowPart);
            }

            if (bitmap & kFPDFLen)
            {
                reply.push_num<UINT32>(data_size64.LowPart);
            }
        }

        if (bitmap & (kFPRFLen | kFPExtRsrcForkLen))
        {
            afp_open_file afp_file(volume_id, dir_id, kReadMode, static_cast<INT8>(kAttrFork), entry, entry->sz_path.c_str());

            rsrc_size64.QuadPart = afp_file.get_stream_length(const_cast<wchar_t*>(AFP_RESOURCE_FORK_NAME_PLAIN));

            if (rsrc_size64.QuadPart == BAD_LENGTH_64)
            {
                DBGWRITE(dbg_level_warning, L"GetStreamLength() failed! (%d) (%s)\n", GetLastError(), entry->sz_object_name.c_str());
                rsrc_size64.QuadPart = 0;
            }
            else
            {
                DBGWRITE(dbg_level_all, L"Resource fork size =  (%d)\n", rsrc_size64.LowPart);
            }
        }

        if (bitmap & kFPRFLen)
        {
            reply.push_num<UINT32>(rsrc_size64.LowPart);
        }

        if (bitmap & kFPExtDataForkLen)
        {
            reply.push_num<UINT64>(data_size64.QuadPart);
        }

        if (bitmap & kFPUnicodeName)
        {
            uni_name_offset = reply.get_ptr_offset<UINT16>();
        }

        if (bitmap & kFPExtRsrcForkLen)
        {
            // Not sure what the client is doing here, but it needs an
            // extra 2 bytes inserted here to read the resource fork length
            // correctly. Is it possible it's looking for the launch limit? The
            // spec isn't clear.
            reply.push_num<UINT32>(0);
            reply.push_num<UINT64>(rsrc_size64.QuadPart);
        }
    }

    if (bitmap & kFPLongName || bitmap & kFPUnicodeName)
    {
        //get_char_map_for_index_entry(entry, char_map);

        if (bitmap & kFPLongName)
        {
            *long_name_offset = htons(static_cast<UINT16>(reply.get_current_pos_ptr() - parms_start));
            reply.push_path(kLongNames, entry->sz_object_name);
        }

        if (bitmap & kFPUnicodeName)
        {
            *uni_name_offset = htons(static_cast<UINT16>(reply.get_current_pos_ptr() - parms_start));
            reply.push_path(kUnicodeNames, entry->sz_object_name);
        }
    }

    DBG_DUMP_BUFFER(reply.get_buffer(), 256);

    return AFP_OK;
}

AFPERROR afp_set_file_dir_parms(
    IN const WORD bitmap,
    IN const DWORD file_attributes,
    IN const afp_index* index_entry,
    IN afp_net_buffer& request
)
{
    // Change the file's attributes, we only allow some to be
    // changed as per the spec.
    if (bitmap & kFPFileAttributes)
    {
        DWORD win_attributes = file_attributes;

        if (const auto afp_attributes = request.pull_num<UINT16>(); afp_attributes & kFileSetClearFlag)
        {
            if (afp_attributes & kFileInvisible)
                win_attributes |= FILE_ATTRIBUTE_HIDDEN;
            if (afp_attributes & kFileSystem)
                win_attributes |= FILE_ATTRIBUTE_SYSTEM;
            if (afp_attributes & (kFileReadOnly | kFileDeleteInhibit))
                win_attributes |= FILE_ATTRIBUTE_READONLY;
            if (afp_attributes & kFileNeedsBackup)
                win_attributes |= FILE_ATTRIBUTE_ARCHIVE;
        }
        else
        {
            if (afp_attributes & kFileInvisible)
                win_attributes &= ~FILE_ATTRIBUTE_HIDDEN;
            if (afp_attributes & kFileSystem)
                win_attributes &= ~FILE_ATTRIBUTE_SYSTEM;
            if (afp_attributes & (kFileReadOnly | kFileDeleteInhibit))
                win_attributes &= ~FILE_ATTRIBUTE_READONLY;
            if (afp_attributes & kFileNeedsBackup)
                win_attributes &= ~FILE_ATTRIBUTE_ARCHIVE;
        }

        if (!SetFileAttributes(index_entry->sz_path.c_str(), win_attributes))
        {
            const DWORD win_error = GetLastError();
            DBGWRITE(dbg_level_info, L"SetFileAttributes() failed! (%d)\n", win_error);

            return (afp_map_win_error_to_afp(win_error, afpMiscErr));
        }
    }

    ULONG create_date = 0;
    ULONG mod_date = 0;

    if (bitmap & (kFPCreateDate | kFPModDate | kFPBackupDate))
    {
        if (bitmap & kFPCreateDate)
        {
            create_date = request.pull_num<UINT32>();
        }
        if (bitmap & kFPModDate)
        {
            mod_date = request.pull_num<UINT32>();
        }
        if (bitmap & kFPBackupDate)
        {
            request.pull_num<UINT32>();
        }
    }

    if (bitmap & kFPFinderInfo)
    {
        afp_finder_info finder_info;
        request.pull_raw_data(&finder_info, sizeof(finder_info));

        finder_info.fd_flags = ntohs(finder_info.fd_flags);
        finder_info.fd_location.x = static_cast<SHORT>(ntohs(finder_info.fd_location.x));
        finder_info.fd_location.y = static_cast<SHORT>(ntohs(finder_info.fd_location.y));
        finder_info.fd_fldr = static_cast<SHORT>(ntohs(finder_info.fd_fldr));

        set_finder_info(index_entry, &finder_info, FALSE);
    }

    if (file_attributes & FILE_ATTRIBUTE_DIRECTORY)
    {
        if (bitmap & kFPDirOwnerID)
        {
        }

        if (bitmap & kFPDirGroupID)
        {
        }

        if (bitmap & kFPDirAccess)
        {
        }
    }

    // We wait till the very end to do this since many things above will
    // change the modification date of an object.
    if (bitmap & (kFPCreateDate | kFPModDate | kFPBackupDate))
    {
        afp_set_file_time(
            index_entry->sz_path.c_str(),
            file_attributes & FILE_ATTRIBUTE_DIRECTORY ? TRUE : FALSE,
            create_date,
            mod_date
        );
    }

    return AFP_OK;
}

AFPERROR afp_set_file_time(
    IN const WCHAR* windows_path,
    IN const BOOL is_directory,
    IN const UINT32 creation,
    IN const UINT32 modification
)
{
    HANDLE h_file;
    FILETIME ft_creation, ft_mod;

    // To set the file time, we have to have a handle to
    // the file object.
    if (is_directory)
    {
        h_file = CreateFile(
            windows_path,
            FILE_GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS,
            nullptr);
    }
    else
    {
        h_file = CreateFile(
            windows_path,
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS,
            nullptr
        );
    }

    if (h_file == INVALID_HANDLE_VALUE)
    {
        const auto win_error = GetLastError();
        DBGWRITE(dbg_level_error,
                 L"CreatFile() failed in AFPSetFileTime! (%d)\n", win_error);

        return win_error == ERROR_FILE_NOT_FOUND ? afpObjectNotFound : afpMiscErr;
    }

    // Convert all the mac times to FILETIME structures.
    if (creation != 0)
    {
        afp_mac_to_windows_time(creation, &ft_creation);
        if (!SetFileTime(h_file, &ft_creation, nullptr, nullptr))
        {
            DBGWRITE(dbg_level_error, L"SetFileTime() failed setting creation date. (%d)\n", GetLastError());
        }
    }
    if (modification != 0)
    {
        afp_mac_to_windows_time(modification, &ft_mod);
        if (!SetFileTime(h_file, nullptr, &ft_mod, nullptr))
        {
            DBGWRITE(dbg_level_error, L"SetFileTime() failed setting modification date. (%d)\n", GetLastError());
        }
    }

    CloseHandle(h_file);

    return AFP_OK;
}

WORD win_to_afp_attributes(
    IN const DWORD win_attributes
)
{
    WORD attributes = 0;

    if (win_attributes & FILE_ATTRIBUTE_HIDDEN)
        attributes |= kFileInvisible;
    if (win_attributes & FILE_ATTRIBUTE_SYSTEM)
        attributes |= kFileSystem;
    if (win_attributes & FILE_ATTRIBUTE_READONLY)
        attributes |= (kFileReadOnly | kFileDeleteInhibit | kFileRenameInhibit);
    if (win_attributes & FILE_ATTRIBUTE_ARCHIVE)
        attributes |= kFileNeedsBackup;
    if (win_attributes & FILE_ATTRIBUTE_SYSTEM)
        attributes |= kFileSystem;

    return attributes;
}
