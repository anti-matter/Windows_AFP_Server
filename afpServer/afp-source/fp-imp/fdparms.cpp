#include "StdAfx.h"
#include "afp.h"
#include "afpdebug.h"
#include "afphelp.h"
#include "fdparms.h"
#include "fpinfo.h"
#include "afpxattr.h"
#include "afp_open_file.h"
#include "afp_volumes.h"

#include "winerror.h"

extern std::unique_ptr<afp_volumes> g_volumes;


size_t afp_get_parms_buffer_size_required(
    IN const PFILEDIRPARMS file_dir_parms
)
{
    size_t w_size = 0;
    WORD bitmap = (file_dir_parms->bFDPAttributes & kFileDirIsDir) ? file_dir_parms->wDirBitmap : file_dir_parms->wFileBitmap;

    static BYTE fd_parms_sizes[] = {
        sizeof(WORD), //Attributes
        sizeof(DWORD), //Parent DirID
        sizeof(LONG), //Creation date
        sizeof(LONG), //Mod date
        sizeof(LONG), //Backup date
        sizeof(FINDER_INFO), //Finder info
        sizeof(WORD) + sizeof(BYTE), //Long name offset
        sizeof(WORD) + sizeof(BYTE), //Short name offset
        sizeof(DWORD), //File/Dir ID
        sizeof(DWORD), //Data fork len/offspring count
        sizeof(DWORD), //Res fork len/owner id
        sizeof(DWORD), //Group id
        sizeof(DWORD), //Access rights
        PRODOS_LEN //ProDOS info
    };

    if (bitmap & kFPLongName)
    {
        w_size += strlen(file_dir_parms->parms.fp.longName);
    }

    if (bitmap & kFPShortName)
    {
        w_size += strlen(file_dir_parms->parms.fp.shortName);
    }

    if ((file_dir_parms->bFDPAttributes & kFileDirIsDir) && (bitmap & kFPDirOffCount))
    {
        w_size -= sizeof(WORD);
    }

    for (WORD i = 0; bitmap; i++)
    {
        if (bitmap & 1)
        {
            w_size += static_cast<WORD>(fd_parms_sizes[i]);
        }

        bitmap >>= 1;
    }

    return w_size;
}

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
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND)
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
                DBGWRITE(dbg_level_info,
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
    IN const UINT32 dir_id,
    IN const dsi::dsi_connection* dsi_session,
    IN afp_index* entry,
    IN WIN32_FILE_ATTRIBUTE_DATA win_attributes
)
{
    AFPCHARMAPARRAY char_map{};
    LARGE_INTEGER data_size64{};
    LARGE_INTEGER rsrc_size64{};
    UINT16* long_name_offset = nullptr;
    UINT16* uni_name_offset = nullptr;
    const char* parms_start = reply.get_current_pos_ptr();

    if (bitmap & kFPFileAttributes)
    {
        UINT16 attributes = 0;

        AFPWinToAFPAttributes(win_attributes.dwFileAttributes, &attributes);

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
        FINDER_INFO finder_info;

        RtlZeroMemory(&finder_info, sizeof(FINDER_INFO));

        if (AFP_SUCCESS(AFPGetFinderInfo(entry, &finder_info)))
        {
            if (win_attributes.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)
            {
                finder_info.fdFlags |= kFDInvisible;
            }

            finder_info.fdFlags = htons(finder_info.fdFlags);
            finder_info.fdLocation.x = static_cast<SHORT>(htons(finder_info.fdLocation.x));
            finder_info.fdLocation.y = static_cast<SHORT>(htons(finder_info.fdLocation.y));
            finder_info.fdFldr = static_cast<SHORT>(htons(finder_info.fdFldr));

            reply.push_raw_data(&finder_info, sizeof(FINDER_INFO));
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
                (lstrlen(domain_name) > 0) ? domain_name : nullptr,
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
                DBGWRITE(dbg_level_warning, L"[FDP]GetCompressedFileSize() failed! (%d)\n", GetLastError());
            }
            else
            {
                DBGWRITE(dbg_level_all, L"[FDP]Data fork size =  (%d)\n", data_size64.LowPart);
            }

            if (bitmap & kFPDFLen)
            {
                reply.push_num<UINT32>(data_size64.LowPart);
            }
        }

        if (bitmap & (kFPRFLen | kFPExtRsrcForkLen))
        {
            afp_open_file afp_file(volume_id, dir_id, kReadMode, kAttrFork, entry, entry->sz_path.c_str());

            rsrc_size64.QuadPart = afp_file.get_stream_length(const_cast<wchar_t*>(AFP_RESOURCE_FORK_NAME_PLAIN));

            if (rsrc_size64.QuadPart == BAD_LENGTH_64)
            {
                DBGWRITE(dbg_level_warning, L"[FDP]GetStreamLength() failed! (%d) (%s)\n", GetLastError(), entry->sz_object_name.c_str());
                rsrc_size64.QuadPart = 0;
            }
            else
            {
                DBGWRITE(dbg_level_all, L"[FDP]Resource fork size =  (%d)\n", rsrc_size64.LowPart);
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

    if ((bitmap & kFPLongName) || (bitmap & kFPUnicodeName))
    {
        AFPGetCharMapForIndexEntry(entry, char_map);

        if (bitmap & kFPLongName)
        {
            *long_name_offset = htons(static_cast<uint16>(reply.get_current_pos_ptr() - parms_start));
            reply.push_pascal_string(entry->sz_object_name, char_map);
        }

        if (bitmap & kFPUnicodeName)
        {
            *uni_name_offset = htons(static_cast<uint16>(reply.get_current_pos_ptr() - parms_start));
            reply.push_unicode_string(entry->sz_object_name, char_map);
        }
    }

    return AFP_OK;
}

//---------------------------------------------------------------------
// AFPSetFileDirParms()
//---------------------------------------------------------------------
// Worker for the FPSetFileDirParms API. The caller is responsible
// for verifying the bitmap.
//
AFPERROR AFPSetFileDirParms(
    IN WORD wBitmap,
    IN afp_index* pEntry,
    IN PCHAR pParms,
    IN DWORD dwFileAttributes
)
{
    PCHAR pTemp = pParms;
    DWORD dwError = NO_ERROR;
    ULONG lwCreateDate = 0;
    ULONG lwModDate = 0;
    ULONG lwBackupDate = 0;
    DWORD dwAttributes = dwFileAttributes;
    WORD wAFPAttributes = 0;

    //
    //Change the file's attributes, we only allow some to be
    //changed as per the spec.
    //
    if (wBitmap & kFPFileAttributes)
    {
        EXTRACT_WORD(pTemp, wAFPAttributes);

        if (wAFPAttributes & kFileSetClearFlag)
        {
            if (wAFPAttributes & kFileInvisible)
                dwAttributes |= FILE_ATTRIBUTE_HIDDEN;
            if (wAFPAttributes & kFileSystem)
                dwAttributes |= FILE_ATTRIBUTE_SYSTEM;
            if (wAFPAttributes & (kFileReadOnly | kFileReadOnly | kFileDeleteInhibit))
                dwAttributes |= FILE_ATTRIBUTE_READONLY;
            if (wAFPAttributes & kFileNeedsBackup)
                dwAttributes |= FILE_ATTRIBUTE_ARCHIVE;
        }
        else
        {
            if (wAFPAttributes & kFileInvisible)
                dwAttributes &= ~FILE_ATTRIBUTE_HIDDEN;
            if (wAFPAttributes & kFileSystem)
                dwAttributes &= ~FILE_ATTRIBUTE_SYSTEM;
            if (wAFPAttributes & (kFileReadOnly | kFileReadOnly | kFileDeleteInhibit))
                dwAttributes &= ~FILE_ATTRIBUTE_READONLY;
            if (wAFPAttributes & kFileNeedsBackup)
                dwAttributes &= ~FILE_ATTRIBUTE_ARCHIVE;
        }

        if (!SetFileAttributes(pEntry->sz_path.c_str(), dwAttributes))
        {
            dwError = GetLastError();
            DBGWRITE(dbg_level_info, L"[FDP]SetFileAttributes() failed! (%d)\n", dwError);

            return (afp_map_win_error_to_afp(dwError, static_cast<DWORD>(afpMiscErr)));
        }
    }

    if (wBitmap & (kFPCreateDate | kFPModDate | kFPBackupDate))
    {
        if (wBitmap & kFPCreateDate)
            EXTRACT_LONG(pTemp, lwCreateDate)
        if (wBitmap & kFPModDate)
            EXTRACT_LONG(pTemp, lwModDate)
        if (wBitmap & kFPBackupDate)
            EXTRACT_LONG(pTemp, lwBackupDate)
    }

    if (wBitmap & kFPFinderInfo)
    {
        FINDER_INFO fInfo;

        RtlCopyMemory(&fInfo, pTemp, sizeof(FINDER_INFO));
        pTemp += sizeof(FINDER_INFO);

        fInfo.fdFlags = ntohs(fInfo.fdFlags);
        fInfo.fdLocation.x = static_cast<SHORT>(ntohs(fInfo.fdLocation.x));
        fInfo.fdLocation.y = static_cast<SHORT>(ntohs(fInfo.fdLocation.y));
        fInfo.fdFldr = static_cast<SHORT>(ntohs(fInfo.fdFldr));

        AFPSetFinderInfo(pEntry, &fInfo, FALSE);
    }

    if (wBitmap & kFPProDos)
    {
    }

    //
    //Now handle the dir specific cases.
    //
    if (dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
        if (wBitmap & kFPDirOwnerID)
        {
        }

        if (wBitmap & kFPDirGroupID)
        {
        }

        if (wBitmap & kFPDirAccess)
        {
        }
    }

    //
    //We wait till the very end to do this since many things above will
    //change the modification date of an object.
    //
    if (wBitmap & (kFPCreateDate | kFPModDate | kFPBackupDate))
    {
        AFPSetFileTime(
            const_cast<wchar_t*>(pEntry->sz_path.c_str()),
            (dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? TRUE : FALSE,
            (lwCreateDate == 0) ? nullptr : &lwCreateDate,
            (lwModDate == 0) ? nullptr : &lwModDate,
            (lwBackupDate == 0) ? nullptr : &lwBackupDate
        );
    }

    return (NO_ERROR);
}

//---------------------------------------------------------------------
// AFPSetFileTime()
//---------------------------------------------------------------------
// Set the time fields of a file.
//
AFPERROR AFPSetFileTime(
    IN PTCHAR pWindowspath,
    IN BOOL fIsDirectory,
    IN PULONG plwCreation,
    IN PULONG plwModification,
    IN PULONG plwBackup
)
{
    auto hFile = INVALID_HANDLE_VALUE;
    DWORD dwError = NO_ERROR;
    PFILETIME pCreation = nullptr;
    PFILETIME pMod = nullptr;
    PFILETIME pBackup = nullptr;
    FILETIME creation, mod, backup;

    //
    //To set the file time, we have to have a handle to
    //the file object. This sucks.
    //
    if (fIsDirectory)
    {
        hFile = CreateFile(
            pWindowspath,
            FILE_GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS,
            nullptr);
    }
    else
    {
        hFile = CreateFile(
            pWindowspath,
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS,
            nullptr
        );
    }

    if (hFile == INVALID_HANDLE_VALUE)
    {
        dwError = GetLastError();

        DBGWRITE(dbg_level_info,
                 L"[FDP]CreatFile() failed in AFPSetFileTime! (%d)\n", dwError);

        switch (dwError)
        {
        case ERROR_FILE_NOT_FOUND:
            return (afpObjectNotFound);
            break;

        default:
            return (afpMiscErr);
        }
    }

    //
    //Convert all the mac times to FILETIME structures.
    //
    if (plwCreation != nullptr)
    {
        afp_mac_to_windows_time(*plwCreation, &creation);
        pCreation = &creation;
    }
    if (plwModification != nullptr)
    {
        afp_mac_to_windows_time(*plwModification, &mod);
        pMod = &mod;
    }
    if (plwBackup != nullptr)
    {
        afp_mac_to_windows_time(*plwBackup, &backup);
        pBackup = &backup;
    }

    //
    //Make the windows API call that does the dirty deed.
    //
    if (!SetFileTime(hFile, pCreation, nullptr, pMod))
    {
        DBGWRITE(dbg_level_info,
                 L"[FDP]SetFileTime() failed! (%d)\n", GetLastError());

        dwError = static_cast<DWORD>(afpMiscErr);
    }

    CloseHandle(hFile);
    return (dwError);
}

//---------------------------------------------------------------------
// AFPWinToAFPAttributes()
//---------------------------------------------------------------------
// Convert windows to AFP attributes.
//
WORD AFPWinToAFPAttributes(
    IN DWORD dwWinAttributes,
    OUT PWORD pwAFPAttributes
)
{
    WORD wAFPAttribs = 0;

    if (dwWinAttributes & FILE_ATTRIBUTE_HIDDEN)
        wAFPAttribs |= kFileInvisible;
    if (dwWinAttributes & FILE_ATTRIBUTE_SYSTEM)
        wAFPAttribs |= kFileSystem;
    if (dwWinAttributes & FILE_ATTRIBUTE_READONLY)
        wAFPAttribs |= (kFileReadOnly | kFileDeleteInhibit | kFileRenameInhibit);
    if (dwWinAttributes & FILE_ATTRIBUTE_ARCHIVE)
        wAFPAttribs |= kFileNeedsBackup;
    if (dwWinAttributes & FILE_ATTRIBUTE_SYSTEM)
        wAFPAttribs |= kFileSystem;

    if (pwAFPAttributes != nullptr)
    {
        *pwAFPAttributes = wAFPAttribs;
    }

    return (wAFPAttribs);
}

#ifdef _DEBUG
// ---------------------------------------------------------------------------
// AFPExtractDirParms()
// ---------------------------------------------------------------------------
// Extracts requested dir information from the FPFileDirParms reply block.
void AFPExtractDirParms(
    IN PBYTE pParms, //Points to actual start of data
    IN WORD wBitMap
)
{
    PBYTE UNALIGNED pTemp;
    PBYTE UNALIGNED pTemp2;
    PBYTE UNALIGNED pInputParms;
    BYTE wLen;
    CHAR name[MAX_PATH];
    DWORD dwTemp;
    WORD wTemp;

    pTemp = pParms;
    pInputParms = pParms;

    if (wBitMap & kFPDirAttribute)
    {
        EXTRACT_WORD(pTemp, wTemp);
        DBGWRITE(dbg_level_info, L"[FDP]attributes = %d\n", wTemp);
    }
    if (wBitMap & kFPDirParentID)
    {
        EXTRACT_LONG(pTemp, dwTemp);
        DBGWRITE(dbg_level_info, L"[FDP]parent id = %d\n", dwTemp);
    }
    if (wBitMap & kFPDirCreateDate)
    {
        EXTRACT_LONG(pTemp, dwTemp);
        DBGWRITE(dbg_level_info, L"[FDP]dir create date = %d\n", dwTemp);
    }
    if (wBitMap & kFPDirModDate)
    {
        EXTRACT_LONG(pTemp, dwTemp);
        DBGWRITE(dbg_level_info, L"[FDP]dir mod date = %d\n", dwTemp);
    }
    if (wBitMap & kFPDirBackupDate)
    {
        EXTRACT_LONG(pTemp, dwTemp);
        DBGWRITE(dbg_level_info, L"[FDP]dir backup date = %d\n", dwTemp);
    }
    if (wBitMap & kFPDirFinderInfo)
    {
        DBGWRITE(dbg_level_info, L"[FDP]FinderInfo:\n");
        hex_dump_buffer((PCHAR)pTemp, FINDERINFO_LEN);
        pTemp += FINDERINFO_LEN;
    }
    if (wBitMap & kFPDirLongName)
    {
        RtlZeroMemory(name, MAX_PATH);

        pTemp2 = pInputParms + ntohs(*(WORD UNALIGNED *)pTemp);
        wLen = *(pTemp2++);

        strncpy_s(name, sizeof(name), (PCHAR)pTemp2, wLen);
        pTemp += sizeof(WORD);

        DBGWRITE(dbg_level_info, L"[FDP]dir long name = %s\n", name);
    }
    if (wBitMap & kFPDirShortName)
    {
        RtlZeroMemory(name, MAX_PATH);

        pTemp2 = pInputParms + ntohs(*(WORD UNALIGNED *)pTemp);
        wLen = *(pTemp2++);

        strncpy_s(name, sizeof(name), (PCHAR)pTemp2, wLen);
        pTemp += sizeof(WORD);

        DBGWRITE(dbg_level_info, L"[FDP]dir short name = %s\n", name);
    }
    if (wBitMap & kFPDirID)
    {
        EXTRACT_LONG(pTemp, dwTemp);
        DBGWRITE(dbg_level_info, L"[FDP]dir id = %d\n", dwTemp);
    }
    if (wBitMap & kFPDirOffCount)
    {
        EXTRACT_WORD(pTemp, wTemp);
        DBGWRITE(dbg_level_info, L"[FDP]dir offspring = %d\n", wTemp);
    }
    if (wBitMap & kFPDirOwnerID)
    {
        EXTRACT_LONG(pTemp, dwTemp);
        DBGWRITE(dbg_level_info, L"[FDP]dir owner ID = %d\n", dwTemp);
    }
    if (wBitMap & kFPDirGroupID)
    {
        EXTRACT_LONG(pTemp, dwTemp);
        DBGWRITE(dbg_level_info, L"[FDP]dir group ID = %d\n", dwTemp);
    }
    if (wBitMap & kFPDirAccess)
    {
        EXTRACT_LONG(pTemp, dwTemp);
        DBGWRITE(dbg_level_info, L"[FDP]dir access = %x\n", dwTemp);
    }
}
#endif
