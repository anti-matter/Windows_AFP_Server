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

static mac_extension_map ext_map[] = {  // NOLINT(misc-use-anonymous-namespace)
    {.extension = L".sit",   .type = "SIT5", .creator = "SIT!"},
    {.extension = L".hqx",   .type = "TEXT", .creator = "SITx"},
    {.extension = L".zip",   .type = "ZIP ", .creator = "SITx"},
    {.extension = L".bin",   .type = "BINA", .creator = "SITx"},
    {.extension = L".txt",   .type = "TEXT", .creator = "ttxt"},
    {.extension = L".img",   .type = "dimg", .creator = "ddsk"},
    {.extension = L".image", .type = "dimg", .creator = "ddsk"},
    {.extension = L".p",     .type = "TEXT", .creator = "CWIE"},
    {.extension = L".cp",    .type = "TEXT", .creator = "CWIE"},
    {.extension = L".cpp",   .type = "TEXT", .creator = "CWIE"},
    {.extension = L".c",     .type = "TEXT", .creator = "CWIE"},
    {.extension = L".h",     .type = "TEXT", .creator = "CWIE"},
    {.extension = L".hpp",   .type = "TEXT", .creator = "CWIE"},
    {.extension = L".pch",   .type = "TEXT", .creator = "CWIE"},
    {.extension = L".pch++", .type = "TEXT", .creator = "CWIE"},
    {.extension = L".prj",   .type = "MMPr", .creator = "CWIE"}, //CW Professional 1 project file
    {.extension = L".mcp",   .type = "MMPr", .creator = "CWIE"}, //CW Professional 4+ project file
    {.extension = L".ppob",  .type = "rsrc", .creator = "MWC2"}, //PowerPlant object file (CW Professional 4+)
    {.extension = L".err",   .type = "MMCH", .creator = "CWIE"},
    {.extension = L".res",   .type = "rsrc", .creator = "RSED"}
};

/// <summary>
/// This function will apply the default finder info based on the extension of the file.
/// </summary>
/// <param name="filename"></param>
/// <param name="finfo"></param>
void finder_info_based_on_extension( // NOLINT(misc-use-internal-linkage)
    IN const std::wstring& filename,
    OUT afp_finder_info* finfo)  
{
    std::string result;
    const std::filesystem::path filepath = filename;
    const std::wstring extension = filepath.extension().wstring();

    for (UINT i = 0; i < sizeof(ext_map); i++)
    {
        if (extension == ext_map[i].extension)
        {
            memcpy(finfo->fd_type, ext_map[i].type, 4);
            memcpy(finfo->fd_creator, ext_map[i].creator, 4);

            return;
        }
    }

    memcpy(finfo->fd_type, "????", 4);
    memcpy(finfo->fd_creator, "????", 4);
}

/// <summary>
/// Read in the finder info from the AFP_Info stream. If the stream
/// does not exist, or the finder info is invalid, then we will call
/// the finder_info_based_on_extension() function to set the default.
/// </summary>
/// <param name="entry"></param>
/// <param name="finder_info"></param>
/// <returns></returns>
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
        if (dw_error != ERROR_FILE_NOT_FOUND)
        {
            DBGWRITE(dbg_level_error, L"CreateFile() failed (%d) (%s)\n", dw_error, path.c_str());
        }
    }

    if (dw_error == ERROR_SUCCESS)
    {
        if (!ReadFile(h_file, finder_info, sizeof(afp_finder_info), &dw_bytes_read, nullptr))
        {
            dw_error = GetLastError();
            DBGWRITE(dbg_level_error, L"ReadFile() failed! (%d)\n", dw_error);
        }
    }

    if (h_file != INVALID_HANDLE_VALUE)
    {
        CloseHandle(h_file);
    }

    if (dw_error != ERROR_SUCCESS || memcmp(finder_info->fd_type, "????", 4) == 0)
    {
        finder_info_based_on_extension(entry->sz_path.wstring(), finder_info);
    }

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
                DBGWRITE(dbg_level_warning, L"Sleeping for 500ms for notify wait!\n");

                Sleep(500);

                dw_error = NO_ERROR;
                continue;
            }

            DBGWRITE(dbg_level_error,
                     L"CreateFile() failed! (%d) (%s)\n",
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

