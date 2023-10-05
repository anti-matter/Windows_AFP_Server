//===================================================================
// fdparms.c
//===================================================================

#include "StdAfx.h"
#include "afp.h"
#include "afpdebug.h"
#include "afphelp.h"
#include "fdparms.h"
#include "fpinfo.h"
#include "afpxattr.h"
#include "AFPSession.h"
#include "AFPOpenFile.h"

#include "winerror.h"

//---------------------------------------------------------------------
// AFPGetParmsBufferSize()
//---------------------------------------------------------------------
// Returns how big a buffer is needed to pack all the requested
// parameters.
//
size_t AFPGetParmsBufferSizeRequired(
    IN	PFILEDIRPARMS	pFdp
)
{
    WORD	i;
    size_t	wSize	= 0;
    WORD	wBitmap = (pFdp->bFDPAttributes & kFileDirIsDir) ? pFdp->wDirBitmap : pFdp->wFileBitmap;

    static BYTE	fdParmSizes[14] = {
                    sizeof(WORD),				//Attributes
                    sizeof(DWORD),				//Parent DirID
                    sizeof(LONG),				//Creation date
                    sizeof(LONG),				//Mod date
                    sizeof(LONG),				//Backup date
                    sizeof(FINDER_INFO),		//Finder info
                    sizeof(WORD) + sizeof(BYTE),//Long name offset
                    sizeof(WORD) + sizeof(BYTE),//Short name offset
                    sizeof(DWORD),				//File/Dir ID
                    sizeof(DWORD),				//Data fork len/offspring count
                    sizeof(DWORD),				//Res fork len/owner id
                    sizeof(DWORD),				//Group id
                    sizeof(DWORD),				//Access rights
                    PRODOS_LEN					//ProDOS info
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
            wSize += (WORD)fdParmSizes[i];
        }

        wBitmap >>= 1;
    }

    return(wSize);
}

//---------------------------------------------------------------------
// AFPMapAFPToWindowsPath()
//---------------------------------------------------------------------
// Get the real Windows full path to the object in question. pWindowsPath
// should be at least MAX_PATH in size.
//
AFPERROR AFPMapAFPToWindowsPath(
    IN	WORD	wVolID,
    IN	DWORD	dwDirID,
    IN	PTCHAR	pAFPPath,
    OUT PTCHAR	pWindowsPath,
    IN	size_t	cbWindowsPath
)
{
    AFP_VOLUME_INFO*	pVolInfo;
    DWORD				dwError;
        
    if (AFPGetVolumeInfoBlock(NULL, wVolID, &pVolInfo) == FALSE)
    {
        DBGWRITE(dbg_level_info, L"[FDP]Failed to get vol info block!\n");
        return(afpParmErr);
    }
    
    //
    //Go to the volume index and get the path to the
    //directory ID we're working with.
    //
    dwError = AFPIndexGetFullPath(pVolInfo, dwDirID, pWindowsPath, cbWindowsPath);
    
    if (dwError == NO_ERROR)
    {
        if ((pAFPPath != NULL) && (lstrlen(pAFPPath) > 0))
        {			
            StringCbCat(pWindowsPath, cbWindowsPath, TEXT("\\"));
            StringCbCat(pWindowsPath, cbWindowsPath, pAFPPath);
        }
    }
    
    return(dwError);
}

