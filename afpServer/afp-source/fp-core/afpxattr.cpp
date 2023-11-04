// ReSharper disable CppInconsistentNaming
// ReSharper disable CppClangTidyClangDiagnosticGnuZeroVariadicMacroArguments
// ReSharper disable CppParameterMayBeConstPtrOrRef
#include "StdAfx.h"

#include "afp.h"
#include "afphelp.h"
#include "afpdebug.h"
#include "afpxattr.h"
#include "volumes.h"
#include "fpinfo.h"
#include "afp_session.h"

#include "AltStreams.h"

/*
 * FPGetExtAttribute()
 *
 * Description:
 *		Retrieves an extended attribute from the file.
 *
 * Returns: AFPERROR
 */

AFPERROR FPGetExtAttribute(
    IN afp_net_buffer& afpRequest,
    IN afp_net_buffer& afpReply,
    IN dsi::dsi_connection* connection
)
{
    WCHAR afpPathname[MAX_AFP_PATH];
    WCHAR afpAttrName[MAX_AFP_NAME];
    uint16 afpNameLen;
    uint32 afpActualRead = 0;
    afp_index* afpEntry = nullptr;

    DBGWRITE(dbg_level_all, L"[FPGetExtAttribute]Enter...\n");

    //
    //Advance beyond command and pad byte
    //
    afpRequest.advance_pos_ptr(sizeof(int16));

    // ReSharper disable CppDeclaratorNeverUsed
    const auto afpVolID = afpRequest.pull_num<UINT16>();
    const uint32 afpDirID = afpRequest.pull_num<UINT32>();
    const auto afpBitmap = afpRequest.pull_num<UINT16>();
    afpRequest.advance_pos_ptr(sizeof(INT64) * 2); // offset and req count
    const int32 afpMaxReplySize = afpRequest.pull_num<INT32>();
    const int8 afpPathtype = afpRequest.pull_num<INT8>();
    // ReSharper restore CppDeclaratorNeverUsed

    //
    //See if the user has the volume open and get a pointer to
    //the volume object.
    //	
    if (!connection->get_afp_session()->session_has_volume_open(afpVolID))
    {
        DBGWRITE(dbg_level_warning, L"[FPGetExtAttribute]User does not have volume open!\n");
        return (afpParmErr);
    }

    //
    //Get the pathname of the object we're working on.
    //
    AFPERROR afpError = afpRequest.get_pascal_string(afpPathname, sizeof(afpPathname), true, afpPathtype);

    if (AFP_FAILED(afpError))
    {
        DBGWRITE(dbg_level_warning, L"[FPGetExtAttribute]Could not retrieve pathname! (%d)\n", afpError);
        return (afpError);
    }

    //
    //Advance if we need to in order to land on an even boundary
    //
    afpRequest.advance_if_odd();

    //
    //Get the size of the attribute name string.
    //
    afpNameLen = afpRequest.pull_num<UINT16>();

    //
    //Now get the string. Note that on return afpNameLen now contains the actual
    //length of the converted string.
    //
    afpError = afpRequest.get_unicode_string(afpAttrName, sizeof(afpAttrName), &afpNameLen);

    if (AFP_FAILED(afpError))
    {
        DBGWRITE(dbg_level_warning, L"[FPGetExtAttribute]Could not retrieve attribute name! (%d)\n", afpError);
        return (afpError);
    }

    DBGWRITE(dbg_level_all, L"[FPGetExtAttribute]Getting attr, file=%s, attr=%s\n", afpPathname, afpAttrName);

    //
    //Set the entry object that will point to the object we're working with.
    //
    afpError = AFPIndexFindEntryWithVolIDAndPath(
        afpVolID,
        afpDirID,
        afpPathname,
        &afpEntry
    );

    if (AFP_SUCCESS(afpError))
    {
        //
        //Get the size of the attribute data. Note we don't pass the name of the
        //stream because we already have an open handle to the right stream.
        //
        const auto afpDataLen = static_cast<uint32>(GetStreamLen(const_cast<wchar_t*>(afpEntry->sz_path.c_str()), afpAttrName));

        if (afpDataLen != BAD_LENGTH_32)
        {
            //
            //Plum the first 2 items in the reply block. These will always be
            //returned in every case beyond here.
            //
            afpReply.push_num(afpBitmap);
            afpReply.push_num(afpDataLen);

            DBGWRITE(dbg_level_all, L"[FPGetExtAttribute]DataLen == %d\n", afpDataLen);

            if (afpMaxReplySize > 0)
            {
                if (afpReply.check_space_needed(afpDataLen))
                {
                    StringCbPrintf(afpPathname, sizeof(afpPathname), L"%s:%s", afpEntry->sz_path.c_str(), afpAttrName);

                    if (!afp_impersonate_user(connection->get_user_token()))
                    {
                        //
                        //Impersonation failed, don't allow any further operations.
                        //
                        return (afpAccessDenied);
                    }

                    //
                    //Open a handle to the file stream object.
                    //
                    const auto hFile = CreateFile(
                        afpPathname,
                        GENERIC_READ,
                        FILE_SHARE_READ,
                        nullptr,
                        OPEN_EXISTING,
                        FILE_FLAG_BACKUP_SEMANTICS,
                        nullptr
                    );

                    if (hFile != INVALID_HANDLE_VALUE)
                    {
                        const auto read_result = ReadFile(
                            hFile,
                            afpReply.get_current_pos_ptr(),
                            min((int)afpDataLen, (int)afpMaxReplySize),
                            &afpActualRead,
                            nullptr
                        );

                        CloseHandle(hFile);

                        DBGWRITE(
                            dbg_level_all,
                            L"ReadFile() returned: %d, read %d bytes\n",
                            GetLastError(),
                            afpActualRead
                        );

                        if (read_result)
                        {
                            //
                            //We need to advance_pos_ptr the data buffer the amount we read.
                            //
                            afpReply.advance_pos_ptr(afpActualRead);
                        }
                    }

                    afp_revert_to_self(connection->get_user_token());
                }
                else
                {
                    //
                    //Not enough space in the reply buffer.
                    //
                    DBGWRITE(dbg_level_warning, L"[FPGetExtAttribute]Not enough space in reply buffer!\n");
                    ASSERT(0);

                    return (afpMiscErr);
                }
            }
        }
        else
        {
            DBGWRITE(dbg_level_warning, L"[FPGetExtAttribute]GetStreamLen() failed!\n");
            afpError = afpMiscErr;
        }
    }
    else
    {
        DBGWRITE(
            dbg_level_warning,
            L"[FPGetExtAttribute]Object not found! (name = %s, dirID = %d)\n",
            afpPathname,
            afpDirID
        );
    }

    DBGWRITE(dbg_level_all, L"[FPGetExtAttribute]Returning: %d\n", afpError);

    return (afpError);
}


