#include "StdAfx.h"
#include "fpinfo.h"
#include "afpxattr.h"


BOOL create_finder_info_stream(
    IN afp_index* entry,
    IN AFPCHARMAPARRAY char_map,
    IN bool make_invisible
)
{
    afp_extended_finder_info finder_info_ex;

    if (AFP_FAILED(get_finder_info_ex(entry, &finder_info_ex)))
    {
        const auto attributes = GetFileAttributes(entry->sz_path.c_str());

        if (attributes != 0xFFFFFFFF && attributes & FILE_ATTRIBUTE_HIDDEN || make_invisible)
        {
            finder_info_ex.finder_info.fd_flags |= kFDInvisible;
        }

        finder_info_ex.finder_info.fd_location.x = 20;
        finder_info_ex.finder_info.fd_location.y = 20;

        // Copy the charMap into the extended finder info structure.
        if (char_map != nullptr)
        {
            RtlCopyMemory(&finder_info_ex.char_map, char_map, sizeof(AFPCHARMAPARRAY));
        }

        // If an item didn't have a finder info stream before, set the
        // defaults and add one now.
        set_finder_info_ex(entry, &finder_info_ex, true);

        return false;
    }

    return true;
}


AFPERROR get_finder_info(
    IN afp_index* entry,
    OUT afp_finder_info* finder_info
)
{
    afp_extended_finder_info extended_finder_info;

    const auto afp_error = get_finder_info_ex(entry, &extended_finder_info);
    if (AFP_SUCCESS(afp_error))
    {
        *finder_info = extended_finder_info.finder_info;
    }

    return afp_error;
}


AFPERROR get_finder_info_ex(
    IN afp_index* entry,
    OUT afp_extended_finder_info* extended_finder_info
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
        DBGWRITE(dbg_level_error, L"CreatFile() failed in get_finder_info_ex! (%d) (%s)\n", dw_error, path.c_str());

        return afp_map_win_error_to_afp(dw_error, afpMiscErr);
    }

    if (!ReadFile(h_file, extended_finder_info, sizeof(afp_extended_finder_info), &dw_bytes_read, nullptr))
    {
        dw_error = GetLastError();
        DBGWRITE(dbg_level_info, L"[fpinfo]ReadFile() failed! (%d)\n", dw_error);

        dw_error = afp_map_win_error_to_afp(dw_error, afpMiscErr);
    }

    if (extended_finder_info->version != extended_finder_info_version)
    {
        // Clear everything out and start over (with the extended portion).
        afp_extended_finder_info new_info;
        new_info.finder_info = extended_finder_info->finder_info;

        set_finder_info_ex(entry, &new_info, true);

        *extended_finder_info = new_info;
    }

    CloseHandle(h_file);

    return afp_map_win_error_to_afp(dw_error, afpMiscErr);
}


AFPERROR set_finder_info(
    IN afp_index* entry,
    IN const afp_finder_info* finder_info,
    IN const bool create_always
)
{
    afp_extended_finder_info extended_finder_info;

    if (create_always)
    {
        extended_finder_info.finder_info = *finder_info;
    }
    else
    {
        const auto afp_error = get_finder_info_ex(entry, &extended_finder_info);
        if (AFP_SUCCESS(afp_error))
        {
            extended_finder_info.finder_info = *finder_info;
        }
    }

    return set_finder_info_ex(entry, &extended_finder_info, create_always);
}


