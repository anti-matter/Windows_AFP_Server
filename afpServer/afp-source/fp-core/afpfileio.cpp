/*
 *	afpfileio.cpp
 *	Created 3/5/2005
 *
 *	Copyright © 2005, Michael J. Conrad. All Rights Reserved.
 *
 *	Core FP file I/O routines.
 */

#include "StdAfx.h"
#include "afpfileio.h"


/*
 * FPFlushFork()
 *
 * Description:
 *		Flushes an open file fork.
 *
 * Returns: AFPERROR
 */

AFPERROR FPFlushFork(
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{
    UNREFERENCED_PARAMETER(afpRequest);
    UNREFERENCED_PARAMETER(afpReply);
    UNREFERENCED_PARAMETER(afpSession);

    return( AFP_OK );
}


/*
 * FPCloseFork()
 *
 * Description:
 *		Closes the data or resource fork from reading/writing.
 *
 * Returns: AFPERROR
 */

AFPERROR FPCloseFork(
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{
    UNREFERENCED_PARAMETER(afpReply);

    LAFPSession*	afpAFPSession	= (LAFPSession*)afpSession->afpSessionObject;
    uint16			afpRefNum		= 0;
    AFPERROR		afpError		= AFP_OK;
    
    DBGWRITE(DbgLevelAll, L"[afp:FPCloseFork]Enter...\n");
            
    //
    //Parameters start after first word (afp cmd and 0 byte)
    //
    afpRequest.Advance(sizeof(int16));
    
    //
    //Extract the parameter and make the call.
    //
    afpRefNum	= afpRequest.GetInt16();
    afpError	= afpAFPSession->SessionCloseFile(afpRefNum);
    
    return( afpError );
}


/*
 * FPOpenFork()
 *
 * Description:
 *		Opens the data or resource fork for reading/writing.
 *
 * Returns: AFPERROR
 */

AFPERROR FPOpenFork(
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{
    unichar				afpPathname[MAX_AFP_PATH];
    LAFPSession*		afpAFPSession	= (LAFPSession*)afpSession->afpSessionObject;
    LOpenFile*			afpOpenFile		= NULL;
    AFP_VOLUME_INFO*	afpVolInfo		= NULL;
    int8				afpFork			= 0;
    int16				afpVolumeID		= 0;
    int32				afpDirID		= 0;
    int8				afpPathType		= 0;
    int16				afpBitmap		= 0;
    int16				afpMode			= 0;
    uint16				afpNewRefNum	= 0;
    AFPERROR			afpError		= AFP_OK;
    LPAFPINDEX			afpEntry		= NULL;

    WIN32_FILE_ATTRIBUTE_DATA	winAttributes;
    
    DBGWRITE(DbgLevelAll, L"[afp:FPOpenFork]Enter\n");
    
    //
    //The first byte contains the afp command.
    //
    afpRequest.Advance(sizeof(int8));
    
    //
    //Get the parameters for the file we're opening.
    //
    afpFork		= (afpRequest.GetInt8() & kResourceForkBit) ? kRsrcFork : kDataFork;
    afpVolumeID	= afpRequest.GetInt16();
    afpDirID	= afpRequest.GetInt32();
    afpBitmap	= afpRequest.GetInt16();
    afpMode		= afpRequest.GetInt16();
    afpPathType	= afpRequest.GetInt8();

    //
    //The client must have previously opened the volume we are operating on.
    //
    afpVolInfo = afpAFPSession->SessionFindOpenVolumeBlock(afpVolumeID);
    
    if (afpVolInfo == NULL)
    {
        //
        //The volume has not been opened, we need to return error.
        //
        return( afpParmErr );
    }

    //
    //Get the pathname of the object we're working on.
    //
    afpError = afpRequest.GetString(afpPathname, sizeof(afpPathname), true, afpPathType);
    
    if (!AFP_SUCCESS(afpError))
    {
        return( afpError );
    }
        
    //
    //Pathname cannot be null since it contains the file to open.
    //
    if (lstrlen(afpPathname) == 0)
    {
        DBGWRITE(DbgLevelInfo, L"[afp:FPOpenFork]Null pathname!\n");
        return( afpParmErr );
    }

    //
    //Get the index entry pertaining to the file we're opening.
    //
    afpError = AFPIndexFindEntryWithPath(afpVolInfo, afpDirID, afpPathname, &afpEntry);
    
    if (!AFP_SUCCESS(afpError))
    {
        DBGWRITE(DbgLevelInfo, L"[afp:FPOpenFork]Index entry not found! (%s)\n", afpPathname);
        return( afpObjectNotFound );
    }

    //
    //Collect attribute data on the file or folder.
    //
    if (!GetFileAttributesEx(afpEntry->szPath, GetFileExInfoStandard, &winAttributes))
    {
        DBGWRITE(DbgLevelInfo, L"[afp:FPOpenFork]GetFileAttributesEx() failed! (%d)\n", GetLastError());
        DBGWRITE(DbgLevelInfo, L"[afp:FPOpenFork]path = %s\n", afpEntry->szPath);
                
        return( afpObjectNotFound );
    }

    //
    //Make sure the user doesn't have too many files open already.
    //
    if (afpAFPSession->GetNumFilesOpen() > MAX_AFP_FILES_OPEN)
    {
        DBGWRITE(DbgLevelInfo, L"[afp:FPOpenFork]****Can't open file, too many open already! (%d)\n", afpAFPSession->GetNumFilesOpen());
        return( afpTooManyFilesOpen );
    }

    afpOpenFile = LAFPSession::NewOpenFileObject(
                            afpAFPSession,
                            afpVolumeID,
                            afpDirID,
                            afpMode,
                            afpFork,
                            afpEntry,
                            afpEntry->szPath
                            );

    afpError = (afpOpenFile == NULL) ? afpMiscErr : AFP_OK;

    if (AFP_SUCCESS(afpError))
    {
        //
        //Impersonate the logged on user for security checks.
        //
        if (!AFPImpersonateUser(afpSession->hUserToken))
        {
            //
            //Impersonation failed, don't allow any further operations.
            //
            return( afpAccessDenied );
        }

        //
        //Now, actually open the file for access as desired.
        //
        afpError = afpOpenFile->OpenFileFork();

        AFPRevertToSelf(afpSession->hUserToken);

        //
        //If we successfully open the file, then return whatever object parms
        //the caller has asked for.
        //
        if (AFP_SUCCESS(afpError))
        {
            //
            //We need to return the file ref num to the caller.
            //
            afpNewRefNum = afpOpenFile->GetFileRefNum();

            //
            //We don't support the following bitmaps, so we clear them.
            //
            if (afpSession->bAFPVersion < afpVersion30)
            {
                if (afpBitmap & kFPProDos)		afpBitmap  &= ~kFPProDos;
                if (afpBitmap & kFPShortName)	afpBitmap  &= ~kFPShortName;
            }

            //
            //Add in the bitmap and new refnum id.
            //
            afpReply.AddInt16(afpBitmap);
            afpReply.AddInt16(afpNewRefNum);

            afpError = AFPGetObjectParms(
                            afpReply,
                            afpBitmap,
                            afpVolumeID,
                            afpDirID,
                            afpSession,
                            afpEntry,
                            winAttributes
                            );
        }
    }

    //
    //If we have an error opening the file, make sure we dispose
    //of the file object or we'll leak memory as well as leak
    //open file counts.
    //
    if (!AFP_SUCCESS(afpError))
    {
        afpAFPSession->SessionCloseFile(afpOpenFile->GetFileRefNum());
    }

    return( afpError );
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

AFPERROR FPSetForkParms(
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{
    UNREFERENCED_PARAMETER(afpReply);

    LAFPSession*	afpAFPSession	= (LAFPSession*)afpSession->afpSessionObject;
    LOpenFile*		afpOpenFile		= NULL;
    int16			afpForkRef		= 0;
    int16			afpBitmap		= 0;
    uint32			afpForkLen		= 0;
    AFPERROR		afpError		= AFP_OK;
    
    DBGWRITE(DbgLevelAll, L"[afp:FPSetForkParms]Enter\n");
    
    //
    //The first byte contains the afp command.
    //
    afpRequest.Advance(sizeof(int16));
    
    afpForkRef 	= afpRequest.GetInt16();
    afpBitmap	= afpRequest.GetInt16();
    afpForkLen	= afpRequest.GetInt32();
    
    afpOpenFile = afpAFPSession->FindOpenFileObject(afpForkRef);
    afpError	= (afpOpenFile == NULL) ? afpParmErr : AFP_OK;

    if (AFP_SUCCESS(afpError))
    {
        //
        //Make sure the volume is not marked read-only.
        //
        afpError = AFPVolumeWriteAccessCheck(afpOpenFile->GetVolID());

        if (!AFP_SUCCESS(afpError))
        {
            return( afpError );
        }

        if ((afpBitmap & kFPDFLen) || (afpBitmap & kFPExtDataForkLen))
        {
            if (afpOpenFile->GetFork() != kDataFork)
            {
                DBGWRITE(DbgLevelAll, L"[afp:FPSetForkParms]Data fork is not open!\n");
                return( afpBitmapErr );
            }
        }
        
        if ((afpBitmap & kFPRFLen) || (afpBitmap & kFPExtRsrcForkLen))
        {
            if (afpOpenFile->GetFork() != kRsrcFork)
            {
                DBGWRITE(DbgLevelAll, L"[afp:FPSetForkParms]Resource fork is not open! (%x)\n");
                return( afpBitmapErr );
            }
        }
    }
    else
    {
        DBGWRITE(DbgLevelAll, L"[afp:FPSetForkParms]Fork item not found!\n");
    }
    
    return( afpError );
}


/*
 * FPGetForkParms()
 *
 * Description:
 *		Retrieves parameters for a file referenced by the open fork refnum.
 *
 * Returns: AFPERROR
 */

AFPERROR FPGetForkParms(
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{
    LAFPSession*	afpAFPSession	= (LAFPSession*)afpSession->afpSessionObject;
    LOpenFile*		afpOpenFile		= NULL;
    int16			afpForkRef		= 0;
    int16			afpBitmap		= 0;
    AFPERROR		afpError		= AFP_OK;

    WIN32_FILE_ATTRIBUTE_DATA	winAttributes;

    DBGWRITE(DbgLevelAll, L"[afp:FPGetForkParms]Enter\n");
    
    //
    //The first byte contains the afp command.
    //
    afpRequest.Advance(sizeof(int16));
    
    //
    //Get the parameters for the file we're opening.
    //
    afpForkRef	= afpRequest.GetInt16();
    afpBitmap	= afpRequest.GetInt16();
    
    afpOpenFile = afpAFPSession->FindOpenFileObject(afpForkRef);
    afpError	= (afpOpenFile == NULL) ? afpParmErr : AFP_OK;
    
    if (AFP_SUCCESS(afpError))
    {
        //
        //We don't support the following bitmaps, so we clear them.
        //
        if (afpSession->bAFPVersion < afpVersion30)
        {
            if (afpBitmap & kFPProDos)		afpBitmap  &= ~kFPProDos;
            if (afpBitmap & kFPShortName)	afpBitmap  &= ~kFPShortName;
        }

        //
        //Make sure the caller is asking for the length of the fork
        //that is actually opened.
        //
        if (((afpBitmap & kFPDFLen) && (afpOpenFile->GetFork() == kRsrcFork))	||
            ((afpBitmap & kFPRFLen) && (afpOpenFile->GetFork() == kDataFork))	)
        {
            DBGWRITE(DbgLevelInfo, L"[afp:FPGetForkParms]Attempt to get length of wrong fork!\n");
            return( afpBitmapErr );
        }

        //
        //Add in the bitmap
        //
        afpReply.AddInt16(afpBitmap);

        //
        //We need this to properly set the attributes.
        //
        GetFileAttributesEx(afpOpenFile->GetWinPath(), GetFileExInfoStandard, &winAttributes);
        
        //
        //Now that we've opened the file, get the requested parameters.
        //
        afpError = AFPGetObjectParms(
                        afpReply,
                        afpBitmap,
                        afpOpenFile->GetVolID(),
                        afpOpenFile->GetDirID(),
                        afpSession,
                        afpOpenFile->GetEntry(),
                        winAttributes
                        );
    }
    
    DBGWRITE(DbgLevelAll, L"[afp:FPGetForkParms]Returning %d\n", afpError);

    return( afpError );
}


/*
 * FPCreateFile()
 *
 * Description:
 *		Create a new file fork.
 *
 * Returns: AFPERROR
 */

AFPERROR FPCreateFile(
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{
    UNREFERENCED_PARAMETER(afpReply);

    unichar				afpPathname[MAX_AFP_PATH];
    unichar				winPath[MAX_PATH];
    AFPCHARMAPARRAY		charMap;
    LPAFPINDEX			afpEntry		= NULL;
    AFP_VOLUME_INFO*	afpVolumeInfo	= NULL;
    int8				createFlag		= 0;
    int16				afpVolumeID		= 0;
    int32				afpDirID		= 0;
    int8				afpPathType		= 0;
    AFPERROR			afpError		= AFP_OK;
    HANDLE				hFile			= INVALID_HANDLE_VALUE;

    DBGWRITE(DbgLevelAll, L"[afp:FPCreateFile]Enter\n");
    
    //
    //First byte is padding.
    //
    afpRequest.Advance(sizeof(int8));
    
    createFlag	= afpRequest.GetInt8();
    afpVolumeID	= afpRequest.GetInt16();
    afpDirID	= afpRequest.GetInt32();
    afpPathType	= afpRequest.GetInt8();

    //
    //The client must have previously opened the volume we are creating
    //the file on.
    //
    if (!((LAFPSession*)afpSession->afpSessionObject)->SessionHasVolumeOpen(afpVolumeID))
    {
        //
        //The volume has not been opened, we need to return error.
        //
        return( afpParmErr );
    }

    //
    //Make sure the volume is not marked read-only.
    //
    afpError = AFPVolumeWriteAccessCheck(afpVolumeID);

    if (!AFP_SUCCESS(afpError))
    {
        return( afpError );
    }

    //
    //Get the pathname of the object we're working on.
    //
    memset(charMap, 0, sizeof(AFPCHARMAPARRAY));
    
    afpError = afpRequest.GetString(
                                afpPathname,
                                sizeof(afpPathname),
                                true,
                                afpPathType,
                                charMap
                                );
    
    if (!AFP_SUCCESS(afpError))
    {
        return( afpError );
    }
    
    //
    //Pathname cannot be null since it contains the new file name.
    //
    if (lstrlen(afpPathname) == 0)
    {
        DBGWRITE(DbgLevelInfo, L"[afp:FPCreateFile]Null pathname!\n");
        return( afpParmErr );
    }

    afpVolumeInfo = ((LAFPSession*)afpSession->afpSessionObject)->SessionFindOpenVolumeBlock(afpVolumeID);
    if (afpVolumeInfo == NULL)
    {
        DBGWRITE(DbgLevelInfo, L"[afp:FPCreateFile]Bad volume ID!\n");
        return( afpParmErr );
    }
    
    //
    //Take the path supplied by the Mac client and convert it into a windows path.
    //The path should start from the directory as pointed to by dirID.
    //
    afpError = AFPIndexFindEntryWithPath(afpVolumeInfo, afpDirID, NULL, &afpEntry);
            
    if (!AFP_SUCCESS(afpError))
    {
        //
        //We couldn't find the requested object.
        //
        DBGWRITE(DbgLevelInfo, L"[afp:FPCreateFile]Path not found in FPCreatFile!\n");
        return( afpError );
    }

    //
    //Create the full path to the new file.
    //
    StringCbPrintf(winPath, sizeof(winPath), TEXT("%s\\%s"), afpEntry->szPath, afpPathname);
    
    //
    //Don't let the change notify mechanism do anything yet.
    //
    AQUIRE_NOTIFY_LOCK_VOLINFO(afpVolumeInfo);

    //
    //Impersonate the logged on user. This makes sure only those who
    //are allowed can create files.
    //
    if (!AFPImpersonateUser(afpSession->hUserToken))
    {
        //
        //Impersonation failed, don't allow any further operations.
        //
        return( afpAccessDenied );
    }
    
    //
    //Call the Win32 API that creates the file for us on this filesystem.
    //
    hFile = CreateFile(
                winPath,
                GENERIC_READ | GENERIC_WRITE,
                0,
                NULL,
                (createFlag & kHardCreate) ? CREATE_ALWAYS : CREATE_NEW,
                FILE_ATTRIBUTE_NORMAL,
                NULL
    );

    AFPRevertToSelf(afpSession->hUserToken);
    
    if (hFile == INVALID_HANDLE_VALUE)
    {
        DBGWRITE(DbgLevelInfo, 
                    L"[afp:FPCreateFile]CreateFile() for data fork failed! (%d) (%s)\n",
                    GetLastError(),
                    winPath);
        
        afpError = AFPMapWinErrorToAFP(GetLastError(), static_cast<DWORD>(afpParmErr));
    }
    else
    {
        afpError = NO_ERROR;

        //
        //Add the new file to the afp index for tracking purposes.
        //
        afpEntry = AFPIndexAddEntry(
                        afpVolumeInfo,
                        afpVolumeID,
                        afpDirID,
                        afpPathname,
                        winPath,
                        false,
                        NULL
                        );

        if (afpEntry != NULL)
        {
            //
            //Now make sure that our NTFS streams are created so we have
            //a FInfo and resource forks.
            //
            AFPCheckForAndAddFinderInfoStream(afpEntry);
            AFPCheckForAndCreateFinderRsrcStream(afpEntry);

            //
            //Copy the charmap that contains the Mac-Windows mapping for
            //characters to the entries mapping table.
            //
            AFPAddCharMapToIndexEntry(afpEntry, charMap);
        }

        //
        //This tells the afp message system to kick in and notify other
        //Mac clients that a change happened on the volume.
        //
        AFPMarkVolumeDirty(afpVolumeInfo->wVolID);

        //
        //We're done with the file and we don't need the handle
        //to it anymore.
        //
        CloseHandle(hFile);
    }

    RELEASE_NOTIFY_LOCK_VOLINFO(afpVolumeInfo);

    return( afpError );
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
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{
    LAFPSession*	afpAFPSession	= (LAFPSession*)afpSession->afpSessionObject;
    LOpenFile*		afpOpenFile		= NULL;
    int8			afpCommand		= 0;
    int8			afpBRLFlags		= 0;
    int16			afpForkRef		= 0;
    int64			afpOffset		= 0;
    int64			afpLength		= 0;
    uint32			afpFileSize		= 0;
    AFPERROR		afpError		= AFP_OK;
    HANDLE			hFile			= INVALID_HANDLE_VALUE;
    
    //
    //Skip the first byte which is afp command.
    //
    afpCommand	= afpRequest.GetInt8();
    afpBRLFlags	= afpRequest.GetInt8();
    afpForkRef	= afpRequest.GetInt16();

    switch (afpCommand)
    {
        case afpByteRangeLock:
            afpOffset	= afpRequest.GetInt32();
            afpLength	= afpRequest.GetInt32();
            break;
        
        case afpByteRangeLockExt:
            afpOffset	= afpRequest.GetInt64();
            afpLength	= afpRequest.GetInt64();
            break;
            
        default:
            return( afpParmErr );
    }
    
    //
    //Get the handle to the open file fork.
    //
    afpOpenFile = afpAFPSession->FindOpenFileObject(afpForkRef);
    afpError	= (afpOpenFile == NULL) ? afpParmErr : AFP_OK;

    if (!AFP_SUCCESS(afpError))
    {
        return( afpError );
    }

    hFile = afpOpenFile->GetFileHandle();
    
    if (hFile == INVALID_HANDLE_VALUE)
    {
        //
        //Bad news, the fork is apparently not open.
        //
        return( afpParmErr );
    }
    
    //
    //Determine the size of the file so we can calculate the offset
    //from the end of the file and lock the entire range if
    //necessary.
    //
    afpFileSize = GetFileSize(hFile, NULL);
    afpError	= (afpFileSize == 0xFFFFFFFF) ? afpParmErr : AFP_OK;
    
    if (AFP_SUCCESS(afpError))
    {		
        if (afpBRLFlags & kUnlockFlag)
        {
            if (afpLength >= (int32)0xFFFFFFFF)
            {
                switch(afpOffset)
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
            
            DBGWRITE(DbgLevelInfo, L"[afp:FPByteRangeLock]Unlocking...\n");
            DBGWRITE(DbgLevelInfo, L"[afp:FPByteRangeLock]Offset = %d\n", afpOffset);
            DBGWRITE(DbgLevelInfo, L"[afp:FPByteRangeLock]Length = %d\n", afpLength);
            
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
            if (afpBRLFlags & kStartEndFlag) {
            
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
                    DBGWRITE(DbgLevelInfo, L"[afp:FPByteRangeLock]Negative offset from start!\n");
                    return( afpParmErr );
                }
                
                //
                //If the length is 0xFFFFFFFF, then we have special handling to do.
                //
                if (((afpCommand == afpByteRangeLock) && (afpLength == 0xFFFFFFFF))	||
                    ((afpCommand == afpByteRangeLockExt) && (afpLength == 0xFFFFFFFFFFFFFFFFLL)))
                {	
                    switch(afpOffset)
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
            }//from beginning

            DBGWRITE(DbgLevelInfo, L"[afp:FPByteRangeLock]Locking...\n");
            DBGWRITE(DbgLevelInfo, L"[afp:FPByteRangeLock]Offset = %d\n", afpOffset);
            DBGWRITE(DbgLevelInfo, L"[afp:FPByteRangeLock]Length = %d\n", afpLength);

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
                afpReply.AddInt32((uint32)afpOffset);
                break;
            
            case afpByteRangeLockExt:
                afpReply.AddInt64(afpOffset);
                break;
                
            default:
                return( afpParmErr );
        }
    }
    
    return( afpError );
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
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{
    LAFPSession*	afpAFPSession	= (LAFPSession*)afpSession->afpSessionObject;
    LOpenFile*		afpOpenFile		= NULL;
    int8			afpCommand		= 0;
    int16			afpForkRef		= 0;
    int64			afpOffset		= 0;
    int64			afpReqCount		= 0;
    uint32			afpActCount		= 0;
    AFPERROR		afpError		= AFP_OK;
    
    DBGWRITE(DbgLevelAll, L"[afp:FPRead]Enter\n");
    
    //
    //The first byte contains the afp command.
    //
    afpCommand = afpRequest.GetInt8();
    afpRequest.Advance(sizeof(int8));
    
    afpForkRef = afpRequest.GetInt16();
    
    switch(afpCommand)
    {
        case afpRead:
            afpOffset	= afpRequest.GetInt32();
            afpReqCount	= afpRequest.GetInt32();
            break;
        
        case afpReadExt:
            afpOffset	= afpRequest.GetInt64();
            afpReqCount	= afpRequest.GetInt64();
            break;
        
        default:
            return( afpParmErr );
    }

    //
    //Here is where we would normally get the newLineMask and char, however,
    //we are not supporting that functionality in this server (that functionality
    //has been dropped in AFP3.x anyway).
    //

    afpOpenFile = afpAFPSession->FindOpenFileObject(afpForkRef);
    afpError	= (afpOpenFile == NULL) ? afpParmErr : AFP_OK;

    if (AFP_SUCCESS(afpError))
    {
        if (!AFPImpersonateUser(afpSession->hUserToken))
        {
            //
            //Impersonation failed, don't allow any further operations.
            //
            return( afpAccessDenied );
        }

        afpError = afpOpenFile->ReadFork(
                                    afpReply,
                                    afpOffset,
                                    afpReqCount,
                                    &afpActCount
                                    );

        AFPRevertToSelf(afpSession->hUserToken);
    }

    //
    //FPRead is a special case in that the return value is returned
    //in the DSI Header's datasize field.
    //
    afpReply.SetDSIReturnValue(afpActCount);
    
    DBGWRITE(DbgLevelAll, L"[afp:FPRead]Returning error %d and count %d\n", afpError, afpActCount);

    return( afpError );
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
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{
    LAFPSession*	afpAFPSession	= (LAFPSession*)afpSession->afpSessionObject;
    LOpenFile*		afpOpenFile		= NULL;
    int8			afpCommand		= 0;
    int16			afpForkRef		= 0;
    int64			afpOffset		= 0;
    int64			afpReqCount		= 0;
    int64			afpActCount		= 0;
    int8			afpFlag			= 0;
    AFPERROR		afpError		= AFP_OK;
    
    DBGWRITE(DbgLevelAll, L"[afp:FPWrite]Enter\n");
    
    //
    //The first byte contains the afp command.
    //
    afpCommand	= afpRequest.GetInt8();
    afpFlag		= afpRequest.GetInt8();
    afpForkRef	= afpRequest.GetInt16();
    
    switch(afpCommand)
    {
        case afpWrite:
            afpOffset	= afpRequest.GetInt32();
            afpReqCount	= afpRequest.GetInt32();
            break;
        
        case afpWriteExt:
            afpOffset	= afpRequest.GetInt64();
            afpReqCount	= afpRequest.GetInt64();
            break;
            
        default:
            return( afpParmErr );
    }

    afpOpenFile = afpAFPSession->FindOpenFileObject(afpForkRef);
    afpError	= (afpOpenFile == NULL) ? afpParmErr : AFP_OK;

    if (AFP_SUCCESS(afpError))
    {
        //
        //Make sure the volume is not marked read-only.
        //
        afpError = AFPVolumeWriteAccessCheck(afpOpenFile->GetVolID());

        if (!AFP_SUCCESS(afpError))
        {
            return( afpError );
        }

        if (!AFPImpersonateUser(afpSession->hUserToken))
        {
            //
            //Impersonation failed, don't allow any further operations.
            //
            return( afpAccessDenied );
        }

        afpError = afpOpenFile->WriteFork(
                                    afpRequest,
                                    afpOffset,
                                    afpReqCount,
                                    (afpFlag & kWriteStartEndFlag) ? FALSE : TRUE,
                                    &afpActCount
                                    );

        AFPRevertToSelf(afpSession->hUserToken);

        if (AFP_SUCCESS(afpError))
        {
            switch(afpCommand)
            {
                case afpWrite:
                    afpReply.AddInt32((int32)afpActCount);
                    break;
                
                case afpWriteExt:
                    afpReply.AddInt64(afpActCount);
                    break;
            }
        }
    }
    
    return( afpError );
}

