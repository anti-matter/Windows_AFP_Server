// ReSharper disable CppClangTidyClangDiagnosticGnuZeroVariadicMacroArguments
// ReSharper disable CppParameterMayBeConstPtrOrRef
#include "StdAfx.h"
#include "afpdirectory.h"
#include "afp_session.h"
#include "afp_volumes.h"
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

    afp_request.advance_pos_ptr(sizeof(INT16));

    const auto afp_volume_id = afp_request.pull_num<INT16>();
    const auto afp_file_id = afp_request.pull_num<UINT32>();
    auto afp_bitmap = afp_request.pull_num<UINT16>();

    // Get a pointer to the volume info structure for this volume.
    const auto afp_session = connection->get_afp_session();
    const std::shared_ptr<afp_volume> afp_volume_info = afp_session->find_open_volume(afp_volume_id);
    const std::filesystem::path path;
    const auto afp_entry = afp_volume_info->get_index()->find(afp_file_id,path);

    AFPERROR afp_error = AFP_OK;

    if (afp_entry != nullptr)
    {
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

        afp_error = AFPGetObjectParms(
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
    WCHAR afp_pathname[MAX_AFP_PATH];
    int8 afp_attributes = 0;
    afp_index* afp_entry = nullptr;

    DBGWRITE(dbg_level_all, L"Enter...\n");

    afp_request.advance_pos_ptr(sizeof(int16));
    const auto afp_volume_id = afp_request.pull_num<INT16>();
    const auto afp_dir_id = afp_request.pull_num<UINT32>();
    auto afp_file_bitmap = afp_request.pull_num<UINT16>();
    auto afp_dir_bitmap = afp_request.pull_num<UINT16>();
    const auto afp_path_type = afp_request.pull_num<INT8>();

    // The client must have previously opened the volume we are operating on.
    if (!connection->get_afp_session()->session_has_volume_open(afp_volume_id))
    {
        // The volume has not been opened, we need to return error.
        return afpParmErr;
    }

    if (AFP_FAILED(afp_request.get_pascal_string(afp_pathname, sizeof(afp_pathname), true, afp_path_type)))
    {
        return (afpParmErr);
    }

    AFPERROR afp_error = AFPIndexFindEntryWithVolIDAndPath(afp_volume_id, afp_dir_id, afp_pathname, &afp_entry);

    if (AFP_FAILED(afp_error))
    {
        DBGWRITE(dbg_level_all,
                 L"Entry not found in GetFileDirParms! (dirID = %d)(name = %s)\n",
                 afp_dir_id, afp_pathname);

        return (afpObjectNotFound);
    }

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

    // If both bitmaps are null, then we are to just return
    // the bitmaps and the attributes.
    if (afp_file_bitmap == kFPFileNone && afp_dir_bitmap == kFPDirNone)
    {
        return AFP_OK;
    }

    // We add a null byte when adding more parameters to separate the stuff.
    afp_reply.push_int8(0);

    afp_error = AFPGetObjectParms(
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
    WCHAR afp_pathname[MAX_AFP_PATH];
    int16 afp_act_count = 0;
    int8 afp_attributes = 0;
    int16 afp_objects_found = 0;
    int32 afp_start_index;
    int32 afp_max_reply_size;
    afp_index* afp_target_entry = nullptr;

    WIN32_FILE_ATTRIBUTE_DATA win_attributes;

    DBGWRITE(dbg_level_all, L"Enter...\n");

    const int8 afp_command = afp_request.pull_num<INT8>();
    afp_request.advance_pos_ptr(sizeof(INT8));

    const auto afp_vol_id = afp_request.pull_num<UINT16>();
    const auto afp_dir_id = afp_request.pull_num<UINT32>();
    auto afp_file_bitmap = afp_request.pull_num<UINT16>();
    auto afp_dir_bitmap = afp_request.pull_num<UINT16>();
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
        return (afpParmErr);
    }

    const auto afp_path_type = afp_request.pull_num<INT8>();

    const auto afp_session = connection->get_afp_session();
    if (!afp_session->session_has_volume_open(afp_vol_id))
    {
        DBGWRITE(dbg_level_info, L"Volume not opened!\n");
        return afpParmErr;
    }

    AFPERROR afp_error = afp_request.get_pascal_string(afp_pathname, sizeof(afp_pathname), true, afp_path_type);
    if (AFP_FAILED(afp_error))
    {
        return afp_error;
    }

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

    std::shared_ptr<afp_volume> afp_volume_info = afp_session->find_open_volume(afp_vol_id);

    // Retrieve the index entry of the directory pointed to by the AFP path.
    // We use it's dirID to determine which files we'll return in the enum.
    afp_error = AFPIndexFindEntryWithPath(afp_volume_info, afp_dir_id, afp_pathname, &afp_target_entry);
    if (AFP_FAILED(afp_error))
    {
        DBGWRITE(dbg_level_info, L"Failed to get index entry (vID = %d, dirID = %d, name = %s)\n",
                 afp_vol_id,
                 afp_dir_id,
                 afp_pathname
        );

        return afp_error;
    }

    // Create a temporary index array to hold all the offspring for the
    // target directory.
    std::vector<afp_index*>* afp_temp_index;

    // Now populate the temp array with the offspring in the target dir.
    afp_volume_info->get_index()->get_offspring(afp_target_entry->object_id, &afp_temp_index);

    // Go through the entire index and find entries that have the dirID as their parent.
    for (auto it = afp_temp_index->begin(); it < afp_temp_index->end(); ++it)
    {
        afp_index* afp_entry = *it;

#ifdef _DEBUG
        if ((afp_entry->par_id == afp_target_entry->object_id) && (GetFileAttributesEx(
            afp_entry->sz_path.c_str(), GetFileExInfoStandard, &win_attributes) == 0))
        {
            DBGWRITE(dbg_level_info, L"GetFileAttributesEx() failed (%d)\n", GetLastError());
            DBGWRITE(dbg_level_info, L"File = %s\n", afp_entry->sz_path.c_str());
        }
#endif

        // We don't include some files (like the desktop db) since they
        // are server specific and the macs shouldn't see or edit them.
        if (!lstrcmp(afp_entry->sz_object_name.c_str(), DESKTOP_FILE_NAME))
        {
            continue;
        }

        if (GetFileAttributesEx(afp_entry->sz_path.c_str(), GetFileExInfoStandard, &win_attributes))
        {
            char temp_buffer[1024] = {};
            afp_net_buffer parameter_buffer(temp_buffer, sizeof(temp_buffer));

            afp_objects_found++;
            if (afp_objects_found < afp_start_index)
            {
                continue;
            }

            DBGWRITE(dbg_level_all, L"Enumerating %s\n", afp_entry->sz_object_name.c_str());

            const bool afp_is_directory = (afp_entry->flags & flag_index_is_dir);

            if (afp_dir_bitmap == kFPDirNone && afp_is_directory ||
                afp_file_bitmap == kFPFileNone && !afp_is_directory)
            {
                continue;
            }

            parameter_buffer.rewind();
            afp_attributes = 0;

            afp_error = AFPGetObjectParms(
                parameter_buffer,
                (afp_is_directory) ? afp_dir_bitmap : afp_file_bitmap,
                afp_vol_id,
                afp_dir_id,
                connection,
                afp_entry,
                win_attributes
            );

            if (afp_is_directory)
            {
                afp_attributes |= static_cast<INT8>(kFileDirIsDir);
            }

            if (AFP_SUCCESS(afp_error))
            {
                UINT8* struct_len = nullptr;
                INT16* ext_struct_len = nullptr;

                // Check to make sure there is enough room in the buffer to add the
                // file/dir information.
                size_t size_required = parameter_buffer.get_data_length() + afp_reply.get_data_length();

                // There are items after the file dirs parms that are added to the buffer,
                // so we add a little extra to be safe.
                size_required += (4);

                // afp_max_reply_size is the maximum size our return buffer can be.
                if (size_required >= static_cast<size_t>(afp_max_reply_size))
                {
                    DBGWRITE(dbg_level_all, L"Breaking out, size required too big!\n");
                    break;
                }

                // Make sure we didn't get bigger then our reply buffer can handle.
                if (size_required > afp_reply.get_buffer_size())
                {
                    // This should never happen (yeah right). In case it does, just
                    // bail from here.
                    DBGWRITE(dbg_level_all, L"Breaking out, reply buffer too small!\n");
                    break;
                }

                afp_act_count++;

                // The first byte (or word for AFP3.x) contains the struct len.
                if (afp_command > afpEnumerate)
                {
                    ext_struct_len = reinterpret_cast<INT16*>(afp_reply.get_current_pos_ptr());
                    afp_reply.advance_pos_ptr(sizeof(int16));
                }
                else
                {
                    struct_len = reinterpret_cast<UINT8*>(afp_reply.get_current_pos_ptr());
                    afp_reply.advance_pos_ptr(sizeof(int8));
                }

                afp_reply.push_int8(afp_attributes);

                if (afp_command > afpEnumerate)
                {
                    afp_reply.push_int8(0);
                }

                afp_reply.push_raw_data(parameter_buffer.get_buffer(),
                                        static_cast<int16>(parameter_buffer.get_data_length()));

                // Set the structure length byte in the buffer. The extra bytes are
                // for the first parms in the structure (structLen, attributes and padding).
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
                    *ext_struct_len = static_cast<INT16>(htons(len));
                }
                else
                {
                    *struct_len = static_cast<INT8>(len);
                }
            }
            else
            {
                DBGWRITE(dbg_level_info, L"Error finding parms for object\n");
            }

            // If we've gotten as much data as the client requested, then
            // exit this beast.
            if (afp_act_count >= afp_req_count || afp_reply.get_data_length() >= afp_max_reply_size)
            {
                break;
            }
        }
    }

    // Set the actual number of objects found and in the buffer.
    *afp_act_count_spot = static_cast<INT16>(htons(afp_act_count));

    DBGWRITE(dbg_level_all, L"afpActCount = %d\n", afp_act_count);
    DBGWRITE(dbg_level_all, L"Returning: %d\n", (afp_act_count == 0) ? afpObjectNotFound : AFP_OK);

    delete afp_temp_index;

    return ((afp_act_count == 0) ? afpObjectNotFound : AFP_OK);
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

    WCHAR afp_pathname[MAX_AFP_PATH];
    afp_index* afp_entry = nullptr;

    WIN32_FILE_ATTRIBUTE_DATA win_attributes;

    DBGWRITE(dbg_level_all, L"Enter...\n");

    const auto afp_command = afp_request.pull_num<INT8>();
    afp_request.advance_pos_ptr(sizeof(int8));

    const auto afp_vol_id = afp_request.pull_num<UINT16>();
    const auto afp_dir_id = afp_request.pull_num<UINT32>();
    const auto afp_bitmap = afp_request.pull_num<UINT16>();
    const auto afp_path_type = afp_request.pull_num<INT8>();

    if (!connection->get_afp_session()->session_has_volume_open(afp_vol_id))
    {
        return afpParmErr;
    }

    AFPERROR afp_error = AFPVolumeWriteAccessCheck(afp_vol_id);
    if (AFP_FAILED(afp_error))
    {
        return afp_error;
    }

    afp_error = afp_request.get_pascal_string(afp_pathname, sizeof(afp_pathname), true, afp_path_type);
    if (AFP_FAILED(afp_error))
    {
        return afp_error;
    }

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
            DBGWRITE(dbg_level_all, L"Bad bitmap!\n");
            return afpBitmapErr;
        }
        break;

    case afpSetDirParms:
        if (afp_bitmap & (kFPParentID | kFPLongName | kFPShortName |
            kFPDirOffCount | kFPDirID | kFPDirUnicodeName))
        {
            DBGWRITE(dbg_level_all, L"[afp:FPSetDirParms]Bad bitmap!\n");
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

    afp_error = AFPIndexFindEntryWithVolIDAndPath(afp_vol_id, afp_dir_id, afp_pathname, &afp_entry);
    if (AFP_FAILED(afp_error))
    {
        DBGWRITE(dbg_level_info,
                 L"Index entry not found in FPSetFileDirParms! (dirID = %d  path = %s)\n",
                 afp_dir_id, afp_pathname);

        return afp_error;
    }

    if (!GetFileAttributesEx(afp_entry->sz_path.c_str(), GetFileExInfoStandard, &win_attributes))
    {
        DBGWRITE(dbg_level_info, L"GetFileAttributesEx() failed! (%d)\n", GetLastError());
        return afpObjectNotFound;
    }

    afp_error = AFPSetFileDirParms(
        afp_bitmap,
        afp_entry,
        afp_request.get_current_pos_ptr(),
        win_attributes.dwFileAttributes
    );

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
    AFPCHARMAPARRAY char_map;
    WCHAR afp_pathname[MAX_AFP_PATH];
    WCHAR win_path[MAX_PATH];
    afp_index* afp_entry = nullptr;

    DBGWRITE(dbg_level_all, L"FPCreateDir...\n");

    afp_request.advance_pos_ptr(sizeof(int16));

    const auto afp_volume_id = afp_request.pull_num<UINT16>();
    auto afp_dir_id = afp_request.pull_num<UINT32>();
    const auto afp_path_type = afp_request.pull_num<INT8>();
    const auto afp_session = connection->get_afp_session();

    std::shared_ptr<afp_volume> afp_volume = afp_session->find_open_volume(afp_volume_id);
    if (afp_volume == nullptr)
    {
        DBGWRITE(dbg_level_info, L"Bad volume ID in FPCreateDir!\n");
        return afpParmErr;
    }

    if (!afp_session->session_has_volume_open(afp_volume_id))
    {
        // The volume has not been opened, we need to return error.
        return afpParmErr;
    }

    // Make sure the volume is not marked read-only.
    AFPERROR afp_error = AFPVolumeWriteAccessCheck(afp_volume_id);
    if (AFP_FAILED(afp_error))
    {
        return afp_error;
    }

    afp_error = afp_request.get_pascal_string(afp_pathname, sizeof(afp_pathname), true, afp_path_type, char_map);
    if (AFP_FAILED(afp_error))
    {
        DBGWRITE(dbg_level_info, L"GetString failed for path!\n");
        return afp_error;
    }

    // Pathname cannot be null since it contains the new dir name.
    if (lstrlen(afp_pathname) == 0)
    {
        DBGWRITE(dbg_level_info, L"No pathname in FPCreateDir!\n");
        return afpParmErr;
    }

    // Get the directory entry for the directory where we are creating
    // the new one.
    afp_error = AFPIndexFindEntryWithPath(afp_volume, afp_dir_id, nullptr, &afp_entry);
    if (AFP_FAILED(afp_error))
    {
        DBGWRITE(dbg_level_info, L"Path not found in FPCreateDir!\n");
        return afp_error;
    }

    // Build the path to the new directory using the parent directory's entry.
    // ReSharper disable once CppFunctionResultShouldBeUsed
    StringCbPrintf(win_path, sizeof(win_path), TEXT("%s\\%s"), afp_entry->sz_path.c_str(), afp_pathname);

    if (GetFileAttributes(win_path) != AFP_MAX_DWORD)
    {
        return afpObjectExists;
    }

    // Make sure only those who are allowed can create directories.
    if (!afp_impersonate_user(connection->get_user_token()))
    {
        return afpAccessDenied;
    }

    afp_error = CreateDirectory(win_path, nullptr)
	                ? AFP_OK
	                : afp_map_win_error_to_afp(GetLastError(), afpMiscErr);

    afp_revert_to_self(connection->get_user_token());

    if (AFP_SUCCESS(afp_error))
    {
        afp_dir_id = afp_volume->get_index()->wait_for_entry(win_path);
        afp_volume->set_is_dirty();

        // Now build the reply block to send_ back to the client.
        afp_reply.push_num(afp_dir_id);
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

    WCHAR afp_pathname[MAX_AFP_PATH];
    afp_index* afp_entry = nullptr;

    DBGWRITE(dbg_level_all, L"Enter\n");

    afp_request.advance_pos_ptr(sizeof(int16));

    const auto afp_volume_id = afp_request.pull_num<UINT16>();
    const auto afp_dir_id = afp_request.pull_num<UINT32>();
    const auto afp_path_type = afp_request.pull_num<INT8>();

    if (!connection->get_afp_session()->session_has_volume_open(afp_volume_id))
    {
        return afpParmErr;
    }

    AFPERROR afp_error = AFPVolumeWriteAccessCheck(afp_volume_id);
    if (AFP_FAILED(afp_error))
    {
        return afp_error;
    }

    afp_error = afp_request.get_pascal_string(afp_pathname, sizeof(afp_pathname), true, afp_path_type);
    if (AFP_FAILED(afp_error))
    {
        return afp_error;
    }

    if (lstrlen(afp_pathname) == 0)
    {
        DBGWRITE(dbg_level_info, L"Null pathname!\n");
        return afpParmErr;
    }

    std::shared_ptr<afp_volume> afp_volume_info = connection->get_afp_session()->find_open_volume(afp_volume_id);
    if (afp_volume_info == nullptr)
    {
        DBGWRITE(dbg_level_info, L"Bad volume ID! (%d)\n", afp_volume_id);
        return afpParmErr;
    }

    afp_error = AFPIndexFindEntryWithPath(afp_volume_info, afp_dir_id, afp_pathname, &afp_entry);
    if (AFP_FAILED(afp_error))
    {
        DBGWRITE(dbg_level_info, L"Index entry not found! (%s)\n", afp_pathname);
        return afp_error;
    }

    if (!afp_impersonate_user(connection->get_user_token()))
    {
        return (afpAccessDenied);
    }

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
    }

    afp_revert_to_self(connection->get_user_token());

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

    WCHAR afp_src_pathname[MAX_AFP_PATH];
    WCHAR afp_dst_pathname[MAX_AFP_PATH];
    WCHAR afp_new_name[MAX_AFP_NAME];
    WCHAR new_win_path[MAX_PATH];
    AFPCHARMAPARRAY dest_char_map;
    AFPCHARMAPARRAY new_char_map;
    afp_index* afp_src_entry = nullptr;
    afp_index* afp_dst_entry = nullptr;

    DBGWRITE(dbg_level_all, L"Enter...\n");

    afp_request.advance_pos_ptr(sizeof(int16));

    const auto afp_src_vol_id = afp_request.pull_num<UINT16>();
    const auto afp_src_dir_id = afp_request.pull_num<UINT32>();
    const auto afp_dst_vol_id = afp_request.pull_num<UINT16>();
    const auto afp_dst_dir_id = afp_request.pull_num<UINT32>();

    // Get the pathname of the object we're copying.
    auto afp_path_type = afp_request.pull_num<INT8>();
    AFPERROR afp_error = afp_request.get_pascal_string(
        afp_src_pathname,
        sizeof(afp_src_pathname),
        true,
        afp_path_type
    );

    if (AFP_FAILED(afp_error))
    {
        return afp_error;
    }

    // Get the destination pathname we're copying to
    afp_path_type = afp_request.pull_num<INT8>();
    afp_error = afp_request.get_pascal_string(
        afp_dst_pathname,
        sizeof(afp_dst_pathname),
        true,
        afp_path_type,
        dest_char_map
    );

    if (AFP_FAILED(afp_error))
    {
        return afp_error;
    }

    // And now the [optional] new name for the file
    afp_path_type = afp_request.pull_num<INT8>();
    afp_error = afp_request.get_pascal_string(
        afp_new_name,
        sizeof(afp_new_name),
        true,
        afp_path_type,
        new_char_map
    );

    if (AFP_FAILED(afp_error))
    {
        return afp_error;
    }

    const auto afp_session = connection->get_afp_session();
    if (!afp_session->session_has_volume_open(afp_src_vol_id) || !afp_session->session_has_volume_open(afp_dst_vol_id))
    {
        return afpParmErr;
    }

    if (lstrlen(afp_src_pathname) == 0)
    {
        DBGWRITE(dbg_level_info, L"NULL source pathname provided\n");
        return afpParmErr;
    }

    afp_error = AFPIndexFindEntryWithVolIDAndPath(afp_src_vol_id, afp_src_dir_id, afp_src_pathname, &afp_src_entry);
    if (AFP_FAILED(afp_error))
    {
        DBGWRITE(dbg_level_info, L"Source index entry not found!\n");
        return afp_error;
    }

    // Make sure the path doesn't point to a directory, we only work with
    // files as per the spec.
    if (afp_src_entry->flags & flag_index_is_dir)
    {
        return afpObjectTypeErr;
    }

    // Get the index entry for the destination directory.
    afp_error = AFPIndexFindEntryWithVolIDAndPath(afp_dst_vol_id, afp_dst_dir_id, afp_dst_pathname, &afp_dst_entry);
    if (AFP_FAILED(afp_error))
    {
        DBGWRITE(dbg_level_info, L"Destination directory index entry not found!\n");
        return afp_error;
    }

    // Build the path to the new location. If we were supplied a new name,
    // then use it, otherwise use the old file name.
    if (lstrlen(afp_new_name) > 0)
    {
        // ReSharper disable once CppFunctionResultShouldBeUsed
        StringCbPrintf(new_win_path, sizeof(new_win_path), TEXT("%s\\%s"), afp_dst_entry->sz_path.c_str(), afp_new_name);
    }
    else
    {
        // ReSharper disable once CppFunctionResultShouldBeUsed
        StringCbPrintf(new_win_path, sizeof(new_win_path), TEXT("%s\\%s"), afp_dst_entry->sz_path.c_str(),
                       afp_src_entry->sz_object_name.c_str());
    }

    DBGWRITE(dbg_level_all, L"copying from %s\n", afp_src_entry->sz_path.c_str());
    DBGWRITE(dbg_level_all, L"copying to %s\n", new_win_path);

    // Setup the correct impersonation for this API call. We have to
    // have the right credentials for copy file.
    if (!afp_impersonate_user(connection->get_user_token()))
    {
        return afpAccessDenied;
    }

    const BOOL copy_succeeded = CopyFile(afp_src_entry->sz_path.c_str(), new_win_path, true);
    if (!copy_succeeded)
    {
        DBGWRITE(dbg_level_info, L"CopyFile() failed (%d) (%s)!\n", GetLastError(), new_win_path);
        afp_error = afp_map_win_error_to_afp(GetLastError(), afpParmErr);
    }

    afp_revert_to_self(connection->get_user_token());

    return afp_error;
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

    WCHAR afp_pathname[MAX_AFP_PATH];
    WCHAR afp_new_pathname[MAX_AFP_NAME];
    //WCHAR win_dst_path[MAX_PATH];
    AFPCHARMAP dest_char_map[MAX_DIR_NAME];
    AFPCHARMAP new_char_map[MAX_DIR_NAME];
    afp_index* afp_src_entry;
    afp_index* afp_dst_entry;

    WIN32_FILE_ATTRIBUTE_DATA win_attributes;

    DBGWRITE(dbg_level_all, L"Enter\n");

    afp_request.advance_pos_ptr(sizeof(int16));

    const auto afp_volume_id = afp_request.pull_num<UINT16>();
    const auto afp_src_dir_id = afp_request.pull_num<UINT32>();
    const auto afp_dst_dir_id = afp_request.pull_num<UINT32>();
    auto afp_path_type = afp_request.pull_num<INT8>();

    if (!connection->get_afp_session()->session_has_volume_open(afp_volume_id))
    {
        return afpParmErr;
    }

    AFPERROR afp_error = AFPVolumeWriteAccessCheck(afp_volume_id);
    if (AFP_FAILED(afp_error))
    {
        return afp_error;
    }

    //*****************
    //Beging by getting the source file/dir entry.
    //*****************

    afp_error = afp_request.get_pascal_string(afp_pathname, sizeof(afp_pathname), true, afp_path_type);
    if (AFP_FAILED(afp_error))
    {
        return afp_error;
    }

    afp_error = AFPIndexFindEntryWithVolIDAndPath(afp_volume_id, afp_src_dir_id, afp_pathname, &afp_src_entry);
    if (AFP_FAILED(afp_error))
    {
        DBGWRITE(dbg_level_info, L"Src Index entry not found!\n");
        return afp_error;
    }

    if (!GetFileAttributesEx(afp_src_entry->sz_path.c_str(), GetFileExInfoStandard, &win_attributes))
    {
        DBGWRITE(dbg_level_info, L"Could not get attributes on src entry!\n");
        return afpObjectNotFound;
    }

    const auto afp_attributes = AFPWinToAFPAttributes(win_attributes.dwFileAttributes, nullptr);
    if (afp_attributes & (kFileDeleteInhibit | kFileRenameInhibit))
    {
        DBGWRITE(dbg_level_info, L"Cannot rename, object locked!\n");
        return afpObjectLocked;
    }

    //*****************
    //Now get the destination directory we're moving to
    //*****************

    afp_path_type = afp_request.pull_num<INT8>();
    afp_error = afp_request.get_pascal_string(afp_pathname, sizeof(afp_pathname), true, afp_path_type, dest_char_map);
    if (AFP_FAILED(afp_error))
    {
        return afp_error;
    }

    afp_error = AFPIndexFindEntryWithVolIDAndPath(afp_volume_id, afp_dst_dir_id, afp_pathname, &afp_dst_entry);
    if (AFP_FAILED(afp_error))
    {
        DBGWRITE(dbg_level_info, L"Dst dir Index entry not found!\n");
        return afp_error;
    }

    afp_path_type = afp_request.pull_num<INT8>();
    afp_error = afp_request.get_pascal_string(afp_new_pathname, sizeof(afp_new_pathname), true, afp_path_type, new_char_map);
    if (AFP_FAILED(afp_error))
    {
        return afp_error;
    }

    // Build the correct path to the newly moved and/or renamed object.
    std::filesystem::path win_dst_path;
    if (lstrlen(afp_new_pathname) > 0)
    {
        win_dst_path = afp_dst_entry->sz_path / afp_new_pathname;
        afp_src_entry->sz_object_name = afp_new_pathname;
    }
    else
    {
        win_dst_path = afp_dst_entry->sz_path / afp_src_entry->sz_object_name;
    }

    std::shared_ptr<afp_volume> volume_info = afp_volumes::current()->find(afp_volume_id);

    DBGWRITE(dbg_level_info, L"Renaming to new path = %s\n", win_dst_path.c_str());

    if (!afp_impersonate_user(connection->get_user_token()))
    {
        return afpAccessDenied;
    }

    if (!MoveFile(afp_src_entry->sz_path.c_str(), win_dst_path.c_str()))
    {
        DBGWRITE(dbg_level_info, L"MoveFile() failed! (%d)\n", GetLastError());

        afp_error = afp_map_win_error_to_afp(GetLastError(), afpParmErr);
    }

    afp_revert_to_self(connection->get_user_token());

    volume_info->set_is_dirty();

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

    WCHAR afp_pathname[MAX_AFP_PATH];
    WCHAR win_path[MAX_PATH];
    AFPCHARMAP new_char_map[MAX_DIR_NAME];
    afp_index* afp_entry = nullptr;

    DBGWRITE(dbg_level_all, L"[afp:FPRename]Enter\n");

    afp_request.advance_pos_ptr(sizeof(int16));

    const auto afp_volume_id = afp_request.pull_num<UINT16>();
    const auto afp_dir_id = afp_request.pull_num<UINT32>();
    auto afp_path_type = afp_request.pull_num<INT8>();

    if (!connection->get_afp_session()->session_has_volume_open(afp_volume_id))
    {
        return afpParmErr;
    }

    AFPERROR afp_error = AFPVolumeWriteAccessCheck(afp_volume_id);
    if (AFP_FAILED(afp_error))
    {
        return afp_error;
    }

    afp_error = afp_request.get_pascal_string(afp_pathname, sizeof(afp_pathname), true, afp_path_type);
    if (AFP_FAILED(afp_error))
    {
        return afp_error;
    }

    afp_error = AFPIndexFindEntryWithVolIDAndPath(afp_volume_id, afp_dir_id, afp_pathname, &afp_entry);
    if (AFP_FAILED(afp_error))
    {
        DBGWRITE(dbg_level_info, L"[afp:FPRename]Dst entry not found! (%s)\n", afp_pathname);
        return afp_error;
    }

    WIN32_FILE_ATTRIBUTE_DATA win_attributes;
    if (!GetFileAttributesEx(afp_entry->sz_path.c_str(), GetFileExInfoStandard, &win_attributes))
    {
        DBGWRITE(dbg_level_info, L"Could not get attributes on src entry!\n");
        return afpObjectNotFound;
    }

    const auto afp_attributes = AFPWinToAFPAttributes(win_attributes.dwFileAttributes, nullptr);
    if (afp_attributes & (kFileRenameInhibit))
    {
        DBGWRITE(dbg_level_info, L"Cannot rename, object locked!\n");
        return afpObjectLocked;
    }

    afp_path_type = afp_request.pull_num<INT8>();
    afp_error = afp_request.get_pascal_string(afp_pathname, sizeof(afp_pathname), true, afp_path_type, new_char_map);
    if (AFP_FAILED(afp_error) || (lstrlen(afp_pathname) == 0))
    {
        return afp_error;
    }

    // ReSharper disable once CppFunctionResultShouldBeUsed
    StringCbPrintf(win_path, sizeof(win_path), TEXT("%s\\%s"), static_cast<afp_index*>(afp_entry->par_entry)->sz_path.c_str(),
                   afp_pathname);

    DBGWRITE(dbg_level_info, L"Renaming to new path = %s\n", win_path);

    const std::shared_ptr<afp_volume> volumeInfo = afp_volumes::current()->find(afp_volume_id);

    if (!afp_impersonate_user(connection->get_user_token()))
    {
        return afpAccessDenied;
    }

    if (!MoveFile(afp_entry->sz_path.c_str(), win_path))
    {
        DBGWRITE(dbg_level_info, L"MoveFile() failed! (%d)\n", GetLastError());
        afp_error = afp_map_win_error_to_afp(GetLastError(), afpParmErr);
    }

    afp_revert_to_self(connection->get_user_token());

    volumeInfo->set_is_dirty();

    return afp_error;
}