/*
 * FPSetExtAttribute()
 *
 * Description:
 *		Sets an extended file attribute, optionally replacing existing
 *		content.
 *
 * Returns: AFPERROR
 */

AFPERROR FPSetExtAttribute(
    IN afp_net_buffer& afpRequest,
    IN afp_net_buffer& afpReply,
    IN dsi::dsi_connection* connection
)
{
    UNREFERENCED_PARAMETER(afpReply);

    WCHAR afpPathname[MAX_AFP_PATH];
    WCHAR afpAttrName[MAX_AFP_NAME];
    afp_index* afpEntry = nullptr;
    uint16 afpNameLen;
    DWORD bytesWritten = 0;

    DBGWRITE(dbg_level_info, L"[afp:FPSetExtAttribute]Enter...\n");

    //
    //Advance beyond command and pad byte
    //
    afpRequest.advance_pos_ptr(sizeof(int16));

    const auto afpVolID = afpRequest.pull_num<UINT16>();
    const uint32 afpDirID = afpRequest.pull_num<UINT32>();
    const auto afpBitmap = afpRequest.pull_num<UINT16>();
    afpRequest.advance_pos_ptr(sizeof(INT64)); //offset
    const int8 afpPathtype = afpRequest.pull_num<INT8>();

    //
    //Get the pathname of the object we're working on.
    //
    AFPERROR afpError = afpRequest.get_pascal_string(afpPathname, sizeof(afpPathname), true, afpPathtype);

    if (AFP_FAILED(afpError))
    {
        DBGWRITE(dbg_level_info, L"[afp:FPSetExtAttribute]Could not retrieve pathname! (%d)\n", afpError);
        return (afpError);
    }

    //
    //A pad byte is inserted to make the buffer end on an
    //even boundary.
    //
    afpRequest.advance_if_odd();

    //
    //Now get the string. Note that on return afpNameLen now contains the actual
    //length of the converted string.
    //
    afpNameLen = afpRequest.pull_num<UINT16>();
    afpError = afpRequest.get_unicode_string(afpAttrName, sizeof(afpAttrName), &afpNameLen);

    if (AFP_FAILED(afpError))
    {
        DBGWRITE(dbg_level_info, L"[afp:FPSetExtAttribute]Could not retrieve attribute name! (%d)\n", afpError);
        return (afpError);
    }

    DBGWRITE(dbg_level_info, L"[afp:FPSetExtAttribute]Setting attr, file=%s, attr=%s\n", afpPathname, afpAttrName);

    //
    //The attribute data length we're going to write.
    //
    const uint32 afpAttrDataLen = afpRequest.pull_num<UINT32>();

    //
    //See if the user has the volume open and get a pointer to
    //the volume object.
    //
    if (!connection->get_afp_session()->session_has_volume_open(afpVolID))
    {
        DBGWRITE(dbg_level_info, L"[afp:FPSetExtAttribute]User does not have volume open!\n");
        return (afpParmErr);
    }

    //
    //Set the entry object that will point to the object we're working with.
    //
    afpError = AFPIndexFindEntryWithVolIDAndPath(
        afpVolID,
        afpDirID,
        afpPathname,
        &afpEntry
    );

    if (AFP_SUCCESS(afpError))
    {
        //
        //We have different errors to check for based on the supplied attribute
        //flags (replace and/or already exists).
        //
        const auto status = static_cast<uint32>(GetStreamLen(const_cast<wchar_t*>(afpEntry->sz_path.c_str()), afpAttrName));

        if ((status == BAD_LENGTH_32) && (afpBitmap & kXAttrCreate))
        {
            return (afpObjectExists);
        }
        if (afpBitmap & kXAttrReplace)
        {
            return (afpObjectNotFound);
        }

        StringCbPrintf(afpPathname, sizeof(afpPathname), L"%s:%s", afpEntry->sz_path.c_str(), afpAttrName);

        if (!afp_impersonate_user(connection->get_user_token()))
        {
            //
            //Impersonation failed, don't allow any further operations.
            //
            return (afpAccessDenied);
        }

        //
        //Open a handle to the file stream object.
        //
        const auto hFile = CreateFile(
            afpPathname,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            CREATE_ALWAYS,
            FILE_FLAG_BACKUP_SEMANTICS,
            nullptr
        );

        if (hFile != INVALID_HANDLE_VALUE)
        {
            WriteFile(hFile, afpRequest.get_current_pos_ptr(), afpAttrDataLen, &bytesWritten, nullptr);
            CloseHandle(hFile);

            DBGWRITE(
                dbg_level_info,
                L"[FPGetExtAttribute]WriteFile() returned: %d\n",
                GetLastError()
            );
        }

        afp_revert_to_self(connection->get_user_token());

        //
        //As a last check, make sure the right amount of bytes were
        //written out to the atrribute.
        //
        if (bytesWritten != afpAttrDataLen)
        {
            DBGWRITE(dbg_level_info, L"[afp:FPSetExtAttribute]WriteFork() wrote out the wrong number of bytes!\n",
                     afpError);
            return (afpMiscErr);
        }
    }

    DBGWRITE(dbg_level_info, L"[FPSetExtAttribute]Wrote(bytes): %d\n", bytesWritten);
    DBGWRITE(dbg_level_info, L"[FPSetExtAttribute]Returning:    %d\n", afpError);

    return (afpError);
}


