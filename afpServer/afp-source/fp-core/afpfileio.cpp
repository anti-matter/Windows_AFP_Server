// ReSharper disable CppClangTidyClangDiagnosticGnuZeroVariadicMacroArguments
// ReSharper disable CppParameterMayBeConstPtrOrRef
#include "StdAfx.h"
#include "afpfileio.h"
#include "afp_open_file.h"
#include "change_notify.h"
#include "fdparms.h"
#include "fpinfo.h"
#include "windows_security.h"

/*
 * FPFlushFork()
 *
 * Description:
 *		Flushes an open file fork.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPFlushFork(
    IN afp_net_buffer& afp_request,
    IN afp_net_buffer& afp_reply,
    IN dsi::dsi_connection* connection
)
{
    UNREFERENCED_PARAMETER(afp_request);
    UNREFERENCED_PARAMETER(afp_reply);
    UNREFERENCED_PARAMETER(connection);

    return (AFP_OK);
}


/*
 * FPCloseFork()
 *
 * Description:
 *		Closes the data or resource fork from reading/writing.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPCloseFork(
    IN afp_net_buffer& afp_request,
    IN afp_net_buffer& afp_reply,
    IN dsi::dsi_connection* connection
)
{
    UNREFERENCED_PARAMETER(afp_reply);

    afp_session* afp_afp_session = connection->get_afp_session();

    // Parameters start after first word (afp cmd and 0 byte)
    afp_request.advance_pos_ptr(sizeof(UINT16));

    // Extract the parameter and make the call.
    const auto afp_ref_num = afp_request.pull_num<INT16>();
    const AFPERROR afp_error = afp_afp_session->close_file(afp_ref_num);

    DBGWRITE(dbg_level_info, L"Closed fork: %d\n", afp_ref_num);

    return afp_error;
}


/*
 * FPOpenFork()
 *
 * Description:
 *		Opens the data or resource fork for reading/writing.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPOpenFork(
    IN afp_net_buffer& afp_request,
    IN afp_net_buffer& afp_reply,
    IN dsi::dsi_connection* connection
)
{
    afp_session* afp_afp_session = connection->get_afp_session();

    WIN32_FILE_ATTRIBUTE_DATA win_attributes;

    afp_request.advance_pos_ptr(sizeof(UINT8));

    const INT8 afp_fork = (afp_request.pull_num<INT8>() & kResourceForkBit) ? kRsrcFork : kDataFork;
    const auto afp_volume_id = afp_request.pull_num<INT16>();
    const auto afp_dir_id = afp_request.pull_num<INT32>();
    auto afp_bitmap = afp_request.pull_num<UINT16>();
    const auto afp_mode = afp_request.pull_num<INT16>();
    const INT8 afp_path_type = afp_request.pull_num<INT8>();

    const auto volume = afp_afp_session->find_open_volume(afp_volume_id);
    if (volume == nullptr)
    {
        // The volume has not been opened, we need to return error.
        return afpParmErr;
    }

    const auto afp_pathname = afp_request.pull_path(afp_path_type);
    if (afp_pathname.empty())
    {
        return afpParmErr;
    }

    // Get the index entry pertaining to the file we're opening.
    const auto afp_entry = volume->find(afp_dir_id, afp_pathname);
    if (afp_entry == nullptr)
    {
        DBGWRITE(dbg_level_info, L"Index entry not found! (%s)\n", afp_pathname.c_str());
        return afpObjectNotFound;
    }

    if (!GetFileAttributesEx(afp_entry->sz_path.c_str(), GetFileExInfoStandard, &win_attributes))
    {
        DBGWRITE(dbg_level_info, L"GetFileAttributesEx() failed! (%d)\n", GetLastError());
        DBGWRITE(dbg_level_info, L"path = %s\n", afp_entry->sz_path.c_str());

        return afpObjectNotFound;
    }

    if (afp_afp_session->get_num_files_open() > MAX_AFP_FILES_OPEN)
    {
        DBGWRITE(dbg_level_info, L"****Can't open file, too many open already! (%d)\n",
                 afp_afp_session->get_num_files_open());
        return afpTooManyFilesOpen;
    }

    afp_open_file* opened_file = afp_session::open_file(
        afp_afp_session,
        afp_volume_id,
        afp_dir_id,
        afp_mode,
        afp_fork,
        afp_entry,
        afp_entry->sz_path.c_str()
    );

    AFPERROR afp_error = opened_file == nullptr ? afpMiscErr : AFP_OK;
    if (AFP_SUCCESS(afp_error))
    {
        if (!impersonate_user(connection->get_user_token()))
        {
            return afpAccessDenied;
        }

        afp_error = opened_file->afp_open_file_fork();

        revert_to_self(connection->get_user_token());

        // If we successfully open the file, then return whatever object parms
        // the caller has asked for.
        if (AFP_SUCCESS(afp_error))
        {
            // We need to return the file ref num to the caller.
            const auto afp_new_ref_num = opened_file->get_file_ref_num();

            // We don't support the following bitmaps, so we clear them.
            if (connection->get_afp_version() < afpVersion30)
            {
                if (afp_bitmap & kFPProDos) afp_bitmap &= ~kFPProDos;
                if (afp_bitmap & kFPShortName) afp_bitmap &= ~kFPShortName;
            }

            // Add in the bitmap and new refnum id.
            afp_reply.push_num(afp_bitmap);
            afp_reply.push_num(afp_new_ref_num);

            afp_error = afp_get_object_parms(
                afp_reply,
                afp_bitmap,
                afp_volume_id,
                afp_dir_id,
                connection,
                afp_entry,
                win_attributes
            );
        }
    }

    // If we have an error opening the file, make sure we dispose
    // of the file object or we'll leak memory as well as leak
    // open file counts.
    if (AFP_FAILED(afp_error))
    {
        afp_afp_session->close_file(opened_file->get_file_ref_num());
    }

    return afp_error;
}


/*
 * FPSetForkParms()
 *
 * Description:
 *		Not really a supported function. But, we do some error checking
 *		while we're here.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPSetForkParms(
    IN afp_net_buffer& afp_request,
    IN afp_net_buffer& afp_reply,
    IN dsi::dsi_connection* connection
)
{
    UNREFERENCED_PARAMETER(afp_reply);

    afp_session* afp_afp_session = connection->get_afp_session();

    DBGWRITE(dbg_level_trace, L"Enter\n");

    // The first byte contains the afp command.
    afp_request.advance_pos_ptr(sizeof(INT16));

    const auto afp_fork_ref = afp_request.pull_num<INT16>();
    const auto afp_bitmap = afp_request.pull_num<UINT16>();

    // Fork length bytes
    afp_request.advance_pos_ptr(sizeof(UINT32));

    const afp_open_file* open_file = afp_afp_session->find_open_file(afp_fork_ref);
    const AFPERROR afp_error = open_file == nullptr ? afpParmErr : AFP_OK;

    if (AFP_SUCCESS(afp_error))
    {
        // Make sure the volume is not marked read-only.
        if (open_file->is_volume_read_only())
        {
            return afpVolLocked;
        }

        if ((afp_bitmap & kFPDFLen) || (afp_bitmap & kFPExtDataForkLen))
        {
            if (open_file->get_fork() != kDataFork)
            {
                DBGWRITE(dbg_level_trace, L"Data fork is not open!\n");
                return afpBitmapErr;
            }
        }

        if (afp_bitmap & kFPRFLen || afp_bitmap & kFPExtRsrcForkLen)
        {
            if (open_file->get_fork() != kRsrcFork)
            {
                DBGWRITE(dbg_level_error, L"Resource fork is not open! (%x)\n");
                return afpBitmapErr;
            }
        }
    }
    else
    {
        DBGWRITE(dbg_level_trace, L"Fork item not found!\n");
    }

    return afp_error;
}


/*
 * FPGetForkParms()
 *
 * Description:
 *		Retrieves parameters for a file referenced by the open fork refnum.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPGetForkParms(
    IN afp_net_buffer& afp_request,
    IN afp_net_buffer& afp_reply,
    IN dsi::dsi_connection* connection
)
{
    afp_session* afp_session = connection->get_afp_session();

    WIN32_FILE_ATTRIBUTE_DATA win_attributes;

    DBGWRITE(dbg_level_trace, L"Enter\n");

    // The first byte contains the afp command.
    afp_request.advance_pos_ptr(sizeof(INT16));

    // Get the parameters for the file we're opening.
    const auto afp_fork_ref = afp_request.pull_num<INT16>();
    auto afp_bitmap = afp_request.pull_num<UINT16>();

    afp_open_file* open_file = afp_session->find_open_file(afp_fork_ref);
    AFPERROR afp_error = (open_file == nullptr) ? afpParmErr : AFP_OK;

    if (AFP_SUCCESS(afp_error))
    {
        // We don't support the following bitmaps, so we clear them.
        if (connection->get_afp_version() < afpVersion30)
        {
            if (afp_bitmap & kFPProDos) afp_bitmap &= ~kFPProDos;
            if (afp_bitmap & kFPShortName) afp_bitmap &= ~kFPShortName;
        }

        // Make sure the caller is asking for the length of the fork
        // that is actually opened.
        if (afp_bitmap & kFPDFLen && open_file->get_fork() == kRsrcFork ||
            afp_bitmap & kFPRFLen && open_file->get_fork() == kDataFork)
        {
            DBGWRITE(dbg_level_info, L"Attempt to get length of wrong fork!\n");
            return afpBitmapErr;
        }

        // Add in the bitmap
        afp_reply.push_num(afp_bitmap);

        // We need this to properly set the attributes.
        GetFileAttributesEx(open_file->get_win_path(), GetFileExInfoStandard, &win_attributes);

        // Now that we've opened the file, get the requested parameters.
        afp_error = afp_get_object_parms(
            afp_reply,
            afp_bitmap,
            open_file->get_vol_id(),
            open_file->get_dir_id(),
            connection,
            open_file->get_entry(),
            win_attributes
        );
    }

    DBGWRITE(dbg_level_trace, L"Returning %d\n", afp_error);

    return afp_error;
}


/*
 * FPCreateFile()
 *
 * Description:
 *		Create a new file fork.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPCreateFile(
    IN afp_net_buffer& afp_request,
    IN afp_net_buffer& afp_reply,
    IN dsi::dsi_connection* connection
)
{
    UNREFERENCED_PARAMETER(afp_reply);
    DBGWRITE(dbg_level_trace, L"Enter\n");

    // First byte is padding.
    afp_request.advance_pos_ptr(sizeof(UINT8));

    const auto create_flag = afp_request.pull_num<UINT8>();
    const auto volume_id = afp_request.pull_num<INT16>();
    const auto dir_id = afp_request.pull_num<INT32>();
    const auto path_type = afp_request.pull_num<UINT8>();
    const auto session = connection->get_afp_session();

    // The client must have previously opened the volume we are creating
    // the file on.
    const auto volume = session->find_open_volume(volume_id);
    if (volume == nullptr)
    {
        // The volume has not been opened, we need to return error.
        return afpParmErr;
    }

    if (volume->is_read_only())
    {
        return afpVolLocked;
    }

    char_map_data char_map;
    const auto pathname = afp_request.pull_path(path_type, &char_map);
    if (pathname.empty())
    {
        return afpParmErr;
    }

    const auto afp_entry = volume->find(dir_id);
    if (afp_entry == nullptr)
    {
        // We couldn't find the requested object.
        DBGWRITE(dbg_level_error, L"Path not found in FPCreatFile!\n");
        return afpObjectNotFound;
    }

    const std::filesystem::path win_path = afp_entry->sz_path / pathname;

    if (!impersonate_user(connection->get_user_token()))
    {
        return afpAccessDenied;
    }

    notify_suspend suspend(volume.get());

    const auto h_file = CreateFile(
        win_path.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        (create_flag & kHardCreate) ? CREATE_ALWAYS : CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    revert_to_self(connection->get_user_token());

    if (h_file == INVALID_HANDLE_VALUE)
    {
        DBGWRITE(dbg_level_error,
                 L"CreateFile() for data fork failed! (%d) (%s)\n",
                 GetLastError(),
                 win_path.c_str());

        return afp_map_win_error_to_afp(GetLastError(), afpParmErr);
    }

    CloseHandle(h_file);

    const auto new_entry = volume->get_index()->add_entry(
        dir_id,
        pathname,
        win_path,
        false,
        &char_map,
        nullptr);

    if (new_entry != nullptr)
    {
        create_finder_info_stream(new_entry);
        afp_check_for_and_create_rsrc_stream(new_entry);
    }

    volume->set_is_dirty();

    return AFP_OK;
}


/*
 * FPByteRangeLock()
 *
 * Description:
 *		Lock a range for exclusive use.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPByteRangeLock(
    IN afp_net_buffer& afp_request,
    IN afp_net_buffer& afp_reply,
    IN dsi::dsi_connection* connection
)
{
    afp_session* afp_session = connection->get_afp_session();

    const INT8 afp_command = afp_request.pull_num<INT8>();
    const auto afp_brl_flags = afp_request.pull_num<UINT8>();
    const auto afp_fork_ref = afp_request.pull_num<INT16>();

    INT64 afp_offset;
    INT64 afp_length;

    switch (afp_command)
    {
    case afpByteRangeLock:
        afp_offset = afp_request.pull_num<UINT32>();
        afp_length = afp_request.pull_num<UINT32>();
        break;

    case afpByteRangeLockExt:
        afp_offset = afp_request.pull_num<INT64>();
        afp_length = afp_request.pull_num<INT64>();
        break;

    default:
        return afpParmErr;
    }

    // Get the handle to the open file fork.
    const afp_open_file* open_file = afp_session->find_open_file(afp_fork_ref);
    AFPERROR afp_error = (open_file == nullptr) ? afpParmErr : AFP_OK;

    if (AFP_FAILED(afp_error))
    {
        return afp_error;
    }

    const auto h_file = open_file->get_file_handle();
    if (h_file == INVALID_HANDLE_VALUE)
    {
        return afpParmErr;
    }

    // Determine the size of the file so we can calculate the offset
    // from the end of the file and lock the entire range if
    // necessary.
    const UINT32 afp_file_size = GetFileSize(h_file, nullptr);
    afp_error = (afp_file_size == 0xFFFFFFFF) ? afpParmErr : AFP_OK;

    if (AFP_SUCCESS(afp_error))
    {
        if (afp_brl_flags & kUnlockFlag)
        {
            if (afp_length >= static_cast<INT32>(0xFFFFFFFF))
            {
                switch (afp_offset)  // NOLINT(hicpp-multiway-paths-covered)
                {
                case 0:
                    // 0 offset means we unlock the entire range of the file.
                    afp_length = afp_file_size;
                    break;
                default:
                    // The user wants to lock from offset to the end of the file.
                    afp_length = (afp_file_size - afp_offset);
                    break;
                }
            }

            DBGWRITE(dbg_level_info, L"Unlocking...\n");
            DBGWRITE(dbg_level_info, L"Offset = %d\n", afp_offset);
            DBGWRITE(dbg_level_info, L"Length = %d\n", afp_length);

            //
            //BUGBUG: Actual unlocking code goes here.
            //
        }
        else // Locking
        {
            // If the user wants us to set the offset from the end of the
            // file, then we have a lot more work to do.
            if (afp_brl_flags & kStartEndFlag)
            {
                afp_offset = (afp_offset < 0) ? (afp_file_size + afp_offset) : (afp_file_size - afp_offset);
            }
            else // From the beginning
            {
                if (afp_offset < 0)
                {
                    // The offset can only be negative if we are working from the
                    // end of a file.
                    DBGWRITE(dbg_level_info, L"Negative offset from start!\n");
                    return (afpParmErr);
                }

                // If the length is 0xFFFFFFFF, then we have special handling to do.
                if (afp_command == afpByteRangeLock && afp_length == 0xFFFFFFFF ||
                    afp_command == afpByteRangeLockExt && afp_length == 0xFFFFFFFFFFFFFFFFLL)
                {
                    switch (afp_offset)  // NOLINT(hicpp-multiway-paths-covered)
                    {
                    case 0:
                        // 0 offset means we lock the entire range of the file.
                        afp_length = afp_file_size;
                        break;
                    default:
                        // The user wants to lock from offset to the end of the file.
                        afp_length = (afp_file_size - afp_offset);
                        break;
                    }
                }
            } // From beginning

            DBGWRITE(dbg_level_info, L"Locking...\n");
            DBGWRITE(dbg_level_info, L"Offset = %d\n", afp_offset);
            DBGWRITE(dbg_level_info, L"Length = %d\n", afp_length);

            //
            //BUGBUG: Actual locking code goes here.
            //
        }

        // We return the first byte of the newly locked range
        switch (afp_command)
        {
        case afpByteRangeLock:
            afp_reply.push_num<UINT32>(static_cast<UINT32>(afp_offset));
            break;

        case afpByteRangeLockExt:
            afp_reply.push_num(afp_offset);
            break;

        default:
            return (afpParmErr);
        }
    }

    return afp_error;
}


/*
 * FPRead()
 *
 * Description:
 *		Read data from an opened file fork.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPRead(
    IN afp_net_buffer& afp_request,
    IN afp_net_buffer& afp_reply,
    IN dsi::dsi_connection* connection
)
{
    afp_session* afp_session = connection->get_afp_session();
    INT64 afp_offset;
    INT64 afp_req_count;
    DWORD afp_act_count = 0;

    DBGWRITE(dbg_level_trace, L"Enter\n");

    const auto afp_command = afp_request.pull_num<INT8>();
    afp_request.advance_pos_ptr(sizeof(char));

    const auto afp_fork_ref = afp_request.pull_num<INT16>();

    switch (afp_command)
    {
    case afpRead:
        afp_offset = afp_request.pull_num<INT32>();
        afp_req_count = afp_request.pull_num<INT32>();
        break;

    case afpReadExt:
        afp_offset = afp_request.pull_num<INT64>();
        afp_req_count = afp_request.pull_num<INT64>();
        break;

    default:
        return (afpParmErr);
    }

    // 68K machines running MacOS8.x will ask for a negative offset.
    if (afp_offset < 0)
    {
        return afpParmErr;
    }

    // Here is where we would normally get the newLineMask and char, however,
    // we are not supporting that functionality in this server (that functionality
    // has been dropped in AFP3.x anyway).

    afp_open_file* open_file = afp_session->find_open_file(afp_fork_ref);
    AFPERROR afp_error = (open_file == nullptr) ? afpParmErr : AFP_OK;

    if (AFP_SUCCESS(afp_error))
    {
        const auto user_token = connection->get_user_token();

        if (!impersonate_user(user_token))
        {
            // Impersonation failed, don't allow any further operations.
            return afpAccessDenied;
        }

        afp_error = open_file->read_fork(
            afp_reply,
            afp_offset,
            afp_req_count,
            &afp_act_count
        );

        revert_to_self(user_token);
    }

    // FPRead is a special case in that the return value is returned
    // in the DSI Header's data size field.
    afp_reply.set_dsi_return_value(afp_act_count);

    DBGWRITE(dbg_level_trace, L"Returning error: %d, act count: %d\n", afp_error, afp_act_count);

    return afp_error;
}


/*
 * FPWrite()
 *
 * Description:
 *		Write data to an opened file fork.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPWrite(
    IN afp_net_buffer& afp_request,
    IN afp_net_buffer& afp_reply,
    IN dsi::dsi_connection* connection
)
{
    afp_session* session = connection->get_afp_session();
    INT64 offset;
    INT64 req_count;
    INT64 act_count = 0;

    DBGWRITE(dbg_level_trace, L"Enter\n");

    const auto afp_command = afp_request.pull_num<INT8>();
    const auto afp_flag = afp_request.pull_num<INT8>();
    const auto afp_fork_ref = afp_request.pull_num<INT16>();

    switch (afp_command)
    {
    case afpWrite:
        offset = afp_request.pull_num<INT32>();
        req_count = afp_request.pull_num<INT32>();
        break;

    case afpWriteExt:
        offset = afp_request.pull_num<INT64>();
        req_count = afp_request.pull_num<INT64>();
        break;

    default:
        return afpParmErr;
    }

    const afp_open_file* open_file = session->find_open_file(afp_fork_ref);
    AFPERROR afp_error = open_file == nullptr ? afpParmErr : AFP_OK;

    if (AFP_FAILED(afp_error))
    {
        return afp_error;
    }

    const HANDLE user_token = connection->get_user_token();
    if (!impersonate_user(user_token))
    {
        return afpAccessDenied;
    }

    afp_error = open_file->write_fork(
        afp_request,
        offset,
        req_count,
        (afp_flag & kWriteStartEndFlag) == 1 ? FALSE : TRUE,
        &act_count
    );

    revert_to_self(user_token);

    if (AFP_SUCCESS(afp_error))
    {
        switch (afp_command)  // NOLINT(hicpp-multiway-paths-covered)
        {
        case afpWrite:
            afp_reply.push_num(static_cast<INT32>(act_count));
            break;

        case afpWriteExt:
            afp_reply.push_num(act_count);
            break;
        default:
            ASSERT(0);
        }
    }

    return afp_error;
}
