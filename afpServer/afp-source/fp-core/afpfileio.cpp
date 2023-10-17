// ReSharper disable CppClangTidyClangDiagnosticGnuZeroVariadicMacroArguments
#include "StdAfx.h"
#include "afpfileio.h"

#include "afp_open_file.h"
#include "fdparms.h"
#include "fpinfo.h"


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
    IN const afp_net_buffer& afp_request,
    IN const afp_net_buffer& afp_reply,
    IN const dsi::dsi_connection* connection
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
    IN const afp_net_buffer& afp_reply,
    IN const dsi::dsi_connection* connection
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
    IN const dsi::dsi_connection* connection
)
{
    unichar afp_pathname[MAX_AFP_PATH];
    afp_session* afp_afp_session = connection->get_afp_session();
    p_afp_index afp_entry = nullptr;

    WIN32_FILE_ATTRIBUTE_DATA win_attributes;

    // The first byte contains the afp command.
    afp_request.advance_pos_ptr(sizeof(UINT8));

    const INT8 afp_fork = (afp_request.pull_num<INT8>() & kResourceForkBit) ? kRsrcFork : kDataFork;
    const auto afp_volume_id = afp_request.pull_num<UINT16>();
    const auto afp_dir_id = afp_request.pull_num<UINT32>();
    auto afp_bitmap = afp_request.pull_num<UINT16>();
    const auto afp_mode = afp_request.pull_num<INT16>();
    const INT8 afp_path_type = afp_request.pull_num<INT8>();

    // The client must have previously opened the volume we are operating on.
    AFP_VOLUME_INFO* afp_vol_info = afp_afp_session->find_open_volume_block(afp_volume_id);
    if (afp_vol_info == nullptr)
    {
        // The volume has not been opened, we need to return error.
        return afpParmErr;
    }

    // Get the pathname of the object we're working on.
    AFPERROR afp_error = afp_request.get_pascal_string(afp_pathname, sizeof(afp_pathname), true, afp_path_type);

    if (AFP_FAILED(afp_error))
    {
        return afp_error;
    }

    if (lstrlen(afp_pathname) == 0)
    {
        DBGWRITE(dbg_level_info, L"Null pathname!\n");
        return afpParmErr;
    }

    // Get the index entry pertaining to the file we're opening.
    afp_error = AFPIndexFindEntryWithPath(afp_vol_info, afp_dir_id, afp_pathname, &afp_entry);
    if (AFP_FAILED(afp_error))
    {
        DBGWRITE(dbg_level_info, L"Index entry not found! (%s)\n", afp_pathname);
        return afpObjectNotFound;
    }

    // Collect attribute data on the file or folder.
    if (!GetFileAttributesEx(afp_entry->sz_path, GetFileExInfoStandard, &win_attributes))
    {
        DBGWRITE(dbg_level_info, L"GetFileAttributesEx() failed! (%d)\n", GetLastError());
        DBGWRITE(dbg_level_info, L"path = %s\n", afp_entry->sz_path);

        return afpObjectNotFound;
    }

    // Make sure the user doesn't have too many files open already.
    if (afp_afp_session->get_num_files_open() > MAX_AFP_FILES_OPEN)
    {
        DBGWRITE(dbg_level_info, L"****Can't open file, too many open already! (%d)\n",
                 afp_afp_session->get_num_files_open());
        return (afpTooManyFilesOpen);
    }

    afp_open_file* open_file = afp_session::open_file(
        afp_afp_session,
        afp_volume_id,
        afp_dir_id,
        afp_mode,
        afp_fork,
        afp_entry,
        afp_entry->sz_path
    );

    afp_error = (open_file == nullptr) ? afpMiscErr : AFP_OK;
    if (AFP_SUCCESS(afp_error))
    {
        // Impersonate the logged on user for security checks.
        if (!afp_impersonate_user(connection->get_user_token()))
        {
            // Impersonation failed, don't allow any further operations.
            return afpAccessDenied;
        }

        // Now, actually open the file for access as desired.
        afp_error = open_file->afp_open_file_fork();

        afp_revert_to_self(connection->get_user_token());

        // If we successfully open the file, then return whatever object parms
        // the caller has asked for.
        if (AFP_SUCCESS(afp_error))
        {
            // We need to return the file ref num to the caller.
            const auto afp_new_ref_num = open_file->get_file_ref_num();

            // We don't support the following bitmaps, so we clear them.
            if (connection->get_afp_version() < afpVersion30)
            {
                if (afp_bitmap & kFPProDos) afp_bitmap &= ~kFPProDos;
                if (afp_bitmap & kFPShortName) afp_bitmap &= ~kFPShortName;
            }

            // Add in the bitmap and new refnum id.
            afp_reply.push_num(afp_bitmap);
            afp_reply.push_num(afp_new_ref_num);

            afp_error = AFPGetObjectParms(
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
        afp_afp_session->close_file(open_file->get_file_ref_num());
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
    IN const afp_net_buffer& afp_reply,
    IN const dsi::dsi_connection* connection
)
{
    UNREFERENCED_PARAMETER(afp_reply);

    afp_session* afp_afp_session = connection->get_afp_session();

    DBGWRITE(dbg_level_all, L"Enter\n");

    // The first byte contains the afp command.
    afp_request.advance_pos_ptr(sizeof(int16));

    const auto afp_fork_ref = afp_request.pull_num<INT16>();
    const auto afp_bitmap = afp_request.pull_num<UINT16>();

    // Fork length bytes
    afp_request.advance_pos_ptr(sizeof(UINT32));

    afp_open_file* open_file = afp_afp_session->find_open_file(afp_fork_ref);
    AFPERROR afp_error = (open_file == nullptr) ? afpParmErr : AFP_OK;

    if (AFP_SUCCESS(afp_error))
    {
        // Make sure the volume is not marked read-only.
        afp_error = AFPVolumeWriteAccessCheck(open_file->get_vol_id());
        if (AFP_FAILED(afp_error))
        {
            return afp_error;
        }

        if ((afp_bitmap & kFPDFLen) || (afp_bitmap & kFPExtDataForkLen))
        {
            if (open_file->get_fork() != kDataFork)
            {
                DBGWRITE(dbg_level_all, L"Data fork is not open!\n");
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
        DBGWRITE(dbg_level_all, L"Fork item not found!\n");
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
    IN const dsi::dsi_connection* connection
)
{
    afp_session* afp_session = connection->get_afp_session();

    WIN32_FILE_ATTRIBUTE_DATA win_attributes;

    DBGWRITE(dbg_level_all, L"nter\n");

    // The first byte contains the afp command.
    afp_request.advance_pos_ptr(sizeof(int16));

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
        afp_error = AFPGetObjectParms(
            afp_reply,
            afp_bitmap,
            open_file->get_vol_id(),
            open_file->get_dir_id(),
            connection,
            open_file->get_entry(),
            win_attributes
        );
    }

    DBGWRITE(dbg_level_all, L"Returning %d\n", afp_error);

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
    IN const afp_net_buffer& afp_reply,
    IN const dsi::dsi_connection* connection
)
{
    UNREFERENCED_PARAMETER(afp_reply);

    unichar afp_pathname[MAX_AFP_PATH];
    unichar win_path[MAX_PATH];
    AFPCHARMAPARRAY char_map;
    p_afp_index afp_entry = nullptr;

    DBGWRITE(dbg_level_all, L"Enter\n");

    // First byte is padding.
    afp_request.advance_pos_ptr(sizeof(int8));

    const auto create_flag = afp_request.pull_num<INT8>();
    const auto afp_volume_id = afp_request.pull_num<UINT16>();
    const auto afp_dir_id = afp_request.pull_num<UINT32>();
    const auto afp_path_type = afp_request.pull_num<INT8>();
    const auto afp_session = connection->get_afp_session();

    // The client must have previously opened the volume we are creating
    // the file on.
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

    // Get the pathname of the object we're working on.
    memset(char_map, 0, sizeof(AFPCHARMAPARRAY));

    afp_error = afp_request.get_pascal_string(
        afp_pathname,
        sizeof(afp_pathname),
        true,
        afp_path_type,
        char_map
    );

    if (AFP_FAILED(afp_error))
    {
        return afp_error;
    }

    // Pathname cannot be null since it contains the new file name.
    if (lstrlen(afp_pathname) == 0)
    {
        DBGWRITE(dbg_level_error, L"Null pathname!\n");
        return afpParmErr;
    }

    AFP_VOLUME_INFO* afp_volume_info = afp_session->find_open_volume_block(afp_volume_id);
    if (afp_volume_info == nullptr)
    {
        DBGWRITE(dbg_level_error, L"Bad volume ID!\n");
        return afpParmErr;
    }

    // Take the path supplied by the Mac client and convert it into a windows path.
    // The path should start from the directory as pointed to by dirID.
    afp_error = AFPIndexFindEntryWithPath(afp_volume_info, afp_dir_id, nullptr, &afp_entry);
    if (AFP_FAILED(afp_error))
    {
        // We couldn't find the requested object.
        DBGWRITE(dbg_level_error, L"Path not found in FPCreatFile!\n");
        return afp_error;
    }

    // Create the full path to the new file.
    // ReSharper disable once CppFunctionResultShouldBeUsed
    StringCbPrintf(win_path, sizeof(win_path), TEXT("%s\\%s"), afp_entry->sz_path, afp_pathname);

    // Don't let the change notify mechanism do anything yet.
    auto_lock lock(&afp_volume_info->csNotifyQueue);

    // Impersonate the logged on user. This makes sure only those who
    // are allowed can create files.
    if (!afp_impersonate_user(afp_session->hUserToken))
    {
        // Impersonation failed, don't allow any further operations.
        return afpAccessDenied;
    }

    // Call the Win32 API that creates the file for us on this filesystem.
    const auto h_file = CreateFile(
        win_path,
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        (create_flag & kHardCreate) ? CREATE_ALWAYS : CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    afp_revert_to_self(afp_session->hUserToken);

    if (h_file == INVALID_HANDLE_VALUE)
    {
        DBGWRITE(dbg_level_error,
                 L"CreateFile() for data fork failed! (%d) (%s)\n",
                 GetLastError(),
                 win_path);

        afp_error = afp_map_win_error_to_afp(GetLastError(), afpParmErr);
    }
    else
    {
        afp_error = NO_ERROR;

        // Add the new file to the afp index for tracking purposes.
        afp_entry = AFPIndexAddEntry(
            afp_volume_info,
            afp_volume_id,
            afp_dir_id,
            afp_pathname,
            win_path,
            false,
            nullptr
        );

        if (afp_entry != nullptr)
        {
            // Now make sure that our NTFS streams are created so we have
            // a FInfo and resource forks.
            AFPCheckForAndAddFinderInfoStream(afp_entry);
            AFPCheckForAndCreateFinderRsrcStream(afp_entry);

            // Copy the charmap that contains the Mac-Windows mapping for
            // characters to the entries mapping table.
            AFPAddCharMapToIndexEntry(afp_entry, char_map);
        }

        // This tells the afp message system to kick in and notify other
        // Mac clients that a change happened on the volume.
        AFPMarkVolumeDirty(afp_volume_info->wVolID);

        // We're done with the file and we don't need the handle
        // to it anymore.
        CloseHandle(h_file);
    }

    return afp_error;
}


/*
 * FPByteRangeLock()
 *
 * Description:
 *		Lock a range for exclusive use.
 *
 * Returns: AFPERROR
 */

AFPERROR FPByteRangeLock(
    IN afp_net_buffer& afpRequest,
    IN afp_net_buffer& afpReply,
    IN dsi::dsi_connection* connection
)
{
    afp_session* afpAFPSession = static_cast<afp_session*>(afpSession->afpSessionObject);
    int64 afpOffset = 0;
    int64 afpLength = 0;

    //
    //Skip the first byte which is afp command.
    //
    int8 afpCommand = afpRequest.pull_num<INT8>();
    uint8 afpBRLFlags = afpRequest.pull_num<UINT8>();
    uint16 afpForkRef = afpRequest.pull_num<UINT16>();

    switch (afpCommand)
    {
    case afpByteRangeLock:
        afpOffset = afpRequest.pull_num<UINT32>();
        afpLength = afpRequest.pull_num<UINT32>();
        break;

    case afpByteRangeLockExt:
        afpOffset = afpRequest.pull_num<INT64>();
        afpLength = afpRequest.pull_num<INT64>();
        break;

    default:
        return (afpParmErr);
    }

    //
    //Get the handle to the open file fork.
    //
    afp_open_file* afpAFPOpenFile = afpAFPSession->find_open_file(afpForkRef);
    AFPERROR afpError = (afpAFPOpenFile == nullptr) ? afpParmErr : AFP_OK;

    if (AFP_FAILED(afpError))
    {
        return (afpError);
    }

    auto hFile = afpAFPOpenFile->get_file_handle();

    if (hFile == INVALID_HANDLE_VALUE)
    {
        //
        //Bad news, the fork is apparently not open.
        //
        return (afpParmErr);
    }

    //
    //Determine the size of the file so we can calculate the offset
    //from the end of the file and lock the entire range if
    //necessary.
    //
    uint32 afpFileSize = GetFileSize(hFile, nullptr);
    afpError = (afpFileSize == 0xFFFFFFFF) ? afpParmErr : AFP_OK;

    if (AFP_SUCCESS(afpError))
    {
        if (afpBRLFlags & kUnlockFlag)
        {
            if (afpLength >= static_cast<int32>(0xFFFFFFFF))
            {
                switch (afpOffset)
                {
                case 0:
                    //
                    //0 offset means we unlock the entire range of the file.
                    //
                    afpLength = afpFileSize;
                    break;
                default:
                    //
                    //The user wants to lock from offset to the end of the file.
                    //
                    afpLength = (afpFileSize - afpOffset);
                    break;
                }
            }

            DBGWRITE(dbg_level_info, L"[afp:FPByteRangeLock]Unlocking...\n");
            DBGWRITE(dbg_level_info, L"[afp:FPByteRangeLock]Offset = %d\n", afpOffset);
            DBGWRITE(dbg_level_info, L"[afp:FPByteRangeLock]Length = %d\n", afpLength);

            //
            //BUGBUG: Actual unlocking code goes here.
            //
        }
        else //Locking
        {
            //
            //If the user wants us to set the offset from the end of the
            //file, then we have a lot more work to do.
            //
            if (afpBRLFlags & kStartEndFlag)
            {
                afpOffset = (afpOffset < 0) ? (afpFileSize + afpOffset) : (afpFileSize - afpOffset);
            }
            else //From the beginning
            {
                if (afpOffset < 0)
                {
                    //
                    //The offset can only be negative if we are working from the
                    //end of a file.
                    //
                    DBGWRITE(dbg_level_info, L"[afp:FPByteRangeLock]Negative offset from start!\n");
                    return (afpParmErr);
                }

                //
                //If the length is 0xFFFFFFFF, then we have special handling to do.
                //
                if (((afpCommand == afpByteRangeLock) && (afpLength == 0xFFFFFFFF)) ||
                    ((afpCommand == afpByteRangeLockExt) && (afpLength == 0xFFFFFFFFFFFFFFFFLL)))
                {
                    switch (afpOffset)
                    {
                    case 0:
                        //
                        //0 offset means we lock the entire range of the file.
                        //
                        afpLength = afpFileSize;
                        break;
                    default:
                        //
                        //The user wants to lock from offset to the end of the file.
                        //
                        afpLength = (afpFileSize - afpOffset);
                        break;
                    }
                }
            } //from beginning

            DBGWRITE(dbg_level_info, L"[afp:FPByteRangeLock]Locking...\n");
            DBGWRITE(dbg_level_info, L"[afp:FPByteRangeLock]Offset = %d\n", afpOffset);
            DBGWRITE(dbg_level_info, L"[afp:FPByteRangeLock]Length = %d\n", afpLength);

            //
            //BUGBUG: Actual locking code goes here.
            //
        }

        //
        //We return the first byte of the newly locked range
        //
        switch (afpCommand)
        {
        case afpByteRangeLock:
            afpReply.push_int32(static_cast<uint32>(afpOffset));
            break;

        case afpByteRangeLockExt:
            afpReply.push_int64(afpOffset);
            break;

        default:
            return (afpParmErr);
        }
    }

    return (afpError);
}


/*
 * FPRead()
 *
 * Description:
 *		Read data from an opened file fork.
 *
 * Returns: AFPERROR
 */

AFPERROR FPRead(
    IN afp_net_buffer& afpRequest,
    IN afp_net_buffer& afpReply,
    IN dsi::dsi_connection* connection
)
{
    afp_session* afpAFPSession = static_cast<afp_session*>(afpSession->afpSessionObject);
    int64 afpOffset = 0;
    int64 afpReqCount = 0;
    uint32 afpActCount = 0;

    DBGWRITE(dbg_level_all, L"[afp:FPRead]Enter\n");

    //
    //The first byte contains the afp command.
    //
    int8 afpCommand = afpRequest.pull_num<INT8>();
    afpRequest.advance_pos_ptr(sizeof(int8));

    uint16 afpForkRef = afpRequest.pull_num<UINT16>();

    switch (afpCommand)
    {
    case afpRead:
        afpOffset = afpRequest.pull_num<UINT32>();
        afpReqCount = afpRequest.pull_num<UINT32>();
        break;

    case afpReadExt:
        afpOffset = afpRequest.pull_num<INT64>();
        afpReqCount = afpRequest.pull_num<INT64>();
        break;

    default:
        return (afpParmErr);
    }

    //
    //Here is where we would normally get the newLineMask and char, however,
    //we are not supporting that functionality in this server (that functionality
    //has been dropped in AFP3.x anyway).
    //

    afp_open_file* afpAFPOpenFile = afpAFPSession->find_open_file(afpForkRef);
    AFPERROR afpError = (afpAFPOpenFile == nullptr) ? afpParmErr : AFP_OK;

    if (AFP_SUCCESS(afpError))
    {
        if (!afp_impersonate_user(afpSession->hUserToken))
        {
            //
            //Impersonation failed, don't allow any further operations.
            //
            return (afpAccessDenied);
        }

        afpError = afpAFPOpenFile->read_fork(
            afpReply,
            afpOffset,
            afpReqCount,
            &afpActCount
        );

        afp_revert_to_self(afpSession->hUserToken);
    }

    //
    //FPRead is a special case in that the return value is returned
    //in the DSI Header's datasize field.
    //
    afpReply.set_dsi_return_value(afpActCount);

    DBGWRITE(dbg_level_all, L"[afp:FPRead]Returning error %d and count %d\n", afpError, afpActCount);

    return (afpError);
}


/*
 * FPWrite()
 *
 * Description:
 *		Write data to an opened file fork.
 *
 * Returns: AFPERROR
 */

AFPERROR FPWrite(
    IN afp_net_buffer& afpRequest,
    IN afp_net_buffer& afpReply,
    IN dsi::dsi_connection* connection
)
{
    afp_session* afpAFPSession = static_cast<afp_session*>(afpSession->afpSessionObject);
    int64 afpOffset = 0;
    int64 afpReqCount = 0;
    int64 afpActCount = 0;

    DBGWRITE(dbg_level_all, L"[afp:FPWrite]Enter\n");

    //
    //The first byte contains the afp command.
    //
    int8 afpCommand = afpRequest.pull_num<INT8>();
    int8 afpFlag = afpRequest.pull_num<INT8>();
    uint16 afpForkRef = afpRequest.pull_num<UINT16>();

    switch (afpCommand)
    {
    case afpWrite:
        afpOffset = afpRequest.pull_num<UINT32>();
        afpReqCount = afpRequest.pull_num<UINT32>();
        break;

    case afpWriteExt:
        afpOffset = afpRequest.pull_num<INT64>();
        afpReqCount = afpRequest.pull_num<INT64>();
        break;

    default:
        return (afpParmErr);
    }

    afp_open_file* afpAFPOpenFile = afpAFPSession->find_open_file(afpForkRef);
    AFPERROR afpError = (afpAFPOpenFile == nullptr) ? afpParmErr : AFP_OK;

    if (AFP_SUCCESS(afpError))
    {
        //
        //Make sure the volume is not marked read-only.
        //
        afpError = AFPVolumeWriteAccessCheck(afpAFPOpenFile->get_vol_id());

        if (AFP_FAILED(afpError))
        {
            return (afpError);
        }

        if (!afp_impersonate_user(afpSession->hUserToken))
        {
            //
            //Impersonation failed, don't allow any further operations.
            //
            return (afpAccessDenied);
        }

        afpError = afpAFPOpenFile->write_fork(
            afpRequest,
            afpOffset,
            afpReqCount,
            (afpFlag & kWriteStartEndFlag) ? FALSE : TRUE,
            &afpActCount
        );

        afp_revert_to_self(afpSession->hUserToken);

        if (AFP_SUCCESS(afpError))
        {
            switch (afpCommand)
            {
            case afpWrite:
                afpReply.push_int32(static_cast<int32>(afpActCount));
                break;

            case afpWriteExt:
                afpReply.push_int64(afpActCount);
                break;
            }
        }
    }

    return (afpError);
}