/*
 * FPListExtAttribute()
 *
 * Description:
 *		List all extended attributes.
 *
 * Returns: AFPERROR
 */

AFPERROR FPListExtAttribute(
    IN afp_net_buffer& afpRequest,
    IN afp_net_buffer& afpReply,
    IN dsi::dsi_connection* connection
)
{
    WCHAR afpPathname[MAX_AFP_PATH];
    afp_index* afpEntry = nullptr;
    int32 afpDataLen = 0;

    DBGWRITE(dbg_level_info, L"[FPListExtAttribute]Enter...\n");

    //
    //Advance beyond command and pad byte
    //
    afpRequest.advance_pos_ptr(sizeof(int16));

    const auto afpVolID = afpRequest.pull_num<UINT16>();
    const uint32 afpDirID = afpRequest.pull_num<UINT32>();
    const auto afpBitmap = afpRequest.pull_num<UINT16>();
    afpRequest.advance_pos_ptr(sizeof(UINT16) + sizeof(UINT32)); // req count and start index
    const int32 afpMaxReplySize = afpRequest.pull_num<INT32>();
    const int8 afpPathtype = afpRequest.pull_num<INT8>();

    //
    //See if the user has the volume open and get a pointer to
    //the volume object.
    //
    if (!connection->get_afp_session()->session_has_volume_open(afpVolID))
    {
        DBGWRITE(dbg_level_info, L"[FPListExtAttribute]User does not have volume open!\n");
        return (afpParmErr);
    }

    //
    //Get the pathname of the object we're working on.
    //
    AFPERROR afpError = afpRequest.get_pascal_string(afpPathname, sizeof(afpPathname), true, afpPathtype);

    if (AFP_FAILED(afpError))
    {
        DBGWRITE(dbg_level_info, L"[FPListExtAttribute]Could not retrieve pathname! (%d)\n", afpError);
        return (afpError);
    }

    //
    //Set the entry object that will point to the object we're working with.
    //
    afpError = AFPIndexFindEntryWithVolIDAndPath(
        afpVolID,
        afpDirID,
        afpPathname,
        &afpEntry
    );

    if (AFP_SUCCESS(afpError))
    {
        std::vector<afp_attr_info> streamArray;
        char ansi_string[MAX_AFP_NAME];

        DBGWRITE(
            dbg_level_info,
            L"[FPListExtAttribute]Getting stream list for %s\n", afpEntry->sz_path.c_str());

        if (!afp_impersonate_user(connection->get_user_token()))
        {
            //
            //Impersonation failed, don't allow any further operations.
            //
            return (afpAccessDenied);
        }

        afpError = GetStreams(const_cast<wchar_t*>(afpEntry->sz_path.c_str()), streamArray);

        if (AFP_SUCCESS(afpError))
        {
            //
            //The bitmap is always returned to the caller.
            //
            afpReply.push_num(afpBitmap);

            //
            //Save the position of the datalen field...
            //
            auto* saveDL = reinterpret_cast<uint32*>(afpReply.get_current_pos_ptr());
            afpReply.advance_pos_ptr(sizeof(uint32));

            //
            //Now get the size of all the name strings and optionally plumb
            //them into the reply buffer.
            //

            for (const auto& [stream_size, stream_name] : streamArray)
            {
                const auto cbStreamName = static_cast<int16>(lstrlen(stream_name));

                //
                //We're only interested in streams with names (i.e not the main stream)
                //
                if (cbStreamName > 0)
                {
                    //
                    //Check to make sure the stream isn't one of our special streams. We
                    //don't want to send_ streams the server owns back to the client (e.g.
                    //the resource or info streams).
                    //
                    if ((!lstrcmp(stream_name, afp_info_stream_name)) ||
                        (!lstrcmp(stream_name, AFP_RESOURCE_FORK_NAME_PLAIN)))
                    {
                        continue;
                    }

                    afpDataLen += cbStreamName + 1;

                    if (afpMaxReplySize > 0)
                    {
                        if (afpDataLen <= afpMaxReplySize)
                        {
                            DBGWRITE(
                                dbg_level_info,
                                L"[FPListExtAttribute]New attr name added: %s\n",
                                    afpAttrInfo.stream_name);


                            if (afp_windows_to_mac_path(stream_name, sizeof(ansi_string), nullptr,
                                                        ansi_string) != nullptr)
                            {
                                afpReply.push_raw_data(ansi_string, strlen(ansi_string) + sizeof(char));
                            }
                        }
                        else
                        {
                            //
                            //Our buffer will grow larger than the client can handle. Stop
                            //including new names, adjust the reported data size and return.
                            //
                            DBGWRITE(dbg_level_info, L"[FPListExtAttribute]Data len exceeds client buffer size! (%d)\n",
                                     afpMaxReplySize);

                            afpDataLen -= cbStreamName + 1;
                            break;
                        }
                    }
                }
            }

            //
            //If we're only returning the size, we need to include the size of
            //the bitmap and datalen parameters.
            //
            if (afpMaxReplySize == 0)
            {
                afpDataLen += (sizeof(int16) + sizeof(int32));
            }

            //
            //Set the data length field now that we've calculated it completely.
            //
            *saveDL = htonl(afpDataLen);
        }

        afp_revert_to_self(connection->get_user_token());
    }

    DBGWRITE(dbg_level_info, L"[FPListExtAttribute]Name list datalen = %d\n", afpDataLen);
    DBGWRITE(dbg_level_info, L"[FPListExtAttribute]Returning %d\n", afpError);

    return (afpError);
}


