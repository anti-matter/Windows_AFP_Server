// ReSharper disable CppClangTidyClangDiagnosticGnuZeroVariadicMacroArguments
// ReSharper disable CppParameterMayBeConstPtrOrRef
#include "StdAfx.h"
#include "afpdirectory.h"

#include "afp_desktop.h"
#include "afp_session.h"
#include "afp_volumes.h"
#include "change_notify.h"
#include "fdparms.h"
#include "fpinfo.h"

/*
 * FPResolveID()
 *
 * Description:
 *		Returns file information by file ID.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPResolveID(
    IN afp_net_buffer& afp_request,
    IN afp_net_buffer& afp_reply,
    IN dsi::dsi_connection* connection
)
{
    WIN32_FILE_ATTRIBUTE_DATA win_attributes;

    DBGWRITE(dbg_level_trace, L"Enter...\n");

    afp_request.advance_pos_ptr(sizeof(INT16));

    const auto afp_volume_id = afp_request.pull_num<INT16>();
    const auto afp_file_id = afp_request.pull_num<INT32>();
    auto afp_bitmap = afp_request.pull_num<INT16>();

    // Get a pointer to the volume info structure for this volume.
    const auto afp_session = connection->get_afp_session();
    const std::shared_ptr<afp_volume> afp_volume_info = afp_session->find_open_volume(afp_volume_id);
    if (afp_volume_info == nullptr)
    {
        DBGWRITE(dbg_level_error, L"Bad volume id provided!\n");
        return afpParmErr;
    }

    const std::filesystem::path path;
    const auto afp_entry = afp_volume_info->get_index()->find(afp_file_id,path);
    if (afp_entry == nullptr)
    {
        DBGWRITE(dbg_level_error, L"File entry not found (%d)\n", afp_file_id);
        return afpObjectNotFound;
    }

    // We don't support the following bitmaps, so we clear them.
    if (connection->get_afp_version() < afpVersion30)
    {
        if (afp_bitmap & kFPProDos) afp_bitmap &= ~kFPProDos;
        if (afp_bitmap & kFPShortName) afp_bitmap &= ~kFPShortName;
    }

    // Add in the bitmap and new refnum id.
    afp_reply.push_num(afp_bitmap);

    // Collect attribute data on the file or folder.
    if (!GetFileAttributesEx(afp_entry->sz_path.c_str(), GetFileExInfoStandard, &win_attributes))
    {
        DBGWRITE(dbg_level_info, L"GetFileAttributesEx() failed! (%d)\n", GetLastError());
        DBGWRITE(dbg_level_info, L"path = %s\n", afp_entry->sz_path.c_str());

        return afpObjectNotFound;
    }

    const AFPERROR afp_error = afp_get_object_parms(
        afp_reply,
        afp_bitmap,
        afp_volume_id,
        afp_entry->par_id,
        connection,
        afp_entry,
        win_attributes
    );

    if (AFP_FAILED(afp_error))
    {
        DBGWRITE(dbg_level_info, L"AFPGetObjectParms() failed %d\n", afp_error);
    }

    return afp_error;
}


/*
 * FPGetFileDirParms()
 *
 * Description:
 *		Returns file and/or directory parameters.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPGetFileDirParms(
    IN afp_net_buffer& afp_request,
    IN afp_net_buffer& afp_reply,
    IN dsi::dsi_connection* connection
)
{
    WIN32_FILE_ATTRIBUTE_DATA win_attributes;
    INT8 afp_attributes = 0;

    DBGWRITE(dbg_level_trace, L"Enter...\n");

    afp_request.advance_pos_ptr(sizeof(UINT16));
    const auto afp_volume_id = afp_request.pull_num<INT16>();
    const auto afp_dir_id = afp_request.pull_num<INT32>();
    auto afp_file_bitmap = afp_request.pull_num<INT16>();
    auto afp_dir_bitmap = afp_request.pull_num<INT16>();
    const auto afp_path_type = afp_request.pull_num<UINT8>();

    // The client must have previously opened the volume we are operating on.
    const auto volume = connection->get_afp_session()->find_open_volume(afp_volume_id);
    if (volume == nullptr)
    {
        return afpParmErr;
    }

    const auto afp_pathname = afp_request.pull_path(afp_path_type);

    const auto afp_entry = volume->find(afp_dir_id, afp_pathname);
    if (afp_entry == nullptr)
    {
        DBGWRITE(dbg_level_trace,
                 L"Entry not found in GetFileDirParms! (dirID = %d)(name = %s)\n",
                 afp_dir_id, afp_pathname.c_str());

        return afpObjectNotFound;
    }

    DBGWRITE(dbg_level_info, L"Getting parms for: %s\n", afp_entry->sz_path.c_str());

    if (!GetFileAttributesEx(afp_entry->sz_path.c_str(), GetFileExInfoStandard, &win_attributes))
    {
        return afpObjectNotFound;
    }

    if (win_attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
        afp_attributes |= static_cast<INT8>(kFileDirIsDir);
    }

    // We don't support the following bitmaps, so we clear them. Note that
    // we don't clear the KFPProDos bit in AFP3.x since it is now the unicode
    // name bit.
    if (connection->get_afp_version() < afpVersion30)
    {
        if (afp_dir_bitmap & kFPProDos) afp_dir_bitmap &= ~kFPProDos;
        if (afp_file_bitmap & kFPProDos) afp_file_bitmap &= ~kFPProDos;
    }

    if (afp_dir_bitmap & kFPDirShortName) afp_dir_bitmap &= ~kFPDirShortName;
    if (afp_file_bitmap & kFPShortName) afp_file_bitmap &= ~kFPShortName;

    // We always return copies of the bitmaps and the attributes.
    afp_reply.push_num(afp_file_bitmap);
    afp_reply.push_num(afp_dir_bitmap);
    afp_reply.push_num(afp_attributes);
    afp_reply.push_num<UINT8>(0);

    // If both bitmaps are null, then we are to just return
    // the bitmaps and the attributes.
    if (afp_file_bitmap == kFPFileNone && afp_dir_bitmap == kFPDirNone)
    {
        return AFP_OK;
    }

    const auto afp_error = afp_get_object_parms(
        afp_reply,
        (win_attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? afp_dir_bitmap : afp_file_bitmap,
        afp_volume_id,
        afp_dir_id,
        connection,
        afp_entry,
        win_attributes
    );

    return afp_error;
}


/*
 * FPEnumerate()
 *
 * Description:
 *		Enumerate all the contents of a directory.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPEnumerate(
    IN afp_net_buffer& afp_request,
    IN afp_net_buffer& afp_reply,
    IN dsi::dsi_connection* connection
)
{
    INT16 afp_act_count = 0;
    INT16 afp_objects_found = 0;
    INT32 afp_start_index;
    size_t afp_max_reply_size;

    WIN32_FILE_ATTRIBUTE_DATA win_attributes;

    DBGWRITE(dbg_level_trace, L"Enter...\n");

    const auto afp_command = afp_request.pull_num<UINT8>();
    afp_request.advance_pos_ptr(sizeof(UINT8));

    const auto afp_vol_id = afp_request.pull_num<INT16>();
    const auto afp_dir_id = afp_request.pull_num<INT32>();
    auto afp_file_bitmap = afp_request.pull_num<INT16>();
    auto afp_dir_bitmap = afp_request.pull_num<INT16>();
    const auto afp_req_count = afp_request.pull_num<INT16>();

    // In AFP3.1 we may get the new Ext2 call which allows for a
    // larger number of files to be on the volume.
    switch (afp_command)
    {
    case afpEnumerate:
    case afpEnumerateExt:
        afp_start_index = afp_request.pull_num<INT16>();
        afp_max_reply_size = afp_request.pull_num<INT16>();
        break;

    case afpEnumerateExt2:
        afp_start_index = afp_request.pull_num<INT32>();
        afp_max_reply_size = afp_request.pull_num<INT32>();
        break;

    default:
        return afpParmErr;
    }

    const auto afp_path_type = afp_request.pull_num<INT8>();

    const auto afp_session = connection->get_afp_session();
    const auto afp_volume_info = afp_session->find_open_volume(afp_vol_id);
    if (afp_volume_info == nullptr)
    {
        DBGWRITE(dbg_level_info, L"Volume not opened!\n");
        return afpParmErr;
    }

    const auto afp_pathname = afp_request.pull_path(afp_path_type);

    DBGWRITE(dbg_level_info, L"Enumerating directory: %d, path: %s\n", afp_dir_id, afp_pathname.c_str());
    DBGWRITE(dbg_level_trace, L"Start index: %d\n", afp_start_index);

    // We don't support the following bitmaps, so we clear them.
    if (connection->get_afp_version() < afpVersion30)
    {
        if (afp_dir_bitmap & kFPDirProDOS) afp_dir_bitmap &= ~kFPDirProDOS;
        if (afp_file_bitmap & kFPProDos) afp_file_bitmap &= ~kFPProDos;
    }

    if (afp_dir_bitmap & kFPDirShortName) afp_dir_bitmap &= ~kFPDirShortName;
    if (afp_file_bitmap & kFPShortName) afp_file_bitmap &= ~kFPShortName;

    // The first thing we do is add the bitmaps to the reply buffer.
    afp_reply.push_num(afp_file_bitmap);
    afp_reply.push_num(afp_dir_bitmap);

    // This is where the actual number of bytes in the buffer will
    // be inserted into the buffer.
    auto* afp_act_count_spot = reinterpret_cast<INT16*>(afp_reply.get_current_pos_ptr());
    afp_reply.advance_pos_ptr(sizeof(INT16));

    // Retrieve the index entry of the directory pointed to by the AFP path.
    const auto afp_target_entry = afp_volume_info->find(afp_dir_id, afp_pathname);
    if (afp_target_entry == nullptr)
    {
        DBGWRITE(dbg_level_info, L"Failed to get index entry (dirID = %d, name = %s)\n", afp_dir_id, afp_pathname.c_str());
        return afpObjectNotFound;
    }

    // Create a temporary index array to hold all the offspring for the
    // target directory.
    std::vector<afp_index*> offspring;

    // Now populate the temp array with the offspring in the target dir.
    afp_volume_info->get_index()->get_offspring(afp_target_entry->object_id, offspring);
    if (offspring.empty())
    {
        return afpObjectNotFound;
    }

    constexpr UINT16 temp_buffer_size = 4096;
    const auto temp_buffer = std::make_unique<char[]>(temp_buffer_size);
    if (temp_buffer == nullptr)
    {
        return afpMiscErr;
    }

    AFPERROR afp_error = AFP_OK;
    std::unique_lock<std::recursive_mutex> lock(afp_volume_info->get_index()->get_mutex());

    // Go through the entire index and find entries that have the dirID as their parent.
    const auto it = std::ranges::for_each(offspring.begin(), offspring.end(), [&](afp_index* index_item)
    {
        // If we've gotten as much data as the client requested, then exit this beast.
        if (afp_act_count >= afp_req_count || afp_reply.get_data_length() >= afp_max_reply_size)
        {
            return;
        }

        // We don't include some files (like the desktop db) since they
        // are server specific and the macs shouldn't see or edit them.
        if (!lstrcmp(index_item->sz_object_name.c_str(), desktop_file_name.c_str()))
        {
            return;
        }

        if (GetFileAttributesEx(index_item->sz_path.c_str(), GetFileExInfoStandard, &win_attributes) == 0)
        {
            DBGWRITE(dbg_level_error, L"File does not exist on disk: %s\n", index_item->sz_path.c_str());
            return;
        }

        afp_net_buffer parameter_buffer(temp_buffer.get(), temp_buffer_size);

        afp_objects_found++;
        if (afp_objects_found < afp_start_index)
        {
            return;
        }

        DBGWRITE(dbg_level_trace, L"Enumerating, dir id: %d, pathname: %s\n", afp_dir_id, index_item->sz_object_name.c_str());

        const bool is_directory = index_item->flags & flag_index_is_dir;
        if (afp_dir_bitmap == kFPDirNone && is_directory || afp_file_bitmap == kFPFileNone && !is_directory)
        {
            return;
        }

        UINT8 afp_attributes = 0;
        if (is_directory)
        {
            DBGWRITE(dbg_level_trace, L"Is directory: %s\n", is_directory ? L"TRUE" : L"FALSE");
            afp_attributes |= kFileDirIsDir;
        }

        afp_error = afp_get_object_parms(
            parameter_buffer,
            is_directory ? afp_dir_bitmap : afp_file_bitmap,
            afp_vol_id,
            afp_dir_id,
            connection,
            index_item,
            win_attributes);

        if (AFP_FAILED(afp_error))
        {
            DBGWRITE(dbg_level_error, L"AFPGetObjectParms() failed: %s\n", index_item->sz_path.c_str());
            return;
        }

        UINT8* struct_len = nullptr;
        UINT16* ext_struct_len = nullptr;

        // Check to make sure there is enough room in the buffer to add the
        // file/dir information. There are items after the file dirs parms that are
        // added to the buffer, so we add a little extra to be safe.
        const size_t size_required = parameter_buffer.get_data_length() + afp_reply.get_data_length() + sizeof(UINT32);

        // afp_max_reply_size is the maximum size our return buffer can be.
        if (size_required >= afp_max_reply_size || size_required > afp_reply.get_buffer_size())
        {
            DBGWRITE(dbg_level_warning, L"Breaking out, size required too big!\n");
            return;
        }

        afp_act_count++;

        // The first byte (or word for AFP3.x) contains the struct len.
        if (afp_command > afpEnumerate)
        {
            ext_struct_len = reinterpret_cast<UINT16*>(afp_reply.get_current_pos_ptr());
            afp_reply.advance_pos_ptr(sizeof(UINT16));
        }
        else
        {
            struct_len = reinterpret_cast<UINT8*>(afp_reply.get_current_pos_ptr());
            afp_reply.advance_pos_ptr(sizeof(UINT8));
        }

        afp_reply.push_num(afp_attributes);
        if (afp_command > afpEnumerate)
        {
            afp_reply.push_num<UINT8>(0);
        }

        afp_reply.push_raw_data(parameter_buffer.get_buffer(), parameter_buffer.get_data_length());

        // Set the structure length byte in the buffer. The extra bytes are
        // for the first params in the structure (structLen, attributes and padding).
        auto len = static_cast<UINT16>(parameter_buffer.get_data_length() + (afp_command > afpEnumerate ? 4 : 2));

        // This entry must end on an even boundary.
        if (len % 2)
        {
            afp_reply.push_num<UINT8>(0);
            len++;
        }

        // AFP3.x uses a word to describe the struct length vs. a byte
        if (afp_command > afpEnumerate)
        {
            *ext_struct_len = htons(len);
        }
        else
        {
            *struct_len = static_cast<UINT8>(len);
        }
    });

    // Set the actual number of objects found and in the buffer.
    *afp_act_count_spot = static_cast<INT16>(htons(afp_act_count));

    DBGWRITE(dbg_level_trace, L"afpActCount = %d\n", afp_act_count);
    DBGWRITE(dbg_level_trace, L"Returning: %d\n", (afp_act_count == 0) ? afpObjectNotFound : AFP_OK);

    return (afp_act_count == 0 ? afpObjectNotFound : AFP_OK);
}


/*
 * FPSetFileDirParms()
 *
 * Description:
 *		Set the file or directory parameters.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPSetFileDirParms(
    IN afp_net_buffer& afp_request,
    IN afp_net_buffer& afp_reply,
    IN dsi::dsi_connection* connection
)
{
    UNREFERENCED_PARAMETER(afp_reply);

    WIN32_FILE_ATTRIBUTE_DATA win_attributes;

    DBGWRITE(dbg_level_trace, L"Enter...\n");

    const auto afp_command = afp_request.pull_num<INT8>();
    afp_request.advance_pos_ptr(sizeof(INT8));

    const auto afp_vol_id = afp_request.pull_num<INT16>();
    const auto afp_dir_id = afp_request.pull_num<INT32>();
    const auto afp_bitmap = afp_request.pull_num<UINT16>();
    const auto afp_path_type = afp_request.pull_num<UINT8>();

    const auto volume = connection->get_afp_session()->find_open_volume(afp_vol_id);
    if (volume == nullptr)
    {
        return afpParmErr;
    }

    if (volume->is_read_only())
    {
        return afpVolLocked;
    }

    const auto afp_pathname = afp_request.pull_path(afp_path_type);

    // The remaining parameters for the call must start on an
    // even boundary.
    afp_request.advance_if_odd();

    // Verify the bitmap contains valid settings, FPSetFileDirParms doesn't
    // support the following bitmaps. The user is supposed to use
    // FPSetFileParms or FPSetDirParms for these.
    switch (afp_command)
    {
    case afpSetFlDrParms:
        if (afp_bitmap & (kFPParentID | kFPLongName | kFPShortName |
            kFPFileNum | kFPDFLen | kFPRFLen |
            kFPDirGroupID | kFPDirAccess | kFPDirUnicodeName |
            kFPExtDataForkLen | kFPExtRsrcForkLen))
        {
            DBGWRITE(dbg_level_trace, L"Bad bitmap!\n");
            return afpBitmapErr;
        }
        break;

    case afpSetDirParms:
        if (afp_bitmap & (kFPParentID | kFPLongName | kFPShortName |
            kFPDirOffCount | kFPDirID | kFPDirUnicodeName))
        {
            DBGWRITE(dbg_level_trace, L"[afp:FPSetDirParms]Bad bitmap!\n");
            return afpBitmapErr;
        }
        break;

    case afpSetFileParms:
        if (afp_bitmap & (kFPParentID | kFPLongName | kFPShortName |
            kFPFileNum | kFPDFLen | kFPRFLen |
            kFPExtDataForkLen | kFPUnicodeName | kFPExtRsrcForkLen))
        {
            DBGWRITE(dbg_level_info, L"Bad bitmap!\n");
            return afpBitmapErr;
        }
        break;

    default:
        ASSERT(0);
        return afpCallNotSupported;
    }

    const auto afp_entry = volume->get_index()->find(afp_dir_id, afp_pathname);
    if (afp_entry == nullptr)
    {
        DBGWRITE(dbg_level_info,
                 L"Index entry not found in FPSetFileDirParms! (dirID = %d  path = %s)\n",
                 afp_dir_id, afp_pathname.c_str());

        return afpObjectNotFound;
    }

    if (!GetFileAttributesEx(afp_entry->sz_path.c_str(), GetFileExInfoStandard, &win_attributes))
    {
        DBGWRITE(dbg_level_info, L"GetFileAttributesEx() failed! (%d)\n", GetLastError());
        return afpObjectNotFound;
    }

    const auto afp_error = afp_set_file_dir_parms(
        afp_bitmap,
        win_attributes.dwFileAttributes,
        afp_entry,
        afp_request);

    return afp_error;
}


/*
 * FPCreateDir()
 *
 * Description:
 *		Create a new directory on the volume in the specified dir.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPCreateDir(
    IN afp_net_buffer& afp_request,
    IN afp_net_buffer& afp_reply,
    IN dsi::dsi_connection* connection
)
{
    DBGWRITE(dbg_level_trace, L"FPCreateDir...\n");

    afp_request.advance_pos_ptr(sizeof(INT16));

    const auto volume_id = afp_request.pull_num<INT16>();
    auto dir_id = afp_request.pull_num<INT32>();
    const auto afp_path_type = afp_request.pull_num<UINT8>();
    const auto session = connection->get_afp_session();

    const std::shared_ptr<afp_volume> volume = session->find_open_volume(volume_id);
    if (volume == nullptr)
    {
        DBGWRITE(dbg_level_info, L"Bad volume ID in FPCreateDir!\n");
        return afpParmErr;
    }

    if (volume->is_read_only())
    {
        return afpVolLocked;
    }

    char_map_data char_map;
    const auto afp_pathname = afp_request.pull_path(afp_path_type, &char_map);
    if (afp_pathname.empty())
    {
        DBGWRITE(dbg_level_error, L"Failed to get pathname!\n");
        return afpParmErr;
    }

    // Get the directory entry for the directory where we are creating
    // the new one.
    const auto afp_entry = volume->get_index()->find(dir_id);
    if (afp_entry == nullptr)
    {
        DBGWRITE(dbg_level_error, L"Parent directory not found in FPCreateDir!\n");
        return afpObjectNotFound;
    }

    // Build the path to the new directory using the parent directory's entry.
    // ReSharper disable once CppFunctionResultShouldBeUsed
    const std::filesystem::path win_path = afp_entry->sz_path / afp_pathname;
    if (GetFileAttributes(win_path.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        return afpObjectExists;
    }

    // Make sure only those who are allowed can create directories.
    if (!afp_impersonate_user(connection->get_user_token()))
    {
        return afpAccessDenied;
    }

    notify_suspend suspend(volume.get());

    const auto afp_error = CreateDirectory(win_path.c_str(), nullptr)
                               ? AFP_OK
                               : afp_map_win_error_to_afp(GetLastError(), afpMiscErr);

    afp_revert_to_self(connection->get_user_token());

    if (AFP_SUCCESS(afp_error))
    {
        const auto new_dir_entry = volume->get_index()->add_entry(
            dir_id,
            afp_pathname,
            win_path,
            true,
            &char_map,
            &dir_id);

        create_finder_info_stream(new_dir_entry, false);

        volume->set_is_dirty();

        // Now build the reply block to send back to the client.
        afp_reply.push_num(dir_id);
    }

    return afp_error;
}


/*
 * FPDelete()
 *
 * Description:
 *		Delete a file or directory.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPDelete(
    IN afp_net_buffer& afp_request,
    IN afp_net_buffer& afp_reply,
    IN dsi::dsi_connection* connection
)
{
    UNREFERENCED_PARAMETER(afp_reply);

    DBGWRITE(dbg_level_trace, L"Enter\n");

    afp_request.advance_pos_ptr(sizeof(INT16));

    const auto afp_volume_id = afp_request.pull_num<INT16>();
    const auto afp_dir_id = afp_request.pull_num<INT32>();
    const auto afp_path_type = afp_request.pull_num<UINT8>();

    const auto volume = connection->get_afp_session()->find_open_volume(afp_volume_id);
    if (volume == nullptr)
    {
        return afpParmErr;
    }

    if (volume->is_read_only())
    {
        return afpVolLocked;
    }

    const auto afp_pathname = afp_request.pull_path(afp_path_type);
    if (afp_pathname.empty())
    {
        return afpParmErr;
    }

    const std::shared_ptr<afp_volume> afp_volume_info = connection->get_afp_session()->find_open_volume(afp_volume_id);
    if (afp_volume_info == nullptr)
    {
        DBGWRITE(dbg_level_info, L"Bad volume ID! (%d)\n", afp_volume_id);
        return afpParmErr;
    }

    const auto afp_entry = volume->get_index()->find(afp_dir_id, afp_pathname);
    if (afp_entry == nullptr)
    {
        DBGWRITE(dbg_level_info, L"Index entry not found! (%s)\n", afp_pathname.c_str());
        return afpObjectNotFound;
    }

    if (!afp_impersonate_user(connection->get_user_token()))
    {
        return afpAccessDenied;
    }

    DBGWRITE(dbg_level_info, L"Deleting item at path: %s\n", afp_entry->sz_path.c_str());

    AFPERROR afp_error = AFP_OK;

    // We take 2 different approaches to deleting the object based on whether it's
    // a directory or a file.
    if (afp_entry->flags & flag_index_is_dir)
    {
        if (!RemoveDirectory(afp_entry->sz_path.c_str()))
        {
            DBGWRITE(dbg_level_info,
                     L"RemoveDirectory() failed on %s! (%d)\n",
                     afp_entry->sz_path.c_str(),
                     GetLastError()
            );

            afp_error = afp_map_win_error_to_afp(GetLastError(), afpParmErr);
        }
        else
        {
            volume->get_index()->remove_entry(afp_entry);
        }
    }
    else
    {
        if (!DeleteFile(afp_entry->sz_path.c_str()))
        {
            DBGWRITE(dbg_level_info,
                     L"DeleteFile failed on %s! (%d)\n",
                     afp_entry->sz_path.c_str(),
                     GetLastError()
            );

            afp_error = afp_map_win_error_to_afp(GetLastError(), afpParmErr);
        }
        else
        {
            volume->get_index()->remove_entry(afp_entry);
        }
    }

    afp_revert_to_self(connection->get_user_token());

    DBGWRITE(dbg_level_trace, L"FPDelete returning: %d\n", afp_error);

    return afp_error;
}


/*
 * FPCopyFile()
 *
 * Description:
 *		Copy a file from one location to another.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPCopyFile(
    IN afp_net_buffer& afp_request,
    IN afp_net_buffer& afp_reply,
    IN dsi::dsi_connection* connection
)
{
    UNREFERENCED_PARAMETER(afp_reply);

    DBGWRITE(dbg_level_trace, L"Enter...\n");

    afp_request.advance_pos_ptr(sizeof(INT16));

    const auto afp_src_vol_id = afp_request.pull_num<INT16>();
    const auto afp_src_dir_id = afp_request.pull_num<INT32>();
    const auto afp_dst_vol_id = afp_request.pull_num<INT16>();
    const auto afp_dst_dir_id = afp_request.pull_num<INT32>();

    // Get the pathname of the object we're copying.
    auto afp_path_type = afp_request.pull_num<INT8>();
    const auto afp_src_pathname = afp_request.pull_path(afp_path_type);
    if (afp_src_pathname.empty())
    {
        return afpParmErr;
    }

    // Get the destination pathname we're copying to
    afp_path_type = afp_request.pull_num<INT8>();
    const auto afp_dst_pathname = afp_request.pull_path(afp_path_type);

    // And now the [optional] new name for the file
    afp_path_type = afp_request.pull_num<INT8>();
    const auto afp_new_name = afp_request.pull_path(afp_path_type);

    const auto afp_session = connection->get_afp_session();
    const auto src_volume = afp_session->find_open_volume(afp_src_vol_id);
    const auto dst_volume = afp_session->find_open_volume(afp_dst_vol_id);
    if (src_volume == nullptr || dst_volume == nullptr)
    {
        return afpParmErr;
    }

    const auto afp_src_entry = src_volume->get_index()->find(afp_src_dir_id, afp_src_pathname);
    if (afp_src_entry == nullptr)
    {
        DBGWRITE(dbg_level_error, L"Source index entry not found!\n");
        return afpObjectNotFound;
    }

    // Make sure the path doesn't point to a directory, we only work with
    // files as per the spec.
    if (afp_src_entry->flags & flag_index_is_dir)
    {
        return afpObjectTypeErr;
    }

    const auto afp_dst_entry = dst_volume->get_index()->find(afp_dst_dir_id, afp_dst_pathname);
    if (afp_dst_entry == nullptr)
    {
        DBGWRITE(dbg_level_error, L"Destination directory index entry not found!\n");
        return afpObjectNotFound;
    }

    // Build the path to the new location. If we were supplied a new name,
    // then use it, otherwise use the old file name.
    std::filesystem::path new_win_path;

    if (!afp_new_name.empty())
    {
        new_win_path = afp_dst_entry->sz_path / afp_new_name;
    }
    else
    {
        new_win_path = afp_dst_entry->sz_path / afp_src_entry->sz_object_name;
    }

    DBGWRITE(dbg_level_trace, L"copying from %s\n", afp_src_entry->sz_path.c_str());
    DBGWRITE(dbg_level_trace, L"copying to %s\n", new_win_path.c_str());

    // Setup the correct impersonation for this API call. We have to
    // have the right credentials for copy file.
    if (!afp_impersonate_user(connection->get_user_token()))
    {
        return afpAccessDenied;
    }

    notify_suspend suspend(afp_session->find_open_volume(afp_dst_vol_id).get());

    const BOOL copy_succeeded = CopyFile(afp_src_entry->sz_path.c_str(), new_win_path.c_str(), true);

    afp_revert_to_self(connection->get_user_token());

    if (copy_succeeded)
    {
        dst_volume->get_index()->add_entry(
            afp_dst_entry->object_id,
            afp_src_entry->sz_object_name,
            new_win_path,
            false,
            &afp_src_entry->char_map,
            nullptr);
    }
    else
    {
        DBGWRITE(dbg_level_info, L"CopyFile() failed (%d) (%s)!\n", GetLastError(), new_win_path.c_str());
    }

    return copy_succeeded ? AFP_OK : afp_map_win_error_to_afp(GetLastError(), afpParmErr);
}


/*
 * FPMoveAndRename()
 *
 * Description:
 *		Move and optionally rename a file.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPMoveAndRename(
    IN afp_net_buffer& afp_request,
    IN afp_net_buffer& afp_reply,
    IN dsi::dsi_connection* connection
)
{
    UNREFERENCED_PARAMETER(afp_reply);

    WIN32_FILE_ATTRIBUTE_DATA win_attributes;

    DBGWRITE(dbg_level_trace, L"Enter\n");

    afp_request.advance_pos_ptr(sizeof(INT16));

    const auto afp_volume_id = afp_request.pull_num<INT16>();
    const auto afp_src_dir_id = afp_request.pull_num<INT32>();
    const auto afp_dst_dir_id = afp_request.pull_num<INT32>();
    auto afp_path_type = afp_request.pull_num<UINT8>();

    const auto volume = connection->get_afp_session()->find_open_volume(afp_volume_id);
    if (volume == nullptr)
    {
        return afpParmErr;
    }

    if (volume->is_read_only())
    {
        return afpVolLocked;
    }

    //*****************
    //Beging by getting the source file/dir entry.
    //*****************

    auto afp_pathname = afp_request.pull_path(afp_path_type);

    const auto afp_src_entry = volume->find(afp_src_dir_id, afp_pathname);
    if (afp_src_entry == nullptr)
    {
        DBGWRITE(dbg_level_error, L"Src Index entry not found!\n");
        return afpObjectNotFound;
    }

    if (!GetFileAttributesEx(afp_src_entry->sz_path.c_str(), GetFileExInfoStandard, &win_attributes))
    {
        DBGWRITE(dbg_level_error, L"Could not get attributes on src entry: %s\n", afp_src_entry->sz_path.c_str());
        return afpObjectNotFound;
    }

    const auto afp_attributes = win_to_afp_attributes(win_attributes.dwFileAttributes);
    if (afp_attributes & (kFileDeleteInhibit | kFileRenameInhibit))
    {
        DBGWRITE(dbg_level_info, L"Cannot rename, object locked!\n");
        return afpObjectLocked;
    }

    //*****************
    //Now get the destination directory we're moving to
    //*****************

    afp_path_type = afp_request.pull_num<INT8>();
    afp_pathname = afp_request.pull_path(afp_path_type);

    const auto afp_dst_entry = volume->find(afp_dst_dir_id, afp_pathname);
    if (afp_dst_entry == nullptr)
    {
        DBGWRITE(dbg_level_info, L"Dst dir Index entry not found!\n");
        return afpObjectNotFound;
    }

    char_map_data char_map;
    afp_path_type = afp_request.pull_num<INT8>();
    const auto afp_new_pathname = afp_request.pull_path(afp_path_type, &char_map);

    // Build the correct path to the newly moved and/or renamed object.
    std::filesystem::path win_dst_path;
    if (!afp_new_pathname.empty())
    {
        win_dst_path = afp_dst_entry->sz_path / afp_new_pathname;
        afp_src_entry->sz_object_name = afp_new_pathname;
    }
    else
    {
        win_dst_path = afp_dst_entry->sz_path / afp_src_entry->sz_object_name;
    }

    DBGWRITE(dbg_level_info, L"Renaming to new path = %s\n", win_dst_path.c_str());

    if (!afp_impersonate_user(connection->get_user_token()))
    {
        return afpAccessDenied;
    }

    AFPERROR afp_error = AFP_OK;

    notify_suspend suspend(volume.get());

    if (!MoveFile(afp_src_entry->sz_path.c_str(), win_dst_path.c_str()))
    {
        DBGWRITE(dbg_level_info, L"MoveFile() failed! (%d)\n", GetLastError());
        afp_error = afp_map_win_error_to_afp(GetLastError(), afpParmErr);
    }

    afp_revert_to_self(connection->get_user_token());

    if (AFP_SUCCESS(afp_error))
    {
        volume->update_entry(afp_src_entry, afp_dst_entry, L"", &char_map);
        volume->set_is_dirty();
    }

    return afp_error;
}


/*
 * FPRename()
 *
 * Description:
 *		Rename a file or directory.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPRename(
    IN afp_net_buffer& afp_request,
    IN afp_net_buffer& afp_reply,
    IN dsi::dsi_connection* connection
)
{
    UNREFERENCED_PARAMETER(afp_reply);

    DBGWRITE(dbg_level_trace, L"Enter\n");

    afp_request.advance_pos_ptr(sizeof(INT16));

    const auto afp_volume_id = afp_request.pull_num<INT16>();
    const auto afp_dir_id = afp_request.pull_num<INT32>();
    auto afp_path_type = afp_request.pull_num<UINT8>();

    const auto volume = connection->get_afp_session()->find_open_volume(afp_volume_id);
    if (volume == nullptr)
    {
        return afpParmErr;
    }

    if (volume->is_read_only())
    {
        return afpVolLocked;
    }

    const auto afp_pathname = afp_request.pull_path(afp_path_type);
    if (afp_pathname.empty())
    {
        return afpParmErr;
    }

    const auto afp_entry = volume->find(afp_dir_id, afp_pathname);
    if (afp_entry == nullptr)
    {
        DBGWRITE(dbg_level_info, L"Dst entry not found! (%s)\n", afp_pathname.c_str());
        return afpObjectNotFound;
    }

    WIN32_FILE_ATTRIBUTE_DATA win_attributes;
    if (!GetFileAttributesEx(afp_entry->sz_path.c_str(), GetFileExInfoStandard, &win_attributes))
    {
        DBGWRITE(dbg_level_info, L"Could not get attributes on src entry!\n");
        return afpObjectNotFound;
    }

    const auto afp_attributes = win_to_afp_attributes(win_attributes.dwFileAttributes);
    if (afp_attributes & kFileRenameInhibit)
    {
        DBGWRITE(dbg_level_info, L"Cannot rename, object locked!\n");
        return afpObjectLocked;
    }

    char_map_data char_map;
    afp_path_type = afp_request.pull_num<INT8>();
    const auto new_name = afp_request.pull_path(afp_path_type, &char_map);
    if (new_name.empty())
    {
        return afpParmErr;
    }

    const std::filesystem::path win_path = afp_entry->par_entry->sz_path / new_name;

    DBGWRITE(dbg_level_info, L"Renaming to new path = %s\n", win_path.c_str());

    if (!afp_impersonate_user(connection->get_user_token()))
    {
        return afpAccessDenied;
    }

    AFPERROR afp_error = AFP_OK;

    notify_suspend suspend(volume.get());

    if (!MoveFile(afp_entry->sz_path.c_str(), win_path.c_str()))
    {
        DBGWRITE(dbg_level_info, L"MoveFile() failed! (%d)\n", GetLastError());
        afp_error = afp_map_win_error_to_afp(GetLastError(), afpParmErr);
    }

    afp_revert_to_self(connection->get_user_token());

    if (AFP_SUCCESS(afp_error))
    {
        volume->update_entry(afp_entry, afp_entry->par_entry, new_name, &char_map);
        volume->set_is_dirty();
    }

    return afp_error;
}