//---------------------------------------------------------------------
// AFPCheckForAndCreateFinderRsrcStream)
//---------------------------------------------------------------------
// Check to see if a file object has a resource stream. If not, add
// it using defaults.
//
void AFPCheckForAndCreateFinderRsrcStream(
    IN	LPAFPINDEX		pEntry
)
{
    WCHAR		szTempPath[MAX_PATH];
    HANDLE		hFile;
    DWORD		dwError;

    StringCbPrintf(szTempPath, sizeof(szTempPath), TEXT("%s%s"), pEntry->szPath, AFP_RESOURCE_FORK_NAME);
    
    hFile = CreateFile(
                szTempPath,
                GENERIC_READ | GENERIC_WRITE,
                0,
                NULL,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                NULL
    );
    
    if (hFile == INVALID_HANDLE_VALUE)
    {
        dwError = GetLastError();
        
        if (dwError == ERROR_FILE_NOT_FOUND)
        {
            hFile = CreateFile(
                        szTempPath,
                        GENERIC_READ | GENERIC_WRITE,
                        0,
                        NULL,
                        CREATE_NEW,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL
            );

            if (hFile != INVALID_HANDLE_VALUE)
            {
                DBGWRITE(dbg_level_all, 
                        L"[FDP]Successfully created new resource stream (%s)\n",
                        szTempPath
                        );
                
                CloseHandle(hFile);
            }
            else
            {
                DBGWRITE(dbg_level_info, 
                        L"[FDP]Failed to create resource stream for %s! (%d)\n",
                        szTempPath,
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
    IN	AfpBuffer&					afpReply,
    IN	uint16						afpBitmap,
    IN	uint16						afpVolumeID,
    IN	int32						afpDirID,
    IN	LPSESSION					afpSession,
    IN	LPAFPINDEX					afpEntry,
    IN	WIN32_FILE_ATTRIBUTE_DATA	winAttributes
    )
{
    AFPCHARMAPARRAY		charMap = { 0 };
    LARGE_INTEGER		dataSize64 = { 0 };
    LARGE_INTEGER		rsrcSize64 = { 0 };
    uint16*				longNameOffset = NULL;
    uint16*				uniNameOffset = NULL;
    uint8*				pParmsStart = afpReply.GetCurrentPosPtr();
    AFPERROR			afpError = AFP_OK;
    
    if (afpBitmap & kFPFileAttributes)
    {
        uint16	afpAttributes = 0;

        AFPWinToAFPAttributes(winAttributes.dwFileAttributes, &afpAttributes);

        //
        //For files, there are a couple flags that Windows doesn't have.
        //
        if ((winAttributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
            if (afpEntry->flags & FLAG_INDEX_RESOPEN) {
                afpAttributes |= kFileRsrcForkOpen;
            }
            
            if (afpEntry->flags & FLAG_INDEX_DATAOPEN) {
                afpAttributes |= kFileDataForkOpen;
            }
        }

		afpReply.AddInteger<UINT16>(afpAttributes);
    }

    if (afpBitmap & kFPParentID)	afpReply.AddInteger<UINT32>(afpEntry->parID);
    if (afpBitmap & kFPCreateDate)	afpReply.AddInteger<UINT32>(afp_windows_to_mac_time(&winAttributes.ftCreationTime));
    if (afpBitmap & kFPModDate)		afpReply.AddInteger<UINT32>(afp_windows_to_mac_time(&winAttributes.ftLastWriteTime));
    if (afpBitmap & kFPBackupDate)	afpReply.AddInteger<UINT32>(0x80000000);
    
    if (afpBitmap & kFPFinderInfo)
    {
        FINDER_INFO	finderInfo;
        
        RtlZeroMemory(&finderInfo, sizeof(FINDER_INFO));
        
        if (AFP_SUCCESS(AFPGetFinderInfo(afpEntry, &finderInfo)))
        {		
            if (winAttributes.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)
            {
                finderInfo.fdFlags |= kFDInvisible;
            }
                    
            finderInfo.fdFlags 		= htons(finderInfo.fdFlags);
            finderInfo.fdLocation.x = htons(finderInfo.fdLocation.x);
            finderInfo.fdLocation.y = htons(finderInfo.fdLocation.y);
            finderInfo.fdFldr		= htons(finderInfo.fdFldr);
            
            afpReply.AddRawData(&finderInfo, sizeof(FINDER_INFO));
        }
    }

    if (afpBitmap & kFPLongName)
    {
        longNameOffset = (uint16*)afpReply.GetCurrentPosPtr();
        afpReply.Advance(sizeof(int16));
    }

    if (afpBitmap & kFPFileNum)	afpReply.AddInteger<UINT32>(afpEntry->objectID);
    
    //
    //The maps are a little different here for file and directories.
    //
    if (winAttributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
        if (afpBitmap & kFPDirOffCount) {
            afpReply.AddInteger<UINT16>(AFPGetDirOffspringCount(afpVolumeID, afpEntry));
        }

        if (afpBitmap & (kFPDirOwnerID | kFPDirGroupID))
        {
            DWORD	dwOwnerID	= 0;
            DWORD	dwGroupID	= 0;

            //AFPGetDirectoryOwnerAndGroupID(afpEntry->szPath, &dwOwnerID, &dwGroupID);

            if (afpBitmap & kFPDirOwnerID) {
                afpReply.AddInteger<UINT32>(dwOwnerID);
            }
            
            if (afpBitmap & kFPDirGroupID) {
                afpReply.AddInteger<UINT32>(dwGroupID);
            }
        }
        
        if (afpBitmap & kFPDirAccess)
        {
            uint32	afpRights		= 0;

            afpError = afp_get_directory_rights(
                            afpSession->hUserToken,
                            afpEntry->szPath,
                            afpSession->userName,
                            (lstrlen(afpSession->domainName) > 0) ? afpSession->domainName : NULL,
                            &afpRights
                            );

            if (AFP_SUCCESS(afpError)) {

                afpReply.AddInteger<UINT32>(afpRights);
            }
        }

        if (afpBitmap & kFPUnicodeName)
        {
            uniNameOffset = (uint16*)afpReply.GetCurrentPosPtr();
            afpReply.Advance(sizeof(int16));
        }
    }
    else
    {
        if (afpBitmap & (kFPDFLen | kFPExtDataForkLen))
        {
            dataSize64.LowPart = GetCompressedFileSize(afpEntry->szPath, (LPDWORD)&dataSize64.HighPart);

            if ((dataSize64.LowPart == INVALID_FILE_SIZE) && (GetLastError() != NO_ERROR))
            {
                dataSize64.LowPart = 0;
                DBGWRITE(dbg_level_info, L"[FDP]GetCompressedFileSize() failed! (%d)\n", GetLastError());
            }
            else {
                DBGWRITE(dbg_level_all, L"[FDP]Data fork size =  (%d)\n", dataSize64.LowPart);
            }

            if (afpBitmap & kFPDFLen) {

                afpReply.AddInteger<UINT32>(dataSize64.LowPart);
            }
        }

        if (afpBitmap & (kFPRFLen | kFPExtRsrcForkLen))
        {
            AFPOpenFile afpFile(afpVolumeID, afpDirID, kReadMode, kAttrFork, afpEntry, afpEntry->szPath);

            rsrcSize64.QuadPart = afpFile.GetStreamLength(AFP_RESOURCE_FORK_NAME_PLAIN);
            
            if (rsrcSize64.QuadPart == BAD_LENGTH_64)
            {
                //
                //There was an error getting the file size
                //
                DBGWRITE(dbg_level_all, L"[FDP]GetStreamLength() failed! (%d) (%s)\n", GetLastError(), afpEntry->szObjectName);
                rsrcSize64.QuadPart = 0;
            }
            else {
                DBGWRITE(dbg_level_all, L"[FDP]Resource fork size =  (%d)\n", rsrcSize64.LowPart);
            }
        }

        if (afpBitmap & kFPRFLen) {
            
            afpReply.AddInteger<UINT32>(rsrcSize64.LowPart);
        }

        if (afpBitmap & kFPExtDataForkLen) {

            afpReply.AddInteger<UINT64>(dataSize64.QuadPart);
        }

        if (afpBitmap & kFPUnicodeName)
        {
            uniNameOffset = reinterpret_cast<uint16*>(afpReply.GetCurrentPosPtr());
            afpReply.Advance(sizeof(int16));
        }

        if (afpBitmap & kFPExtRsrcForkLen)
        {
            //
            //I don't know what the client is doing here, but it needs an
            //extra 2 bytes inserted here to read the resource fork length
            //correctly. Is it possible it's looking for the launch limit? The
            //spec isn't clear.
            //
            afpReply.AddInteger<UINT32>(0);

            afpReply.AddInteger<UINT64>(rsrcSize64.QuadPart);
        }
    }
    
    if ((afpBitmap & kFPLongName) || (afpBitmap & kFPUnicodeName))
    {
        memset(charMap, 0, sizeof(AFPCHARMAPARRAY));
        AFPGetCharMapForIndexEntry(afpEntry, charMap);

        if (afpBitmap & kFPLongName)
        {
            *longNameOffset = htons((uint16)(afpReply.GetCurrentPosPtr() - pParmsStart));
            afpReply.AddUniStringAsPascal(afpEntry->szObjectName, charMap);
        }

        if (afpBitmap & kFPUnicodeName)
        {			
            *uniNameOffset = htons((uint16)(afpReply.GetCurrentPosPtr() - pParmsStart));
            afpReply.AddUniString(afpEntry->szObjectName, charMap);
        }
    }
    
    return( AFP_OK );
}

//---------------------------------------------------------------------
// AFPGetDirOffspringCount()
//---------------------------------------------------------------------
// Returns the # of files and dirs in a specified directory. We do
// the quickly by search the index rather than the disk.
//
WORD AFPGetDirOffspringCount(
    IN	WORD		wVolID,
    IN	LPAFPINDEX	pDirEntry
)
{
    AFP_VOLUME_INFO*	pVolInfo;
    WORD				wCount = 0;

    if (AFPGetVolumeInfoBlock(NULL, wVolID, &pVolInfo) == FALSE)
    {
        DBGWRITE(dbg_level_info, L"[FDP]Failed to get vol info block in AFPGetDirOffspringCount()!\n");
        return(static_cast<WORD>(afpParmErr));
    }
    
    wCount = (WORD)(pVolInfo->index->GetOffspring(pDirEntry->objectID));

    return( wCount );
}

//---------------------------------------------------------------------
// AFPSetFileDirParms()
//---------------------------------------------------------------------
// Worker for the FPSetFileDirParms API. The caller is responsible
// for verifying the bitmap.
//
AFPERROR AFPSetFileDirParms(
    IN	WORD		wBitmap,
    IN	LPAFPINDEX	pEntry,
    IN	PCHAR		pParms,
    IN	DWORD		dwFileAttributes
)
{
    PCHAR	pTemp 			= pParms;
    DWORD	dwError			= NO_ERROR;
    ULONG	lwCreateDate	= 0;
    ULONG	lwModDate		= 0;
    ULONG	lwBackupDate	= 0;
    DWORD	dwAttributes 	= dwFileAttributes;
    WORD	wAFPAttributes	= 0;
    
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
        
        if (!SetFileAttributes(pEntry->szPath, dwAttributes))
        {
            dwError = GetLastError();
            DBGWRITE(dbg_level_info, L"[FDP]SetFileAttributes() failed! (%d)\n", dwError);
            
            return(afp_map_win_error_to_afp(dwError, static_cast<DWORD>(afpMiscErr)));
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
        FINDER_INFO	fInfo;
        
        RtlCopyMemory(&fInfo, pTemp, sizeof(FINDER_INFO));
        pTemp += sizeof(FINDER_INFO);
                
        fInfo.fdFlags 		= ntohs(fInfo.fdFlags);
        fInfo.fdLocation.x 	= ntohs(fInfo.fdLocation.x);
        fInfo.fdLocation.y 	= ntohs(fInfo.fdLocation.y);
        fInfo.fdFldr		= ntohs(fInfo.fdFldr);
                                
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
            pEntry->szPath,
            (dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? TRUE : FALSE,
            (lwCreateDate == 0) ? NULL : &lwCreateDate,
            (lwModDate == 0) 	? NULL : &lwModDate,
            (lwBackupDate == 0) ? NULL : &lwBackupDate
        );
    }
    
    return(NO_ERROR);
}

//---------------------------------------------------------------------
// AFPSetFileTime()
//---------------------------------------------------------------------
// Set the time fields of a file.
//
AFPERROR AFPSetFileTime(
    IN	PTCHAR	pWindowspath,
    IN	BOOL	fIsDirectory,
    IN	PULONG	plwCreation,
    IN	PULONG	plwModification,
    IN	PULONG	plwBackup
)
{
    HANDLE		hFile		= INVALID_HANDLE_VALUE;
    DWORD		dwError		= NO_ERROR;
    PFILETIME	pCreation	= NULL;
    PFILETIME	pMod		= NULL;
    PFILETIME	pBackup		= NULL;
    FILETIME	creation,mod,backup;
    
    //
    //To set the file time, we have to have a handle to
    //the file object. This sucks.
    //
    if (fIsDirectory)
    {
        hFile = CreateFile(
                    pWindowspath,
                    FILE_GENERIC_WRITE,
                    FILE_SHARE_READ|FILE_SHARE_DELETE,
                    NULL,
                    OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS,
                    NULL);
    }
    else
    {
        hFile = CreateFile(
                    pWindowspath,
                    GENERIC_WRITE,
                    FILE_SHARE_READ	| FILE_SHARE_WRITE,
                    NULL,
                    OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS,
                    NULL
        );
    }
    
    if (hFile == INVALID_HANDLE_VALUE)
    {
        dwError = GetLastError();
        
        DBGWRITE(dbg_level_info, 
                L"[FDP]CreatFile() failed in AFPSetFileTime! (%d)\n", dwError);
        
        switch(dwError)
        {
            case ERROR_FILE_NOT_FOUND:
                return(afpObjectNotFound);
                break;
            
            default:
                return(afpMiscErr);
        }
    }
    
    //
    //Convert all the mac times to FILETIME structures.
    //
    if (plwCreation != NULL)
    {
        afp_mac_to_windows_time(*plwCreation, &creation);
        pCreation = &creation;
    }
    if (plwModification != NULL)
    {
        afp_mac_to_windows_time(*plwModification, &mod);
        pMod = &mod;
    }
    if (plwBackup != NULL)
    {
        afp_mac_to_windows_time(*plwBackup, &backup);
        pBackup = &backup;
    }
    
    //
    //Make the windows API call that does the dirty deed.
    //
    if (!SetFileTime(hFile, pCreation, NULL, pMod))
    {
        DBGWRITE(dbg_level_info,
                L"[FDP]SetFileTime() failed! (%d)\n", GetLastError());
        
        dwError = static_cast<DWORD>(afpMiscErr);
    }
        
    CloseHandle(hFile);
    return(dwError);
}

//---------------------------------------------------------------------
// AFPWinToAFPAttributes()
//---------------------------------------------------------------------
// Convert windows to AFP attributes.
//
WORD AFPWinToAFPAttributes(
    IN	DWORD	dwWinAttributes,
    OUT	PWORD	pwAFPAttributes
)
{
    WORD	wAFPAttribs = 0;
    
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
    
    if (pwAFPAttributes != NULL)
    {
        *pwAFPAttributes = wAFPAttribs;
    }
    
    return(wAFPAttribs);
}

#ifdef _DEBUG
// ---------------------------------------------------------------------------
// AFPExtractDirParms()
// ---------------------------------------------------------------------------
// Extracts requested dir information from the FPFileDirParms reply block.
void AFPExtractDirParms(
        IN PBYTE		pParms,		//Points to actual start of data
        IN WORD			wBitMap
)
{
    PBYTE UNALIGNED	pTemp;
    PBYTE UNALIGNED	pTemp2;
    PBYTE UNALIGNED pInputParms;
    BYTE			wLen;
    CHAR			name[MAX_PATH];
    DWORD			dwTemp;
    WORD			wTemp;
    
    pTemp		= pParms;
    pInputParms	= pParms;
    
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
        
        pTemp2 	= pInputParms + ntohs(*(WORD UNALIGNED *)pTemp);
        wLen 	= *(pTemp2++);
                
        strncpy_s(name, sizeof(name), (PCHAR)pTemp2, wLen);
        pTemp += sizeof(WORD);
        
        DBGWRITE(dbg_level_info, L"[FDP]dir long name = %s\n", name);
    }
    if (wBitMap & kFPDirShortName)
    {
        RtlZeroMemory(name, MAX_PATH);
        
        pTemp2 	= pInputParms + ntohs(*(WORD UNALIGNED *)pTemp);
        wLen 	= *(pTemp2++);
        
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