/*
 * FPRemoveExtAttribute()
 *
 * Description:
 *		Remove an extended file attribute.
 *
 * Returns: AFPERROR
 */

AFPERROR FPRemoveExtAttribute(
    IN afp_net_buffer& afpRequest,
    IN afp_net_buffer& afpReply,
    IN dsi::dsi_connection* connection
)
{
    UNREFERENCED_PARAMETER(afpReply);

    WCHAR afpPathname[MAX_AFP_PATH];
    WCHAR afpAttrName[MAX_PATH];
    afp_index* afpEntry = nullptr;
    uint16 afpNameLen;

    DBGWRITE(dbg_level_info, L"[FPRemoveExtAttribute]Enter...\n");

    //
    //Advance beyond command and pad byte
    //
    afpRequest.advance_pos_ptr(sizeof(int16));

    const auto afpVolID = afpRequest.pull_num<UINT16>();
    const uint32 afpDirID = afpRequest.pull_num<UINT32>();
    afpRequest.advance_pos_ptr(sizeof(UINT16)); // bitmap
    const int8 afpPathtype = afpRequest.pull_num<INT8>();

    //
    //See if the user has the volume open and get a pointer to
    //the volume object.
    //
    const auto volume = connection->get_afp_session()->find_open_volume(afpVolID);
    if (volume == nullptr)
    {
        DBGWRITE(dbg_level_info, L"[FPRemoveExtAttribute]User does not have volume open!\n");
        return afpParmErr;
    }

    if (volume->is_read_only())
    {
        return afpVolLocked;
    }

    //
    //Get the pathname of the object we're working on.
    //
    auto afpError = afpRequest.get_pascal_string(afpPathname, sizeof(afpPathname), true, afpPathtype);

    if (AFP_FAILED(afpError))
    {
        DBGWRITE(dbg_level_info, L"[FPRemoveExtAttribute]Could not retrieve pathname! (%d)\n", afpError);
        return (afpError);
    }

    //
    //Set the entry object that will point to the object we're working with.
    //
    afpError = AFPIndexFindEntryWithVolIDAndPath(
        afpVolID,
        afpDirID,
        afpPathname,
        &afpEntry
    );

    if (AFP_FAILED(afpError))
    {
        DBGWRITE(dbg_level_info, L"[FPRemoveExtAttribute]Object not found! (name = %s, dirID = %d)\n", afpPathname,
                 afpDirID);
        return (afpError);
    }

    //
    //A pad byte is inserted to make the buffer end on an
    //even boundary.
    //
    afpRequest.advance_if_odd();

    //
    //Now get the string. Note that on return afpNameLen now contains the actual
    //length of the converted string.
    //
    afpNameLen = afpRequest.pull_num<UINT16>();
    afpError = afpRequest.get_unicode_string(afpAttrName, sizeof(afpAttrName), &afpNameLen);

    if (AFP_FAILED(afpError))
    {
        DBGWRITE(dbg_level_info, L"[FPRemoveExtAttribute]Could not retrieve attribute name! (%d)\n", afpError);
        return (afpError);
    }

    DBGWRITE(dbg_level_info, L"[FPRemoveExtAttribute]Removing attr, file=%s, attr=%s\n", afpPathname, afpAttrName);

    StringCbPrintf(afpPathname, sizeof(afpPathname), L"%s:%s", afpEntry->sz_path.c_str(), afpAttrName);

    if (!afp_impersonate_user(connection->get_user_token()))
    {
        //
        //Impersonation failed, don't allow any further operations.
        //
        return (afpAccessDenied);
    }

    if (!DeleteFile(afpPathname))
    {
        DBGWRITE(dbg_level_info,
                 L"[FPRemoveExtAttribute]DeleteFile failed on %s! (%d)\n",
                 afpEntry->sz_path.c_str(),
                 GetLastError()
        );

        afpError = afp_map_win_error_to_afp(GetLastError(), afpParmErr);
    }

    afp_revert_to_self(connection->get_user_token());

    DBGWRITE(dbg_level_info, L"[FPRemoveExtAttribute]Returning %d\n", afpError);

    return (afpError);
}


