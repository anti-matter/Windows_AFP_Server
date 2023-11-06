// ReSharper disable CppInconsistentNaming
// ReSharper disable CppClangTidyClangDiagnosticGnuZeroVariadicMacroArguments
// ReSharper disable CppParameterMayBeConstPtrOrRef
#include "StdAfx.h"

#include "afp.h"
#include "afphelp.h"
#include "afpdebug.h"
#include "afpxattr.h"
#include "fpinfo.h"
#include "afp_session.h"
#include "afp_volumes.h"

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
    DWORD afpActualRead = 0;

    DBGWRITE(dbg_level_all, L"Enter...\n");

    //
    //Advance beyond command and pad byte
    //
    afpRequest.advance_pos_ptr(sizeof(INT16));

    // ReSharper disable CppDeclaratorNeverUsed
    const auto afpVolID = afpRequest.pull_num<INT16>();
    const auto afpDirID = afpRequest.pull_num<INT32>();
    const auto afpBitmap = afpRequest.pull_num<UINT16>();
    afpRequest.advance_pos_ptr(sizeof(INT64) * 2); // offset and req count
    const auto afpMaxReplySize = afpRequest.pull_num<INT32>();
    const auto afpPathtype = afpRequest.pull_num<UINT8>();
    // ReSharper restore CppDeclaratorNeverUsed

    //
    //See if the user has the volume open and get a pointer to
    //the volume object.
    //	
    if (!connection->get_afp_session()->session_has_volume_open(afpVolID))
    {
        DBGWRITE(dbg_level_warning, L"User does not have volume open!\n");
        return afpParmErr;
    }

    //
    //Get the pathname of the object we're working on.
    //
    std::filesystem::path afpPathname = afpRequest.pull_path(afpPathtype);
    if (afpPathname.empty())
    {
        DBGWRITE(dbg_level_warning, L"Could not retrieve pathname! (%d)\n");
        return afpParmErr;
    }

    //
    //Advance if we need to in order to land on an even boundary
    //
    afpRequest.advance_if_odd();

    //
    //Now get the string. Note that on return afpNameLen now contains the actual
    //length of the converted string.
    //
    const auto afpAttrName = afpRequest.pull_path(kUnicodeNames);
    if (afpAttrName.empty())
    {
        DBGWRITE(dbg_level_warning, L"Could not retrieve attribute name! (%d)\n");
        return afpParmErr;
    }

    DBGWRITE(dbg_level_all, L"Getting attr, file=%s, attr=%s\n", afpPathname.c_str(), afpAttrName.c_str());

    //
    //Set the entry object that will point to the object we're working with.
    //
    const afp_index* afpEntry = afp_volumes::current()->find(afpVolID)->find(afpDirID, afpPathname);
    if (afpEntry != nullptr)
    {
        //
        //Get the size of the attribute data. Note we don't pass the name of the
        //stream because we already have an open handle to the right stream.
        //
        const auto afpDataLen = static_cast<UINT32>(GetStreamLen(afpEntry->sz_path.c_str(), afpAttrName.c_str()));
        if (afpDataLen != BAD_LENGTH_32)
        {
            //
            //Plum the first 2 items in the reply block. These will always be
            //returned in every case beyond here.
            //
            afpReply.push_num(afpBitmap);
            afpReply.push_num(afpDataLen);

            DBGWRITE(dbg_level_all, L"DataLen == %d\n", afpDataLen);

            if (afpMaxReplySize > 0)
            {
                if (afpReply.check_space_needed(afpDataLen))
                {
                    afpPathname = afpEntry->sz_path;
                    afpPathname.concat(":");
                    afpPathname.concat(afpAttrName);

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
                        afpPathname.c_str(),
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
                    DBGWRITE(dbg_level_warning, L"Not enough space in reply buffer!\n");
                    ASSERT(0);

                    return (afpMiscErr);
                }
            }
        }
        else
        {
            DBGWRITE(dbg_level_warning, L"GetStreamLen() failed!\n");
            return afpMiscErr;
        }
    }
    else
    {
        DBGWRITE(
            dbg_level_warning,
            L"Object not found! (name = %s, dirID = %d)\n",
            afpPathname.c_str(),
            afpDirID
        );
    }

    return AFP_OK;
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

    DWORD bytesWritten = 0;

    DBGWRITE(dbg_level_info, L"[afp:FPSetExtAttribute]Enter...\n");

    //
    //Advance beyond command and pad byte
    //
    afpRequest.advance_pos_ptr(sizeof(INT16));

    const auto afpVolID = afpRequest.pull_num<INT16>();
    const auto afpDirID = afpRequest.pull_num<INT32>();
    const auto afpBitmap = afpRequest.pull_num<UINT16>();
    afpRequest.advance_pos_ptr(sizeof(INT64)); //offset
    const auto afpPathtype = afpRequest.pull_num<UINT8>();

    //
    //Get the pathname of the object we're working on.
    //
    std::filesystem::path afpPathname = afpRequest.pull_path(afpPathtype);
    if (afpPathname.empty())
    {
        DBGWRITE(dbg_level_info, L"[afp:FPSetExtAttribute]Could not retrieve pathname! (%d)\n");
        return afpParmErr;
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
    const auto afpAttrName = afpRequest.pull_string(kUnicodeNames);

    if (afpAttrName.empty())
    {
        DBGWRITE(dbg_level_info, L"[afp:FPSetExtAttribute]Could not retrieve attribute name! (%d)\n");
        return afpParmErr;
    }

    DBGWRITE(dbg_level_info, L"[afp:FPSetExtAttribute]Setting attr, file=%s, attr=%s\n", afpPathname.c_str(), afpAttrName.c_str());

    //
    //The attribute data length we're going to write.
    //
    const UINT32 afpAttrDataLen = afpRequest.pull_num<UINT32>();

    //
    //See if the user has the volume open and get a pointer to
    //the volume object.
    //
    const auto volume = connection->get_afp_session()->find_open_volume(afpVolID);
    if (volume == nullptr)
    {
        DBGWRITE(dbg_level_info, L"[afp:FPSetExtAttribute]User does not have volume open!\n");
        return afpParmErr;
    }

    //
    //Set the entry object that will point to the object we're working with.
    //
    const auto afpEntry = volume->find(afpDirID, afpPathname);
    if (afpEntry != nullptr)
    {
        //
        //We have different errors to check for based on the supplied attribute
        //flags (replace and/or already exists).
        //
        const auto status = static_cast<UINT32>(GetStreamLen(const_cast<wchar_t*>(afpEntry->sz_path.c_str()), afpAttrName.c_str()));

        if ((status == BAD_LENGTH_32) && (afpBitmap & kXAttrCreate))
        {
            return afpObjectExists;
        }
        if (afpBitmap & kXAttrReplace)
        {
            return afpObjectNotFound;
        }

        afpPathname = afpEntry->sz_path;
        afpPathname.concat(":");
        afpPathname.concat(afpAttrName);

        if (!afp_impersonate_user(connection->get_user_token()))
        {
            //
            //Impersonation failed, don't allow any further operations.
            //
            return afpAccessDenied;
        }

        //
        //Open a handle to the file stream object.
        //
        const auto hFile = CreateFile(
            afpPathname.c_str(),
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
                L"WriteFile() returned: %d\n",
                GetLastError()
            );
        }

        afp_revert_to_self(connection->get_user_token());

        //
        //As a last check, make sure the right amount of bytes were
        //written out to the attribute.
        //
        if (bytesWritten != afpAttrDataLen)
        {
            DBGWRITE(dbg_level_info, L"WriteFork() wrote out the wrong number of bytes!\n");
            return afpMiscErr;
        }
    }

    DBGWRITE(dbg_level_info, L"Wrote(bytes): %d\n", bytesWritten);

    return AFP_OK;
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
    INT32 afpDataLen = 0;

    DBGWRITE(dbg_level_info, L"Enter...\n");

    //
    //Advance beyond command and pad byte
    //
    afpRequest.advance_pos_ptr(sizeof(INT16));

    const auto afpVolID = afpRequest.pull_num<INT16>();
    const auto afpDirID = afpRequest.pull_num<INT32>();
    const auto afpBitmap = afpRequest.pull_num<UINT16>();
    afpRequest.advance_pos_ptr(sizeof(UINT16) + sizeof(UINT32)); // req count and start index
    const auto afpMaxReplySize = afpRequest.pull_num<INT32>();
    const auto afpPathtype = afpRequest.pull_num<UINT8>();

    //
    //See if the user has the volume open and get a pointer to
    //the volume object.
    //
    const auto volume = connection->get_afp_session()->find_open_volume(afpVolID);
    if (volume == nullptr)
    {
        DBGWRITE(dbg_level_info, L"User does not have volume open!\n");
        return afpParmErr;
    }

    //
    //Get the pathname of the object we're working on.
    //
    const auto afpPathname = afpRequest.pull_string(afpPathtype);
    if (afpPathname.empty())
    {
        DBGWRITE(dbg_level_info, L"Could not retrieve pathname! (%d)\n");
        return afpParmErr;
    }

    AFPERROR afpError = AFP_OK;

    //
    //Set the entry object that will point to the object we're working with.
    //
    const auto afpEntry = volume->find(afpDirID, afpPathname);
    if (afpEntry != nullptr)
    {
        std::vector<afp_attr_info> streamArray;

        DBGWRITE(
            dbg_level_info,
            L"Getting stream list for %s\n", afpEntry->sz_path.c_str());

        if (!afp_impersonate_user(connection->get_user_token()))
        {
            //
            //Impersonation failed, don't allow any further operations.
            //
            return (afpAccessDenied);
        }

        afpError = GetStreams(afpEntry->sz_path.c_str(), streamArray);

        if (AFP_SUCCESS(afpError))
        {
            //
            //The bitmap is always returned to the caller.
            //
            afpReply.push_num(afpBitmap);

            //
            //Save the position of the data len field...
            //
            auto* saveDL = reinterpret_cast<UINT32*>(afpReply.get_current_pos_ptr());
            afpReply.advance_pos_ptr(sizeof(UINT32));

            //
            //Now get the size of all the name strings and optionally plumb
            //them into the reply buffer.
            //

            for (const auto& [stream_size, stream_name] : streamArray)
            {
                const auto cbStreamName = static_cast<INT16>(lstrlen(stream_name));

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
                                L"New attr name added: %s\n",
                                    stream_name);


                            const auto ansi_string = wide_string_to_string(stream_name);
                            afpReply.push_raw_data(ansi_string.data(), ansi_string.size() + sizeof(char));
                        }
                        else
                        {
                            //
                            //Our buffer will grow larger than the client can handle. Stop
                            //including new names, adjust the reported data size and return.
                            //
                            DBGWRITE(dbg_level_info, L"Data len exceeds client buffer size! (%d)\n",
                                     afpMaxReplySize);

                            afpDataLen -= cbStreamName + 1;
                            break;
                        }
                    }
                }
            }

            //
            //If we're only returning the size, we need to include the size of
            //the bitmap and data len parameters.
            //
            if (afpMaxReplySize == 0)
            {
                afpDataLen += (sizeof(INT16) + sizeof(INT32));
            }

            //
            //Set the data length field now that we've calculated it completely.
            //
            *saveDL = htonl(afpDataLen);
        }

        afp_revert_to_self(connection->get_user_token());
    }

    DBGWRITE(dbg_level_info, L"Name list datalen = %d\n", afpDataLen);

    return afpError;
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
    DBGWRITE(dbg_level_info, L"Enter...\n");

    //
    //Advance beyond command and pad byte
    //
    afpRequest.advance_pos_ptr(sizeof(INT16));

    const auto afpVolID = afpRequest.pull_num<INT16>();
    const auto afpDirID = afpRequest.pull_num<INT32>();
    afpRequest.advance_pos_ptr(sizeof(UINT16)); // bitmap
    const auto afpPathtype = afpRequest.pull_num<UINT8>();

    //
    //See if the user has the volume open and get a pointer to
    //the volume object.
    //
    const auto volume = connection->get_afp_session()->find_open_volume(afpVolID);
    if (volume == nullptr)
    {
        DBGWRITE(dbg_level_info, L"User does not have volume open!\n");
        return afpParmErr;
    }

    if (volume->is_read_only())
    {
        return afpVolLocked;
    }

    //
    //Get the pathname of the object we're working on.
    //
    std::filesystem::path afpPathname = afpRequest.pull_path(afpPathtype);
    if (afpPathname.empty())
    {
        DBGWRITE(dbg_level_info, L"Could not retrieve pathname!\n");
        return afpParmErr;
    }

    //
    //Set the entry object that will point to the object we're working with.
    //
    const auto afpEntry = volume->find(afpDirID, afpPathname);
    if (afpEntry == nullptr)
    {
        DBGWRITE(dbg_level_info, L"Object not found! (name = %s, dirID = %d)\n", afpPathname.c_str(), afpDirID);
        return afpParmErr;
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
    const auto afpAttrName = afpRequest.pull_string(kUnicodeNames);
    if (afpAttrName.empty())
    {
        DBGWRITE(dbg_level_info, L"Could not retrieve attribute name!\n");
        return afpParmErr;
    }

    DBGWRITE(dbg_level_info, L"Removing attr, file=%s, attr=%s\n", afpPathname.c_str(), afpAttrName.c_str());

    afpPathname = afpEntry->sz_path;
    afpPathname.concat(":");
    afpPathname.concat(afpAttrName);

    AFPERROR afpError = afpMiscErr;

    if (!afp_impersonate_user(connection->get_user_token()))
    {
        //
        //Impersonation failed, don't allow any further operations.
        //
        return afpAccessDenied;
    }

    if (!DeleteFile(afpPathname.c_str()))
    {
        DBGWRITE(dbg_level_info,
                 L"DeleteFile failed on %s! (%d)\n",
                 afpEntry->sz_path.c_str(),
                 GetLastError()
        );

        afpError = afp_map_win_error_to_afp(GetLastError(), afpParmErr);
    }

    afp_revert_to_self(connection->get_user_token());

    DBGWRITE(dbg_level_info, L"Returning %d\n", afpError);

    return afpError;
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
    IN const WCHAR* pathname,
    IN const WCHAR* streamName
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

        DBGWRITE(dbg_level_info, L"Stream name not found!\n");
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
    IN const WCHAR* pathname,
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
