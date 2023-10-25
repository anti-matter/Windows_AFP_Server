#include "StdAfx.h"
#include "afp.h"
#include "afpdebug.h"
#include "afphelp.h"
#include "fdparms.h"
#include "fpinfo.h"
#include "afpxattr.h"
#include "afp_session.h"
#include "afp_open_file.h"
#include "afp_volumes.h"

#include "winerror.h"

extern std::unique_ptr<afp_volumes> g_volumes;


//---------------------------------------------------------------------
// AFPGetParmsBufferSize()
//---------------------------------------------------------------------
// Returns how big a buffer is needed to pack all the requested
// parameters.
//
size_t AFPGetParmsBufferSizeRequired(
    IN PFILEDIRPARMS pFdp
)
{
    WORD i;
    size_t wSize = 0;
    WORD wBitmap = (pFdp->bFDPAttributes & kFileDirIsDir) ? pFdp->wDirBitmap : pFdp->wFileBitmap;

    static BYTE fdParmSizes[14] = {
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

    if (wBitmap & kFPLongName)
    {
        wSize += strlen(pFdp->parms.fp.longName);
    }

    if (wBitmap & kFPShortName)
    {
        wSize += strlen(pFdp->parms.fp.shortName);
    }

    if ((pFdp->bFDPAttributes & kFileDirIsDir) && (wBitmap & kFPDirOffCount))
    {
        wSize -= sizeof(WORD);
    }

    for (i = 0; wBitmap; i++)
    {
        if (wBitmap & 1)
        {
            wSize += static_cast<WORD>(fdParmSizes[i]);
        }

        wBitmap >>= 1;
    }

    return (wSize);
}

//---------------------------------------------------------------------
// AFPCheckForAndCreateFinderRsrcStream)
//---------------------------------------------------------------------
// Check to see if a file object has a resource stream. If not, add
// it using defaults.
//
void AFPCheckForAndCreateFinderRsrcStream(
    IN const afp_index* index_entry
)
{
    const std::filesystem::path res_path = index_entry->sz_path / AFP_RESOURCE_FORK_NAME;

    HANDLE hFile = CreateFile(
        res_path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hFile == INVALID_HANDLE_VALUE)
    {
        if (GetLastError() == ERROR_FILE_NOT_FOUND)
        {
            hFile = CreateFile(
                res_path.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,
                CREATE_NEW,
                FILE_ATTRIBUTE_NORMAL,
                nullptr
            );

            if (hFile != INVALID_HANDLE_VALUE)
            {
                DBGWRITE(dbg_level_all,
                    L"Successfully created new resource stream (%s)\n",
                    res_path.c_str()
                );

                CloseHandle(hFile);
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
        CloseHandle(hFile);
    }
}

//---------------------------------------------------------------------
// AFPGetObjectParms()
//---------------------------------------------------------------------
// Fill in the parms of a file or dir for a file/dir parms call.
//
AFPERROR AFPGetObjectParms(
    IN afp_net_buffer& afpReply,
    IN uint16 afpBitmap,
    IN uint16 afpVolumeID,
    IN UINT32 afpDirID,
    IN const dsi::dsi_connection* afpSession,
    IN afp_index* afpEntry,
    IN WIN32_FILE_ATTRIBUTE_DATA winAttributes
)
{
    AFPCHARMAPARRAY charMap = {0};
    LARGE_INTEGER dataSize64 = {0};
    LARGE_INTEGER rsrcSize64 = {0};
    uint16* longNameOffset = nullptr;
    uint16* uniNameOffset = nullptr;
    char* pParmsStart = afpReply.get_current_pos_ptr();
    AFPERROR afpError = AFP_OK;

    if (afpBitmap & kFPFileAttributes)
    {
        uint16 afpAttributes = 0;

        AFPWinToAFPAttributes(winAttributes.dwFileAttributes, &afpAttributes);

        //
        //For files, there are a couple flags that Windows doesn't have.
        //
        if ((winAttributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
            if (afpEntry->flags & flag_index_resource_fork_open)
            {
                afpAttributes |= kFileRsrcForkOpen;
            }

            if (afpEntry->flags & flag_index_data_fork_open)
            {
                afpAttributes |= kFileDataForkOpen;
            }
        }

        afpReply.push_num<UINT16>(afpAttributes);
    }

    if (afpBitmap & kFPParentID) afpReply.push_num<UINT32>(afpEntry->par_id);
    if (afpBitmap & kFPCreateDate) afpReply.push_num<UINT32>(afp_windows_to_mac_time(&winAttributes.ftCreationTime));
    if (afpBitmap & kFPModDate) afpReply.push_num<UINT32>(afp_windows_to_mac_time(&winAttributes.ftLastWriteTime));
    if (afpBitmap & kFPBackupDate) afpReply.push_num<UINT32>(0x80000000);

    if (afpBitmap & kFPFinderInfo)
    {
        FINDER_INFO finderInfo;

        RtlZeroMemory(&finderInfo, sizeof(FINDER_INFO));

        if (AFP_SUCCESS(AFPGetFinderInfo(afpEntry, &finderInfo)))
        {
            if (winAttributes.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)
            {
                finderInfo.fdFlags |= kFDInvisible;
            }

            finderInfo.fdFlags = htons(finderInfo.fdFlags);
            finderInfo.fdLocation.x = htons(finderInfo.fdLocation.x);
            finderInfo.fdLocation.y = htons(finderInfo.fdLocation.y);
            finderInfo.fdFldr = htons(finderInfo.fdFldr);

            afpReply.push_raw_data(&finderInfo, sizeof(FINDER_INFO));
        }
    }

    if (afpBitmap & kFPLongName)
    {
        longNameOffset = (uint16*)afpReply.get_current_pos_ptr();
        afpReply.advance_pos_ptr(sizeof(int16));
    }

    if (afpBitmap & kFPFileNum) afpReply.push_num<UINT32>(afpEntry->object_id);

    //
    //The maps are a little different here for file and directories.
    //
    if (winAttributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
        if (afpBitmap & kFPDirOffCount)
        {
            const auto offspring = static_cast<UINT16>(g_volumes
                ->find(afpVolumeID)
                ->get_index()
                ->get_offspring(afpEntry->object_id, nullptr));
            afpReply.push_num<UINT16>(offspring);
        }

        if (afpBitmap & (kFPDirOwnerID | kFPDirGroupID))
        {
            DWORD dwOwnerID = 0;
            DWORD dwGroupID = 0;

            //AFPGetDirectoryOwnerAndGroupID(afpEntry->szPath, &dwOwnerID, &dwGroupID);

            if (afpBitmap & kFPDirOwnerID)
            {
                afpReply.push_num<UINT32>(dwOwnerID);
            }

            if (afpBitmap & kFPDirGroupID)
            {
                afpReply.push_num<UINT32>(dwGroupID);
            }
        }

        if (afpBitmap & kFPDirAccess)
        {
            uint32 afpRights = 0;

            const auto user_name = _wcsdup(afpSession->get_user_name().c_str());
            const auto domain_name = _wcsdup(afpSession->get_domain_name().c_str());

            const auto win_error = afp_get_directory_rights(
                afpSession->get_user_token(),
                const_cast<wchar_t*>(afpEntry->sz_path.c_str()),
                user_name,
                (lstrlen(domain_name) > 0) ? domain_name : nullptr,
                &afpRights
            );

            if (SUCCEEDED(win_error))
            {
                afpReply.push_num<UINT32>(afpRights);
            }

            free(user_name);
            free(domain_name);
        }

        if (afpBitmap & kFPUnicodeName)
        {
            uniNameOffset = reinterpret_cast<UINT16*>(afpReply.get_current_pos_ptr());
            afpReply.advance_pos_ptr(sizeof(UINT16));
        }
    }
    else
    {
        if (afpBitmap & (kFPDFLen | kFPExtDataForkLen))
        {
            dataSize64.LowPart = GetCompressedFileSize(afpEntry->sz_path.c_str(), (LPDWORD)&dataSize64.HighPart);

            if ((dataSize64.LowPart == INVALID_FILE_SIZE) && (GetLastError() != NO_ERROR))
            {
                dataSize64.LowPart = 0;
                DBGWRITE(dbg_level_info, L"[FDP]GetCompressedFileSize() failed! (%d)\n", GetLastError());
            }
            else
            {
                DBGWRITE(dbg_level_all, L"[FDP]Data fork size =  (%d)\n", dataSize64.LowPart);
            }

            if (afpBitmap & kFPDFLen)
            {
                afpReply.push_num<UINT32>(dataSize64.LowPart);
            }
        }

        if (afpBitmap & (kFPRFLen | kFPExtRsrcForkLen))
        {
            afp_open_file afpFile(afpVolumeID, afpDirID, kReadMode, kAttrFork, afpEntry, afpEntry->sz_path.c_str());

            rsrcSize64.QuadPart = afpFile.get_stream_length(const_cast<wchar_t*>(AFP_RESOURCE_FORK_NAME_PLAIN));

            if (rsrcSize64.QuadPart == BAD_LENGTH_64)
            {
                //
                //There was an error getting the file size
                //
                DBGWRITE(dbg_level_all, L"[FDP]GetStreamLength() failed! (%d) (%s)\n", GetLastError(),
                         afpEntry->sz_object_name.c_str());
                rsrcSize64.QuadPart = 0;
            }
            else
            {
                DBGWRITE(dbg_level_all, L"[FDP]Resource fork size =  (%d)\n", rsrcSize64.LowPart);
            }
        }

        if (afpBitmap & kFPRFLen)
        {
            afpReply.push_num<UINT32>(rsrcSize64.LowPart);
        }

        if (afpBitmap & kFPExtDataForkLen)
        {
            afpReply.push_num<UINT64>(dataSize64.QuadPart);
        }

        if (afpBitmap & kFPUnicodeName)
        {
            uniNameOffset = reinterpret_cast<uint16*>(afpReply.get_current_pos_ptr());
            afpReply.advance_pos_ptr(sizeof(int16));
        }

        if (afpBitmap & kFPExtRsrcForkLen)
        {
            //
            //I don't know what the client is doing here, but it needs an
            //extra 2 bytes inserted here to read the resource fork length
            //correctly. Is it possible it's looking for the launch limit? The
            //spec isn't clear.
            //
            afpReply.push_num<UINT32>(0);

            afpReply.push_num<UINT64>(rsrcSize64.QuadPart);
        }
    }

    if ((afpBitmap & kFPLongName) || (afpBitmap & kFPUnicodeName))
    {
        memset(charMap, 0, sizeof(AFPCHARMAPARRAY));
        AFPGetCharMapForIndexEntry(afpEntry, charMap);

        if (afpBitmap & kFPLongName)
        {
            *longNameOffset = htons(static_cast<uint16>(afpReply.get_current_pos_ptr() - pParmsStart));
            afpReply.push_pascal_string(afpEntry->sz_object_name, charMap);
        }

        if (afpBitmap & kFPUnicodeName)
        {
            *uniNameOffset = htons(static_cast<uint16>(afpReply.get_current_pos_ptr() - pParmsStart));
            afpReply.push_unicode_string(afpEntry->sz_object_name, charMap);
        }
    }

    return (AFP_OK);
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
            EXTRACT_LONG(pTemp, lwCreateDate);
        if (wBitmap & kFPModDate)
            EXTRACT_LONG(pTemp, lwModDate);
        if (wBitmap & kFPBackupDate)
            EXTRACT_LONG(pTemp, lwBackupDate);
    }

    if (wBitmap & kFPFinderInfo)
    {
        FINDER_INFO fInfo;

        RtlCopyMemory(&fInfo, pTemp, sizeof(FINDER_INFO));
        pTemp += sizeof(FINDER_INFO);

        fInfo.fdFlags = ntohs(fInfo.fdFlags);
        fInfo.fdLocation.x = ntohs(fInfo.fdLocation.x);
        fInfo.fdLocation.y = ntohs(fInfo.fdLocation.y);
        fInfo.fdFldr = ntohs(fInfo.fdFldr);

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