/*
 * GetStreamLen()
 *
 * Description:
 *		Returns the length in bytes of a particular stream.
 *
 * NOTE: Massively lame as Windows didn't provide a way to get a specific
 *		 stream length until Vista (Longhorn).
 *
 * Returns: size or BAD_LENGTH_64 if failed
 */

LONGLONG GetStreamLen(
    IN PWCHAR pathname,
    IN PWCHAR streamName
)
{
    std::vector<afp_attr_info> streams;

    if (AFP_SUCCESS(GetStreams(pathname, streams)))
    {
        for (auto it = streams.begin(); it < streams.end(); ++it)
        {
            if (!lstrcmp(it->stream_name, streamName))
            {
                return it->stream_size.QuadPart;
            }
        }

        DBGWRITE(dbg_level_info, L"[afpxattr:GetStreamLen]Stream name not found!\n");
    }

    return BAD_LENGTH_64;
}


/*
 * GetStreams()
 *
 * Description:
 *		Populate an AFPArray with the names and header info of all the streams
 *		for the file.
 *
 *		NOTE: Make sure the AFPArray is initialized to contain element sizes
 *		equal to sizeof(AFP_ATTR_INFO).
 *
 * Returns: afpError code
 */

AFPERROR GetStreams(
    IN WCHAR* pathname,
    IN std::vector<afp_attr_info>& streams
)
{
    afp_attr_info afpAttrInfo;
    PFILE_STREAM_INFORMATION pStreamInfo = nullptr;
    PFILE_STREAM_INFORMATION pSavePtr;
    NTQUERYINFORMATIONFILE NtQueryInformationFile = nullptr;
    NTSTATUS status;
    DWORD dwInfoBlockSize = 0;

    //
    //First load the proc for our undocumented API.
    //
    const HMODULE nt_dll = GetModuleHandle(L"ntdll.dll");
    if (nt_dll != nullptr)
    {
        reinterpret_cast<FARPROC&>(NtQueryInformationFile) = GetProcAddress(  // NOLINT(clang-diagnostic-undefined-reinterpret-cast)
            nt_dll,
            "NtQueryInformationFile"
        );
        if (NtQueryInformationFile == nullptr)
        {
            DBGWRITE(dbg_level_info, L"[afpxattr:GetStreams]GetProcAddress() failed! (%d)\n", GetLastError());
            return afpParmErr;
        }
    }
    else
    {
        return afpMiscErr;
    }

    //
    //Open a generic handle to the file object by opening it data fork.
    //
    auto hFile = CreateFile(
        pathname,
        0,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr
    );

    if (hFile == INVALID_HANDLE_VALUE)
    {
        //
        //The file was not found, return error.
        //
        return afpObjectNotFound;
    }

    //
    //Since we don't know how many bytes NtQueryInformationFile needs, we
    //just increment our allocation by 16K until its enough.
    //
    do
    {
        IO_STATUS_BLOCK ioStatus;

        dwInfoBlockSize += 4 * 1024;

        delete [] pStreamInfo;

        pStreamInfo = reinterpret_cast<PFILE_STREAM_INFORMATION>(new BYTE[dwInfoBlockSize]);

        pSavePtr = pStreamInfo;
        pStreamInfo->StreamNameLength = 0;
        status = NtQueryInformationFile(
            hFile,
            &ioStatus,
            static_cast<LPVOID>(pStreamInfo),
            dwInfoBlockSize,
            FileStreamInformation
        );
    }
    while (status == STATUS_BUFFER_OVERFLOW);

    //
    //We don't need the hFile anymore
    //
    CloseHandle(hFile);

    for (;;)
    {
        LARGE_INTEGER streamSize;
        DWORD i = 0;

        memset(&afpAttrInfo, 0, sizeof(afp_attr_info));

        //
        //If this is a directory, it may have no streams, so we just exit.
        //
        if (pStreamInfo->StreamNameLength == 0)
        {
            break;
        }

        //
        //For non-named streams, we do anything.
        //
        if (pStreamInfo->StreamNameLength > 1)
        {
            WCHAR path[MAX_PATH];
            //
            //Copy the stream name, may not be null terminated!
            //
            while (pStreamInfo->StreamName[i + 1] != ':')
            {
                afpAttrInfo.stream_name[i] = pStreamInfo->StreamName[i + 1];
                i++;
            }

            //
            //Create the full path including the stream name
            //
            StringCbPrintf(path, sizeof(path), L"%s:%s", pathname, afpAttrInfo.stream_name);

            //
            //Get the stream size.
            //
            hFile = CreateFile(
                path,
                0,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr,
                OPEN_EXISTING,
                0,
                nullptr
            );

            if (hFile != INVALID_HANDLE_VALUE)
            {
                if (!GetFileSizeEx(hFile, &streamSize))
                {
                    DBGWRITE(dbg_level_info, L"[afpxattr:GetStreams]GetFileSizeEx() failed (%d)\n", GetLastError());
                }

                CloseHandle(hFile);

                afpAttrInfo.stream_size = streamSize;
            }

            //
            //Finally, add the new attr info block to the array.
            //
            streams.push_back(afpAttrInfo);
        }

        //
        //If there's no more streams, then break out of the loop.
        //
        if (pStreamInfo->NextEntryOffset == 0)
        {
            break;
        }

        pStreamInfo = reinterpret_cast<PFILE_STREAM_INFORMATION>(reinterpret_cast<LPBYTE>(pStreamInfo) + pStreamInfo->
            NextEntryOffset);
    }

    delete [] pSavePtr;

    return AFP_OK;
}
