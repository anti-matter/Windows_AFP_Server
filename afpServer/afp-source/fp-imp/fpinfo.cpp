#include "StdAfx.h"
#include "fpinfo.h"
#include "afpxattr.h"
#include "afp_file_index.h"

void create_finder_info_stream(
    IN const afp_index* entry,
    IN const bool make_invisible
)
{
    afp_finder_info finder_info;

    if (AFP_SUCCESS(get_finder_info(entry, &finder_info)))
    {
        return;
    }

    // If an item didn't have a finder info stream before, set the
    // defaults and add one now.

    const auto attributes = GetFileAttributes(entry->sz_path.c_str());

    if (attributes != 0xFFFFFFFF && attributes & FILE_ATTRIBUTE_HIDDEN || make_invisible)
    {
        finder_info.fd_flags |= kFDInvisible;
    }

    finder_info.fd_location.x = 20;
    finder_info.fd_location.y = 20;

    set_finder_info(entry, &finder_info, true);
}


AFPERROR get_finder_info(
    IN const afp_index* entry,
    OUT afp_finder_info* finder_info
)
{
    DWORD dw_error = NO_ERROR;
    DWORD dw_bytes_read;

    ASSERT(!entry->sz_path.empty());

    std::filesystem::path path = entry->sz_path;
    path.concat(":");
    path.concat(afp_info_stream_name);

    const auto h_file = CreateFile(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (h_file == INVALID_HANDLE_VALUE)
    {
        dw_error = GetLastError();
        DBGWRITE(dbg_level_error, L"CreatFile() failed in get_finder_info! (%d) (%s)\n", dw_error, path.c_str());

        return afp_map_win_error_to_afp(dw_error, afpMiscErr);
    }

    if (!ReadFile(h_file, finder_info, sizeof(afp_finder_info), &dw_bytes_read, nullptr))
    {
        dw_error = GetLastError();
        DBGWRITE(dbg_level_info, L"ReadFile() failed! (%d)\n", dw_error);

        dw_error = afp_map_win_error_to_afp(dw_error, afpMiscErr);
    }

    CloseHandle(h_file);

    return afp_map_win_error_to_afp(dw_error, afpMiscErr);
}


AFPERROR set_finder_info(
    IN const afp_index* entry,
    IN const afp_finder_info* finder_info,
    IN const bool create_always
)
{
    HANDLE h_file;
    DWORD dw_error = NO_ERROR;
    DWORD bytes_written = 0;

    ASSERT(entry != NULL);
    ASSERT(!entry->sz_path.empty());

    std::filesystem::path path = entry->sz_path;
    path.concat(":");
    path.concat(afp_info_stream_name);

    for (;;)
    {
        // Now open the AFP_Info stream to write to it.
        h_file = CreateFile(
            path.c_str(),
            FILE_WRITE_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            create_always ? OPEN_ALWAYS : OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

        if (h_file == INVALID_HANDLE_VALUE)
        {
            dw_error = GetLastError();

            // Sometimes we have to wait for a copy operation to complete before we can
            // open and muck with the file. This occurs mainly during copy operations performed
            // on the windows machine.
            if (dw_error == ERROR_SHARING_VIOLATION)
            {
                DBGWRITE(dbg_level_info, L"Sleeping for 500ms for notify wait!\n");

                Sleep(500);

                dw_error = NO_ERROR;
                continue;
            }

            DBGWRITE(dbg_level_error,
                     L"CreatFile() failed! (%d) (%s)\n",
                     dw_error,
                     path.c_str());

            dw_error = afp_map_win_error_to_afp(dw_error, afpMiscErr);
        }

        break;
    }

    if (dw_error == NO_ERROR)
    {
        if (!WriteFile(h_file, finder_info, sizeof(afp_finder_info), &bytes_written, nullptr))
        {
            dw_error = GetLastError();
            DBGWRITE(dbg_level_error, L"WriteFile() failed! (%d) (%s)\n", dw_error, path.c_str());
        }

        if (dw_error == NO_ERROR)
        {
            ASSERT(bytes_written == sizeof(afp_finder_info));
            DBGWRITE(dbg_level_trace, L"FinderInfo updated successfully for %s.\n", path.c_str());
        }

        CloseHandle(h_file);
    }

    return afp_map_win_error_to_afp(dw_error, afpMiscErr);
}