AFPERROR set_finder_info_ex(
    IN afp_index* entry,
    IN const afp_extended_finder_info* extended_finder_info,
    IN const bool create_always
)
{
    HANDLE h_file;
    DWORD dw_error = NO_ERROR;
    DWORD bytes_written = 0;

    ASSERT(entry != NULL);
    ASSERT(entry->sz_path.string().length() > 0);

    std::filesystem::path path = entry->sz_path;
    path.concat(":");
    path.concat(afp_info_stream_name);

    for (;;)
    {
        //
        //Now open the AFP_Info stream to write to it.
        //
        h_file = CreateFile(
            path.c_str(),
            GENERIC_WRITE,
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
        if (!WriteFile(h_file, extended_finder_info, sizeof(afp_extended_finder_info), &bytes_written, nullptr))
        {
            dw_error = GetLastError();
            DBGWRITE(dbg_level_error, L"WriteFile() failed! (%d) (%s)\n", dw_error, path.c_str());
        }

        if (dw_error == NO_ERROR)
        {
            ASSERT(bytes_written == sizeof(afp_extended_finder_info));
            DBGWRITE(dbg_level_all, L"FinderInfo updated successfully for %s.\n", path.c_str());

            if (extended_finder_info->char_map[0].wKeyCode != 0)
            {
                entry->flags |= flag_index_has_charmap;
            }
            else
            {
                entry->flags &= ~flag_index_has_charmap;
            }
        }

        CloseHandle(h_file);
    }

    return afp_map_win_error_to_afp(dw_error, afpMiscErr);
}


/*
 * AFPUpdateEntryCharMap()
 *
 * Description:
 *		Update the charmap for a particular file and write it out
 *		to the info stream.
 *
 * Returns: AFPERROR
 */

AFPERROR AFPUpdateEntryCharMap(
    IN afp_index* pEntry,
    IN AFPCHARMAPARRAY pCharmap
)
{
    afp_extended_finder_info infoEx;

    auto afpError = get_finder_info_ex(pEntry, &infoEx);

    if (AFP_SUCCESS(afpError))
    {
        RtlCopyMemory(&infoEx.char_map, pCharmap, sizeof(AFPCHARMAPARRAY));

        afpError = set_finder_info_ex(pEntry, &infoEx);
    }

    return afpError;
}


/*
 * AFPAddCharMapToIndexEntry()
 *
 * Description:
 *		Adds a charmap to an index entry, note that if a charmap
 *		already exists, this will overwrite it.
 *
 * Returns: AFPERROR
 */

AFPERROR AFPAddCharMapToIndexEntry(
    IN afp_index* pEntry,
    IN AFPCHARMAPARRAY pCharmap
)
{
    if (pCharmap[0].wKeyCode != 0)
    {
        pEntry->flags |= flag_index_has_charmap;
    }
    else
    {
        pEntry->flags &= ~flag_index_has_charmap;
    }

    return AFPUpdateEntryCharMap(pEntry, pCharmap);
}


/*
 * AFPGetCharMapForIndexEntry()
 *
 * Description:
 *		Looks up a charmap for an index entry and returns it
 *		it to the caller.
 *
 * Returns: AFPERROR
 */

AFPERROR AFPGetCharMapForIndexEntry(
    IN afp_index* pEntry,
    OUT AFPCHARMAPARRAY pCharmap
)
{
    afp_extended_finder_info infoEx;
    AFPERROR afpError = AFP_OK;

    if (pEntry->flags & flag_index_has_charmap)
    {
        afpError = get_finder_info_ex(pEntry, &infoEx);

        if (AFP_SUCCESS(afpError))
        {
            memcpy(pCharmap, &infoEx.char_map, sizeof(AFPCHARMAPARRAY));
        }
    }

    return afpError;
}


/*
 * AFPCheckForCharMap()
 *
 * Description:
 *		Checks to see if a charmap is currently utilized for this
 *		this index entry. If so, it sets the entry's flag to signal
 *		that an entry exists.
 *
 * Returns: AFPERROR
 */

BOOL AFPCheckForCharMap(
    IN afp_index* pEntry
)
{
    afp_extended_finder_info infoEx;

    if (AFP_SUCCESS(get_finder_info_ex(pEntry, &infoEx)))
    {
        if (infoEx.char_map[0].wKeyCode != 0)
        {
            pEntry->flags |= flag_index_has_charmap;
            return true;
        }
    }

    return false;
}
