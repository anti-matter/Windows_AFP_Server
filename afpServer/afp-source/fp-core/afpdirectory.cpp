/*
 *	afpdirectory.cpp
 *	Created 3/5/2005
 *
 *	Copyright © 2005, Michael J. Conrad. All Rights Reserved.
 *
 *	Core FP directory routines.
 */

#include "StdAfx.h"
#include "afpdirectory.h"


/*
 * FPResolveID()
 *
 * Description:
 *		Returns file information by file ID.
 *
 * Returns: AFPERROR
 */

AFPERROR FPResolveID(
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{
    int16				afpVolumeID		= 0;
    int32				afpFileID		= 0;
    int16				afpBitmap		= 0;
    LPAFPINDEX			afpEntry		= NULL;
    AFP_VOLUME_INFO*	afpVolumeInfo	= NULL;
    AFPERROR			afpError		= AFP_OK;

    WIN32_FILE_ATTRIBUTE_DATA	winAttributes;

    //
    //First WORD is afp command and padding byte.
    //
    afpRequest.Advance(sizeof(int16));

    afpVolumeID	= afpRequest.GetInt16();
    afpFileID	= afpRequest.GetInt32();
    afpBitmap	= afpRequest.GetInt16();

    //
    //Get a pointer to the volume info structure for this volume.
    //
    afpVolumeInfo = ((AFPSession*)afpSession->afpSessionObject)->SessionFindOpenVolumeBlock(afpVolumeID);

    afpError = AFPIndexFindEntry(afpVolumeInfo, afpFileID, &afpEntry);

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
        //Add in the bitmap and new refnum id.
        //
        afpReply.AddInt16(afpBitmap);

        //
        //Collect attribute data on the file or folder.
        //
        if (!GetFileAttributesEx(afpEntry->szPath, GetFileExInfoStandard, &winAttributes))
        {
            DBGWRITE(dbg_level_info, L"[afp:FPResolveID]GetFileAttributesEx() failed! (%d)\n", GetLastError());
            DBGWRITE(dbg_level_info, L"[afp:FPResolveID]path = %s\n", afpEntry->szPath);
                    
            return( afpObjectNotFound );
        }

        afpError = AFPGetObjectParms(
                        afpReply,
                        afpBitmap,
                        afpVolumeID,
                        afpEntry->parID,
                        afpSession,
                        afpEntry,
                        winAttributes
                        );

        if (!AFP_SUCCESS(afpError)) {

            DBGWRITE(dbg_level_info, L"[afp:FPResolveID]AFPGetObjectParms() failed %d\n", afpError);
        }
    }

    return( afpError );
}


/*
 * FPGetFileDirParms()
 *
 * Description:
 *		Returns file and/or directory parameters.
 *
 * Returns: AFPERROR
 */

AFPERROR FPGetFileDirParms(
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{
    WIN32_FILE_ATTRIBUTE_DATA	winAttributes;
    WCHAR						afpPathname[MAX_AFP_PATH];
    int8						afpAttributes	= 0;
    int16						afpFileBitmap	= 0;
    int16						afpDirBitmap	= 0;
    uint16						afpVolumeID		= 0;
    int32						afpDirID		= 0;
    int8						afpPathtype		= 0;
    AFPERROR					afpError		= AFP_OK;
    LPAFPINDEX					afpEntry		= NULL;
    
    DBGWRITE(dbg_level_all, L"[afp:FPGetFileDirParms]Enter...\n");
        
    //
    //First WORD is afp command and padding byte.
    //
    afpRequest.Advance(sizeof(int16));
    
    //
    //Extract the volume and directory ID's, the bitmaps and the pathtype.
    //
    afpVolumeID		= afpRequest.GetInt16();
    afpDirID		= afpRequest.GetInt32();
    afpFileBitmap	= afpRequest.GetInt16();
    afpDirBitmap	= afpRequest.GetInt16();
    afpPathtype		= afpRequest.GetInt8();

    //
    //The client must have previously opened the volume we are operating on.
    //
    if (!((AFPSession*)afpSession->afpSessionObject)->SessionHasVolumeOpen(afpVolumeID))
    {
        //
        //The volume has not been opened, we need to return error.
        //
        return( afpParmErr );
    }

    //
    //Extract the pathname to the file or directory.
    //
    if (!AFP_SUCCESS(afpRequest.GetString(afpPathname, sizeof(afpPathname), true, afpPathtype))) {

            return( afpParmErr );
    }
        
    //
    //Get the entry for this item in the afp index.
    //
    afpError = AFPIndexFindEntryWithVolIDAndPath(afpVolumeID, afpDirID, afpPathname, &afpEntry);
    
    if (!AFP_SUCCESS(afpError))
    {
        DBGWRITE(dbg_level_all, 
                L"[AFP]Entry not found in GetFileDirParms! (dirID = %d)(name = %s)\n",
                afpDirID, afpPathname);
        
        return( afpObjectNotFound );
    }
        
    //
    //Collect attribute data on the file or folder.
    //
    if (!GetFileAttributesEx(afpEntry->szPath, GetFileExInfoStandard, &winAttributes))
    {
        return( afpObjectNotFound );
    }
    
    if (winAttributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
        //
        //Set the flag that tells the client this is a dir.
        //
        afpAttributes |= kFileDirIsDir;
    }

    //
    //We don't support the following bitmaps, so we clear them. Note that
    //we don't clear the KFPProDos bit in AFP3.x since it is now the unicode
    //name bit.
    //
    if (afpSession->bAFPVersion < afpVersion30)
    {
        if (afpDirBitmap & kFPProDos)	afpDirBitmap  &= ~kFPProDos;
        if (afpFileBitmap & kFPProDos)	afpFileBitmap &= ~kFPProDos;
    }
        
    if (afpDirBitmap & kFPDirShortName)		afpDirBitmap  &= ~kFPDirShortName;
    if (afpFileBitmap & kFPShortName)		afpFileBitmap &= ~kFPShortName;

    //
    //We always return copies of the bitmaps and the attributes.
    //
    afpReply.AddInt16(afpFileBitmap);
    afpReply.AddInt16(afpDirBitmap);
    afpReply.AddInt8(afpAttributes);
        
    //
    //If both bitmaps are null, then we are to just return
    //the bitmaps and the attributes.
    //
    if ((afpFileBitmap == kFPFileNone) && (afpDirBitmap == kFPDirNone))
    {			
        return( AFP_OK );
    }
    
    //
    //We add a null byte when adding more parameters to separate the stuff.
    //
    afpReply.AddInt8(0);
    
    //
    //Call the worker routine to do all the dirty  work of getting and
    //packing all the parameters for us.
    //
    afpError = AFPGetObjectParms(
                    afpReply,
                    (winAttributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? afpDirBitmap : afpFileBitmap,
                    afpVolumeID,
                    afpDirID,
                    afpSession,
                    afpEntry,
                    winAttributes
                    );
    
    return( afpError );
}


/*
 * FPEnumerate()
 *
 * Description:
 *		Enumerate all the contents of a directory.
 *
 * Returns: AFPERROR
 */

AFPERROR FPEnumerate(
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{
    unichar				afpPathname[MAX_AFP_PATH];
    int16*				afpActCountSpot	= NULL;
    int8				afpCommand		= 0;
    int16				afpActCount		= 0;
    int8				afpAttributes	= 0;
    int16				afpObjectsFound	= 0;
    uint16				afpVolID		= 0;
    uint32				afpDirID		= 0;
    uint16				afpFileBitmap	= 0;
    uint16				afpDirBitmap	= 0;
    int16				afpReqCount		= 0;
    int32				afpStartIndex	= 0;
    int32				afpMaxReplySize	= 0;
    int8				afpPathType		= 0;
    AFPERROR			afpError		= AFP_OK;
    LPAFPINDEX			afpTargetEntry	= NULL;
    LPAFPINDEX			afpEntry		= NULL;
    AFP_VOLUME_INFO*	afpVolumeInfo	= NULL;

    WIN32_FILE_ATTRIBUTE_DATA	winAttributes;

    DBGWRITE(dbg_level_all, L"[afp:FPEnumerate]Enter...\n");

    //
    //The first word contains the afp command and padding byte.
    //
    afpCommand = afpRequest.GetInt8();
    afpRequest.Advance(sizeof(int8));

    //
    //Extract the enumerate parameters for the API.
    //
    afpVolID		= afpRequest.GetInt16();
    afpDirID		= afpRequest.GetInt32();
    afpFileBitmap	= afpRequest.GetInt16();
    afpDirBitmap	= afpRequest.GetInt16();
    afpReqCount		= afpRequest.GetInt16();

    //
    //In AFP3.1 we may get the new Ext2 call which allows for a
    //larger number of files to be on the volume.
    //
    switch(afpCommand)
    {
        case afpEnumerate:
        case afpEnumerateExt:
            afpStartIndex	= afpRequest.GetInt16();
            afpMaxReplySize	= afpRequest.GetInt16();
            break;
        
        case afpEnumerateExt2:
            afpStartIndex	= afpRequest.GetInt32();
            afpMaxReplySize	= afpRequest.GetInt32();
            break;
        
        default:
            return( afpParmErr );
    }
    
    afpPathType	= afpRequest.GetInt8();

    //
    //The client must have previously opened the volume we are enumerating
    //
    if (!((AFPSession*)afpSession->afpSessionObject)->SessionHasVolumeOpen(afpVolID))
    {
        //
        //The volume has not been opened, we need to return error.
        //
        DBGWRITE(dbg_level_info, L"[afp:FPEnumerate]Volume not opened!\n");
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
    //We don't support the following bitmaps, so we clear them.
    //
    if (afpSession->bAFPVersion < afpVersion30)
    {
        if (afpDirBitmap & kFPDirProDOS)	afpDirBitmap  &= ~kFPDirProDOS;
        if (afpFileBitmap & kFPProDos)		afpFileBitmap &= ~kFPProDos;
    }
    
    if (afpDirBitmap & kFPDirShortName)		afpDirBitmap  &= ~kFPDirShortName;
    if (afpFileBitmap & kFPShortName)		afpFileBitmap &= ~kFPShortName;

    //
    //The first thing we do is add the bitmaps to the reply buffer.
    //
    afpReply.AddInt16(afpFileBitmap);
    afpReply.AddInt16(afpDirBitmap);
    
    //
    //This is where the actual number of bytes in the buffer will
    //be inserted into the buffer.
    //
    afpActCountSpot = (int16*)afpReply.GetCurrentPosPtr();
    afpReply.Advance(sizeof(int16));

    //
    //Get a pointer to the volume info structure for this volume.
    //
    afpVolumeInfo = ((AFPSession*)afpSession->afpSessionObject)->SessionFindOpenVolumeBlock(afpVolID);
    
    //
    //Retrieve the index entry of the directory pointed to by the AFP path.
    //We use it's dirID to determine which files we'll return in the enum.
    //
    afpError = AFPIndexFindEntryWithPath(afpVolumeInfo, afpDirID, afpPathname, &afpTargetEntry);
    
    if (!AFP_SUCCESS(afpError))
    {
        DBGWRITE(dbg_level_info, L"[afp:FPEnumerate]Failed to get index entry (vID = %d, dirID = %d, name = %s)\n",
                    afpVolID,
                    afpDirID,
                    afpPathname
                    );

        return( afpError );
    }

    //
    //Don't let the change notify mechanism do anything yet.
    //
    AutoLock lock(&afpVolumeInfo->csNotifyQueue);

    //
    //Create a temporary index array to hold all the offspring for the
    //target directory.
    //
    std::vector<LPAFPINDEX>* afpTempIndex;
        
    //
    //Now populate the temp array with the offspring in the target dir.
    //
    afpVolumeInfo->index->GetOffspring(afpTargetEntry->objectID, &afpTempIndex);
            
    //
    //Go through the entire index and find entries that have the dirID as their parent.
    //
    for (auto it = afpTempIndex->begin(); it < afpTempIndex->end(); ++it)
    {
        afpEntry = *it;

        #ifdef _DEBUG
        if ((afpEntry->parID == afpTargetEntry->objectID) && (GetFileAttributesEx(afpEntry->szPath, GetFileExInfoStandard, &winAttributes) == 0))
        {
            DBGWRITE(dbg_level_info, L"[afp:FPEnumerate]GetFileAttributesEx() failed (%d)\n", GetLastError());
            DBGWRITE(dbg_level_info, L"[afp:FPEnumerate]File = %s\n", afpEntry->szPath);
        }
        #endif
        
        //
        //We don't include some files (like the desktop db) since they
        //are server specific and the macs shouldn't see or edit them.
        //
        if (!lstrcmp(afpEntry->szObjectName, DESKTOP_FILE_NAME)) {
            continue;
        }
        
        if (GetFileAttributesEx(afpEntry->szPath, GetFileExInfoStandard, &winAttributes))
        {
            bool		afpIsDirectory;
            uint8		tempBuffer[1024];
            AfpBuffer	afpParmsBuffer(tempBuffer);

            //
            //This is how many total objects we found during our search.
            //
            afpObjectsFound++;
            
            //
            //If we haven't hit the start index yet, continue on...
            //
            if (afpObjectsFound < afpStartIndex) {
                continue;
            }
            
            DBGWRITE(dbg_level_all, L"[afp:FPEnumerate]Enumerating %s\n", afpEntry->szObjectName);
            
            afpIsDirectory = (afpEntry->flags & FLAG_INDEX_ISDIR);

            //
            //Check to make sure we don't include items not to be included
            //in the return enumeration.
            //
            if (	((afpDirBitmap == kFPDirNone) && (afpIsDirectory))		||
                    ((afpFileBitmap == kFPFileNone) && (!afpIsDirectory))	)
            {
                continue;
            }
                            
            //
            //Call the appropriate GetXXXParms calls.
            //
            afpParmsBuffer.Rewind();
            afpAttributes = 0;

            afpError = AFPGetObjectParms(
                            afpParmsBuffer,
                            (afpIsDirectory) ? afpDirBitmap : afpFileBitmap,
                            afpVolID,
                            afpDirID,
                            afpSession,
                            afpEntry,
                            winAttributes
                            );
            
            if (afpIsDirectory)
            {
                //
                //Set the afp flag that marks this as a directory.
                //
                afpAttributes |= kFileDirIsDir;
            }
                    
            if (AFP_SUCCESS(afpError))
            {
                uint8*	structLen 		= NULL;
                int16*	extStructLen	= NULL;
                size_t	sizeRequired;
                int16	len;
                
                //
                //Check to make sure there is enough room in the buffer to add the
                //file/dir information.
                //
                sizeRequired = afpParmsBuffer.GetDataLength() + afpReply.GetDataLength();
                
                //
                //There are items after the file dirs parms that are added to the buffer,
                //so we add a little extra to be safe.
                //
                sizeRequired += (4);
                
                //
                //wMaxReplySize is the maximum size our return buffer can be.
                //
                if (sizeRequired >= (size_t)afpMaxReplySize)
                {
                    //
                    //We need to stay inside the callers max reply size in the request.
                    //
                    DBGWRITE(dbg_level_all, L"[afp:FPEnumerate]Breaking out, size required too big!\n");
                    break;
                }
                
                //
                //Make sure we didn't get bigger then our reply buffer can handle.
                //
                if (sizeRequired > afpReply.GetBufferSize())
                {
                    //
                    //This should never happen (yeah right). In case it does, just
                    //bail from here.
                    //
                    DBGWRITE(dbg_level_all, L"[afp:FPEnumerate]Breaking out, reply buffer too small!\n");
                    break;
                }
                
                //
                //Increment the count of the # of objects we're returning.
                //
                afpActCount++;
                
                //
                //The first byte (or word for AFP3.x) contains the struct len.
                //
                if (afpCommand > afpEnumerate)
                {
                    extStructLen = (int16*)afpReply.GetCurrentPosPtr();
                    afpReply.Advance(sizeof(int16));
                }
                else
                {
                    structLen = afpReply.GetCurrentPosPtr();
                    afpReply.Advance(sizeof(int8));
                }
                
                //
                //Push in the attributes flag (dir, yes/no)
                //
                afpReply.AddInt8(afpAttributes);
                
                //
                //For AFP3.x, we add an extra null byte for padding here.
                //
                if (afpCommand > afpEnumerate) {
                    afpReply.AddInt8(0);
                }
                
                //
                //Insert the FDP parameters into the reply buffer.
                //
                afpReply.AddRawData(afpParmsBuffer.GetBuffer(), (int16)afpParmsBuffer.GetDataLength());
                
                //
                //Set the structure length byte in the buffer. The extra bytes are
                //for the first parms in the structure (structLen, attributes and padding).
                //
                len = (int16)afpParmsBuffer.GetDataLength() + ((afpCommand > afpEnumerate) ? 4 : 2);
                
                //
                //This entry must end on an even boundary.
                //
                if (len % 2)
                {
                    afpReply.AddInt8(0);
                    len++;
                }
                
                //
                //AFP3.x uses a word to describe the struct length vs. a byte
                //
                if (afpCommand > afpEnumerate)
                {
                    *extStructLen = htons(len);
                }
                else {
                    *structLen = (int8)len;
                }
            }
            else
            {
                //
                //We had an error looking up the parms of an object.
                //
                DBGWRITE(dbg_level_info, L"[afp:Enumerate]Error finding parms for object\n");
            }
            
            //
            //If we've gotten as much data as the client requested, then
            //exit this beast.
            //
            if ((afpActCount >= afpReqCount) || (afpReply.GetDataLength() >= afpMaxReplySize))
            {
                break;
            }
        }
    }
    
    //
    //Set the actual number of objects found and in the buffer.
    //
    *afpActCountSpot = htons(afpActCount);

    DBGWRITE(dbg_level_all, L"[afp:Enumerate]afpActCount = %d\n", afpActCount);
    DBGWRITE(dbg_level_all, L"[afp:Enumerate]Returning: %d\n", (afpActCount == 0) ? afpObjectNotFound : AFP_OK);
    
    delete afpTempIndex;
    
    return((afpActCount == 0) ? afpObjectNotFound : AFP_OK);
}


/*
 * FPSetFileDirParms()
 *
 * Description:
 *		Set the file or directory parameters.
 *
 * Returns: AFPERROR
 */

AFPERROR FPSetFileDirParms(
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{
    UNREFERENCED_PARAMETER(afpReply);

    unichar		afpPathname[MAX_AFP_PATH];
    uint16		afpVolID	= 0;
    uint16		afpBitmap	= 0;
    int32		afpDirID	= 0;
    int8		afpPathType	= 0;
    int8		afpCommand	= 0;
    AFPERROR	afpError	= AFP_OK;
    LPAFPINDEX	afpEntry	= NULL;

    WIN32_FILE_ATTRIBUTE_DATA	winAttributes;

    DBGWRITE(dbg_level_all, L"[afp:FPSetFileDirParms]Enter...\n");

    //
    //The first word is afp command and padding byte.
    //
    afpCommand = afpRequest.GetInt8();
    afpRequest.Advance(sizeof(int8));
    
    //
    //Extract the volume and dir ID's as well as the bitmap that
    //tells what we're setting.
    //
    afpVolID	= afpRequest.GetInt16();
    afpDirID	= afpRequest.GetInt32();
    afpBitmap	= afpRequest.GetInt16();
    afpPathType	= afpRequest.GetInt8();

    //
    //The client must have previously opened the volume we are operating on.
    //
    if (!((AFPSession*)afpSession->afpSessionObject)->SessionHasVolumeOpen(afpVolID))
    {
        //
        //The volume has not been opened, we need to return error.
        //
        return( afpParmErr );
    }

    //
    //Make sure the volume is not marked read-only.
    //
    afpError = AFPVolumeWriteAccessCheck(afpVolID);

    if (!AFP_SUCCESS(afpError))
    {
        return( afpError );
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
    //The remaining parameters for the call must start on an
    //even boundary.
    //
    afpRequest.AdvanceIfOdd();

    //
    //Verify the bitmap contains valid settings, FPSetFileDirParms doesn't
    //support the following bitmaps. The user is supposed to use
    //FPSetFileParms or FPSetDirParms for these.
    //
    switch(afpCommand)
    {
        case afpSetFlDrParms:
            if ((afpBitmap & (	kFPParentID	  | kFPLongName | kFPShortName |
                                kFPFileNum    | kFPDFLen    | kFPRFLen	   |
                                kFPDirGroupID | kFPDirAccess | kFPDirUnicodeName |
                                kFPExtDataForkLen | kFPExtRsrcForkLen)))
            {
                DBGWRITE(dbg_level_all, L"[afp:FPSetFileDirParms]Bad bitmap!\n");
                return( afpBitmapErr );
            }
            break;
            
        case afpSetDirParms:
            if ((afpBitmap & (	kFPParentID	   | kFPLongName | kFPShortName |
                                kFPDirOffCount | kFPDirID | kFPDirUnicodeName)))
            {
                DBGWRITE(dbg_level_all, L"[afp:FPSetDirParms]Bad bitmap!\n");
                return( afpBitmapErr );
            }
            break;
        
        case afpSetFileParms:
            if ((afpBitmap & (	kFPParentID	| kFPLongName | kFPShortName |
                                kFPFileNum  | kFPDFLen    | kFPRFLen     |
                                kFPExtDataForkLen | kFPUnicodeName | kFPExtRsrcForkLen)))
            {
                DBGWRITE(dbg_level_info, L"[afp:FPSetFileParms]Bad bitmap!\n");
                return( afpBitmapErr );
            }
            break;
            
        default:
            ASSERT(0);
            return( afpCallNotSupported );
    }


    //
    //Now get the index entry for the object itself.
    //
    afpError = AFPIndexFindEntryWithVolIDAndPath(afpVolID, afpDirID, afpPathname, &afpEntry);
    
    if (!AFP_SUCCESS(afpError))
    {
        //
        //We couldn't find the requested object.
        //
        DBGWRITE(dbg_level_info,
                    L"[afp:FPSetFileParms]Index entry not found in FPSetFileDirParms! (dirID = %d  path = %s)\n", 
                    afpDirID, afpPathname);
        
        return( afpError );
    }
    
    //
    //Collect attribute data on the file or folder. We need to determine
    //whether this is a dir of a file.
    //
    if (!GetFileAttributesEx(afpEntry->szPath, GetFileExInfoStandard, &winAttributes))
    {
        DBGWRITE(dbg_level_info, L"[afp:FPSetFileParms]GetFileAttributesEx() failed! (%d)\n", GetLastError());
        return( afpObjectNotFound );
    }
    
    afpError = AFPSetFileDirParms(
                    afpBitmap,
                    afpEntry,
                    (PCHAR)afpRequest.GetCurrentPosPtr(),
                    winAttributes.dwFileAttributes
                    );
    
    return( afpError );
}


/*
 * FPCreateDir()
 *
 * Description:
 *		Create a new directory on the volume in the specified dir.
 *
 * Returns: AFPERROR
 */

AFPERROR FPCreateDir(
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{
    AFPCHARMAPARRAY		charMap;
    WCHAR				afpPathname[MAX_AFP_PATH];
    WCHAR				winPath[MAX_PATH];
    int16				afpVolumeID	= 0;
    uint32				afpDirID	= 0;
    int8				afpPathType	= 0;
    AFPERROR			afpError	= AFP_OK;
    AFP_VOLUME_INFO*	afpVolume	= NULL;
    LPAFPINDEX			afpEntry	= NULL;
    
    DBGWRITE(dbg_level_all, L"[AFP]FPCreateDir...\n");
        
    //
    //Parameters start after first word (afp cmd and 0 byte)
    //
    afpRequest.Advance(sizeof(int16));
    
    //
    //Extract the volID, dirID and pathtype parameters for the call.
    //
    afpVolumeID		= afpRequest.GetInt16();
    afpDirID		= afpRequest.GetInt32();
    afpPathType		= afpRequest.GetInt8();

    //
    //Find the volume info block that we need to access the index.
    //
    afpVolume = ((AFPSession*)afpSession->afpSessionObject)->SessionFindOpenVolumeBlock(afpVolumeID);
    if (afpVolume == NULL)
    {
        DBGWRITE(dbg_level_info, L"[afp:FPCreateDir]Bad volume ID in FPCreateDir!\n");
        return( afpParmErr );
    }

    //
    //The client must have previously opened the volume we are creating
    //the directory on.
    //
    if (!((AFPSession*)afpSession->afpSessionObject)->SessionHasVolumeOpen(afpVolumeID))
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
    //Extract the AFP pathname to the new directory to create.
    //
    afpError = afpRequest.GetString(afpPathname, sizeof(afpPathname), true, afpPathType, charMap);

    if (!AFP_SUCCESS(afpError))
    {
        DBGWRITE(dbg_level_info, L"[afp:FPCreateDir]GetString failed for path!\n");
        return( afpError );
    }

    //
    //Pathname cannot be null since it contains the new dir name.
    //
    if (lstrlen(afpPathname) == 0)
    {
        DBGWRITE(dbg_level_info, L"[afp:FPCreateDir]No pathname in FPCreateDir!\n");
        return( afpParmErr );
    }
    
    //
    //Get the directory entry for the directory where we are creating the
    //new one.
    //
    afpError = AFPIndexFindEntryWithPath(afpVolume, afpDirID, NULL, &afpEntry);
        
    if (!AFP_SUCCESS(afpError))
    {
        DBGWRITE(dbg_level_info, L"[afp:FPCreateDir]Path not found in FPCreateDir!\n");
        return( afpError );
    }
    
    //
    //Build the path to the new directory using the parent dir's entry.
    //
    StringCbPrintf(winPath, sizeof(winPath), TEXT("%s\\%s"), afpEntry->szPath, afpPathname);
    
    //
    //See if a directory or file by that name already exists.
    //
    if (GetFileAttributes(winPath) != AFP_MAX_DWORD)
    {
        return( afpObjectExists );
    }

    //
    //Lock down so the notify thread and us don't compete.
    //
    AutoLock lock(&afpVolume->csNotifyQueue);

    //
    //Impersonate the logged on user. This makes sure only those who
    //are allowed can create directories.
    //
    if (!AFPImpersonateUser(afpSession->hUserToken))
    {
        return( afpAccessDenied );
    }

    //
    //Create the new directory using the Win32 API.
    //
    afpError = (CreateDirectory(winPath, NULL)) ? AFP_OK : AFPMapWinErrorToAFP(GetLastError(), static_cast<DWORD>(afpMiscErr));

    //
    //Revert back to the systems control and context.
    //
    AFPRevertToSelf(afpSession->hUserToken);

    if (AFP_SUCCESS(afpError))
    {
        afpEntry = NULL;

        //
        //Add the new directory to the afp index. If the call succeeds, pEntry
        //will contain a pointer to the new index entry data.
        //
        afpEntry = AFPIndexAddEntry(
                    NULL,
                    afpVolumeID,
                    afpDirID,
                    afpPathname,
                    winPath,
                    true,
                    &afpDirID
                    );

        if (afpEntry != NULL)
        {
            //
            //Make sure there is a finder info stream created for later use.
            //
            AFPCheckForAndAddFinderInfoStream(afpEntry);
            
            //
            //Copy the character conversion mapping table into the
            //object's index entry.
            //
            AFPAddCharMapToIndexEntry(afpEntry, charMap);
        }

        //
        //Mark the volume dirty to so other clients will see the changes
        //
        AFPMarkVolumeDirty(afpVolume->wVolID);

        //
        //Now build the reply block to send back to the client.
        //
        afpReply.AddInt32(afpDirID);
    }

    return( afpError );
}


/*
 * FPDelete()
 *
 * Description:
 *		Delete a file or directory.
 *
 * Returns: AFPERROR
 */

AFPERROR FPDelete(
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{
    UNREFERENCED_PARAMETER(afpReply);

    unichar				afpPathname[MAX_AFP_PATH];
    LPAFPINDEX			afpEntry		= NULL;
    AFP_VOLUME_INFO*	afpVolumeInfo	= NULL;
    int16				afpVolumeID		= 0;
    int32				afpDirID		= 0;
    int8				afpPathType		= 0;
    AFPERROR			afpError		= AFP_OK;

    DBGWRITE(dbg_level_all, L"[afp:FPDelete]Enter\n");
    
    //
    //First word is command and padding.
    //
    afpRequest.Advance(sizeof(int16));
    
    afpVolumeID	= afpRequest.GetInt16();
    afpDirID	= afpRequest.GetInt32();
    afpPathType	= afpRequest.GetInt8();
    
    //
    //The client must have previously opened the volume we are operating on.
    //
    if (!((AFPSession*)afpSession->afpSessionObject)->SessionHasVolumeOpen(afpVolumeID))
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
    afpError = afpRequest.GetString(afpPathname, sizeof(afpPathname), true, afpPathType);
    
    if (!AFP_SUCCESS(afpError))
    {
        return( afpError );
    }
    
    //
    //Pathname cannot be null since it contains the file/dir to delete.
    //
    if (lstrlen(afpPathname) == 0)
    {
        DBGWRITE(dbg_level_info, L"[afp:FPDelete]Null pathname!\n");
        return( afpParmErr );
    }

    afpVolumeInfo = ((AFPSession*)afpSession->afpSessionObject)->SessionFindOpenVolumeBlock(afpVolumeID);

    if (afpVolumeInfo == NULL)
    {
        DBGWRITE(dbg_level_info, L"[afp:FPDelete]Bad volume ID! (%d)\n", afpVolumeID);
        return( afpParmErr );
    }

    //
    //Get the index entry for the item we want to delete.
    //
    afpError = AFPIndexFindEntryWithPath(afpVolumeInfo, afpDirID, afpPathname, &afpEntry);
    
    if (!AFP_SUCCESS(afpError))
    {
        DBGWRITE(dbg_level_info, L"[afp:FPDelete]Index entry not found! (%s)\n", afpPathname);
        return( afpError );
    }

    //
    //Don't compete with the notify thread.
    //
    AutoLock lock(&afpVolumeInfo->csNotifyQueue);
        
    //
    //Impersonate the logged on user. This makes sure only those who
    //are allowed can delete files.
    //
    if (!AFPImpersonateUser(afpSession->hUserToken))
    {
        return( afpAccessDenied );
    }

    //
    //We take 2 different approaches to deleting the object based on whether it's
    //a directory or a file.
    //
    if (afpEntry->flags & FLAG_INDEX_ISDIR)
    {
        if (!RemoveDirectory(afpEntry->szPath))
        {
            DBGWRITE(dbg_level_info,
                    L"[afp:FPDelete]RemoveDirectory() failed on %s! (%d)\n",
                    afpEntry->szPath,
                    GetLastError()
                    );
            
            afpError = AFPMapWinErrorToAFP(GetLastError(), static_cast<DWORD>(afpParmErr));
        }
    }
    else
    {
        if (!DeleteFile(afpEntry->szPath))
        {
            DBGWRITE(dbg_level_info,
                    L"[afp:FPDelete]DeleteFile failed on %s! (%d)\n",
                    afpEntry->szPath,
                    GetLastError()
                    );
            
            afpError = AFPMapWinErrorToAFP(GetLastError(), static_cast<DWORD>(afpParmErr));
        }
    }

    AFPRevertToSelf(afpSession->hUserToken);
    
    //
    //If we successfully deleted the file or directory, we need to
    //remove it from the afp index.
    //
    if (AFP_SUCCESS(afpError)) {

        AFPIndexRemoveEntry(afpVolumeInfo, afpEntry, true);
    }

    AFPMarkVolumeDirty(afpVolumeID);

    return( afpError );
}


/*
 * FPCopyFile()
 *
 * Description:
 *		Copy a file from one location to another.
 *
 * Returns: AFPERROR
 */

AFPERROR FPCopyFile(
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{
    UNREFERENCED_PARAMETER(afpReply);

    unichar				afpSrcPathname[MAX_AFP_PATH];
    unichar				afpDstPathname[MAX_AFP_PATH];
    unichar				afpNewName[MAX_AFP_NAME];
    unichar				newWinPath[MAX_PATH];
    AFPCHARMAPARRAY		destCharMap;
    AFPCHARMAPARRAY		newCharMap;
    LPAFPINDEX			afpSrcEntry		= NULL;
    LPAFPINDEX			afpDstEntry		= NULL;
    int16				afpSrcVolID		= 0;
    int32				afpSrcDirID		= 0;
    int16				afpDstVolID		= 0;
    int32				afpDstDirID		= 0;
    int8				afpPathType		= 0;
    AFPERROR			afpError		= AFP_OK;
    
    DBGWRITE(dbg_level_all, L"[afp:FPCopyFile]Enter...\n");
    
    //
    //Skip the first byte which is afp command.
    //
    afpRequest.Advance(sizeof(int16));

    afpSrcVolID		= afpRequest.GetInt16();
    afpSrcDirID		= afpRequest.GetInt32();
    afpDstVolID		= afpRequest.GetInt16();
    afpDstDirID		= afpRequest.GetInt32();
    
    //
    //Get the pathname of the object we're copying.
    //
    afpPathType	= afpRequest.GetInt8();
    afpError 	= afpRequest.GetString(
                                afpSrcPathname, 
                                sizeof(afpSrcPathname), 
                                true, 
                                afpPathType
                                );
    
    if (!AFP_SUCCESS(afpError))
    {
        return( afpError );
    }
    
    //
    //Get the destination pathname we're copying to
    //
    afpPathType		= afpRequest.GetInt8();
    afpError 		= afpRequest.GetString(
                                afpDstPathname, 
                                sizeof(afpDstPathname), 
                                true, 
                                afpPathType,
                                destCharMap
                                );
    
    if (!AFP_SUCCESS(afpError))
    {
        return( afpError );
    }
    
    //
    //And now the [optional] new name for the file
    //
    afpPathType		= afpRequest.GetInt8();
    afpError 		= afpRequest.GetString(
                                afpNewName, 
                                sizeof(afpNewName), 
                                true, 
                                afpPathType,
                                newCharMap
                                );
    
    if (!AFP_SUCCESS(afpError))
    {
        return( afpError );
    }

    //
    //Make sure the client has opened the volumes that the file copy
    //is happening on.
    //
    if (	(!((AFPSession*)afpSession->afpSessionObject)->SessionHasVolumeOpen(afpSrcVolID))	||
            (!((AFPSession*)afpSession->afpSessionObject)->SessionHasVolumeOpen(afpDstVolID))	)
    {
        //
        //The user has not opened one or both of the volumes for access.
        //
        return( afpParmErr );
    }

    //
    //Pathname cannot be null since it contains the name of the file to copy
    //
    if (lstrlen(afpSrcPathname) == 0)
    {
        DBGWRITE(dbg_level_info, L"[afp:FPCopyFile]NULL source pathname provided\n");
        return( afpParmErr );
    }

    //
    //Get the index entry for the source file item that we want to copy
    //
    afpError = AFPIndexFindEntryWithVolIDAndPath(afpSrcVolID, afpSrcDirID, afpSrcPathname, &afpSrcEntry);
    
    if (!AFP_SUCCESS(afpError))
    {
        DBGWRITE(dbg_level_info, L"[afp:FPCopyFile]Source index entry not found!\n");
        return( afpError );
    }

    //
    //Make sure the path doesn't point to a directory, we only work with
    //files as per the spec.
    //
    if (afpSrcEntry->flags & FLAG_INDEX_ISDIR)
    {
        return( afpObjectTypeErr );
    }

    //
    //Get the index entry for the destination directory.
    //
    afpError = AFPIndexFindEntryWithVolIDAndPath(afpDstVolID, afpDstDirID, afpDstPathname, &afpDstEntry);

    if (!AFP_SUCCESS(afpError))
    {
        DBGWRITE(dbg_level_info, L"[afp:FPCopyFile]Destination directory index entry not found!\n");
        return( afpError );
    }

    //
    //Build the path to the new location. If we were supplied a new name,
    //then use it, otherwise use the old file name.
    //
    if (lstrlen(afpNewName) > 0)
    {
        StringCbPrintf(newWinPath, sizeof(newWinPath), TEXT("%s\\%s"), afpDstEntry->szPath, afpNewName);
    }
    else
    {
        StringCbPrintf(newWinPath, sizeof(newWinPath), TEXT("%s\\%s"), afpDstEntry->szPath, afpSrcEntry->szObjectName);
    }
    
    DBGWRITE(dbg_level_all, L"[afp:FPCopyFile]copying from %s\n", afpSrcEntry->szPath);
    DBGWRITE(dbg_level_all, L"[afp:FPCopyFile]copying to %s\n", newWinPath);
    
    //
    //Silence the notify thread while we do our work.
    //
    AFP_VOLUME_INFO* volumeInfo = nullptr;
    AFPGetVolumeInfoBlock(nullptr, afpDstVolID, &volumeInfo);
    AutoLock lock(&volumeInfo->csNotifyQueue);

    //
    //Setup the correct impersonation for this API call. We have to
    //have the right credentials for copy file.
    //
    if (!AFPImpersonateUser(afpSession->hUserToken))
    {
        //
        //Impersonation failed, don't allow any further operations.
        //
        return( afpAccessDenied );
    }

    //
    //Call the Windows API to do the dirty work of copying the file for us.
    //
    BOOL fSucceeded = CopyFile(afpSrcEntry->szPath, newWinPath, true);

    if (!fSucceeded)
    {
        DBGWRITE(dbg_level_info, L"[afp:FPCopyFile]CopyFile() failed (%d) (%s)!\n", GetLastError(), newWinPath);

        afpError = AFPMapWinErrorToAFP(GetLastError(), static_cast<DWORD>(afpParmErr));
    }

    AFPRevertToSelf(afpSession->hUserToken);

    if (AFP_SUCCESS(afpError))
    {
        LPAFPINDEX	pNewIndex = NULL;

        //
        //Add the new file to the afp index for tracking purposes.
        //
        pNewIndex = AFPIndexAddEntry(
                        NULL,
                        afpDstVolID,
                        afpDstEntry->objectID,
                        (lstrlen(afpNewName) > 0) ? afpNewName : afpSrcEntry->szObjectName,
                        newWinPath,
                        false,
                        NULL
                        );

        if (pNewIndex != NULL)
        {
            if (lstrlen(afpNewName) > 0)
            {
                AFPAddCharMapToIndexEntry(afpDstEntry, newCharMap);
            }
            else
            {
                AFPAddCharMapToIndexEntry(afpDstEntry, destCharMap);
            }

            AFPMarkVolumeDirty(afpDstVolID);
        }
        else
        {
            //
            //For some reason we failed to add the new index entry. There's not
            //much we can do at this point. Hopefully, the notify will add the
            //new entry.
            //
            DBGWRITE(dbg_level_info, L"[afp:FPCopyFile]Failed to add new entry!\n");
        }
    }

    return( afpError );
}


/*
 * FPMoveAndRename()
 *
 * Description:
 *		Move and optionally rename a file.
 *
 * Returns: AFPERROR
 */

AFPERROR FPMoveAndRename(
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{
    UNREFERENCED_PARAMETER(afpReply);

    unichar			afpPathname[MAX_AFP_PATH];
    unichar			afpNewPathname[MAX_AFP_NAME];
    unichar			winDstPath[MAX_PATH];
    AFPCHARMAP		destCharMap[MAX_DIR_NAME];
    AFPCHARMAP		newCharMap[MAX_DIR_NAME];
    LPAFPINDEX		afpSrcEntry;
    LPAFPINDEX		afpDstEntry;
    int16			afpVolumeID		= 0;
    int32			afpSrcDirID		= 0;
    int32			afpDstDirID		= 0;
    int8			afpPathType		= 0;
    int16			afpAttributes	= 0;
    AFPERROR		afpError		= AFP_OK;

    WIN32_FILE_ATTRIBUTE_DATA	winAttributes;
    
    DBGWRITE(dbg_level_all, L"[afp:FPMoveAndRename]Enter\n");
    
    //
    //The first byte contains the afp command.
    //
    afpRequest.Advance(sizeof(int16));
    
    afpVolumeID		= afpRequest.GetInt16();
    afpSrcDirID		= afpRequest.GetInt32();
    afpDstDirID		= afpRequest.GetInt32();
    afpPathType		= afpRequest.GetInt8();

    //
    //The client must have previously opened the volume we are creating
    //the directory on.
    //
    if (!((AFPSession*)afpSession->afpSessionObject)->SessionHasVolumeOpen(afpVolumeID))
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

    //*****************
    //Beging by getting the source file/dir entry.
    //*****************
            
    //
    //Get the pathname of the object we're working on.
    //
    afpError = afpRequest.GetString(afpPathname, sizeof(afpPathname), true, afpPathType);
    
    if (!AFP_SUCCESS(afpError))
    {
        return( afpError );
    }

    //
    //Get the index entry for the item we are moving
    //
    afpError = AFPIndexFindEntryWithVolIDAndPath(afpVolumeID, afpSrcDirID, afpPathname, &afpSrcEntry);
        
    if (!AFP_SUCCESS(afpError))
    {
        DBGWRITE(dbg_level_info, L"[afp:FPMoveAndRename]Src Index entry not found!\n");
        return( afpError );
    }

    //
    //Check to make sure that the exiting item can be renamed.
    //
    if (!GetFileAttributesEx(afpSrcEntry->szPath, GetFileExInfoStandard, &winAttributes))
    {
        DBGWRITE(dbg_level_info, L"[afp:FPMoveAndRename]Could not get attributes on src entry!\n");
        return( afpObjectNotFound );
    }

    afpAttributes = AFPWinToAFPAttributes(winAttributes.dwFileAttributes, NULL);

    if (afpAttributes & (kFileDeleteInhibit | kFileRenameInhibit))
    {
        DBGWRITE(dbg_level_info, L"[afp:FPMoveAndRename]Cannot rename, object locked!\n");
        return( afpObjectLocked );
    }
    
    //*****************
    //Now get the destination directory we're moving to
    //*****************
    
    afpPathType = afpRequest.GetInt8();
    
    //
    //Get the pathname of the object we're working on.
    //
    afpError = afpRequest.GetString(afpPathname, sizeof(afpPathname), true, afpPathType, destCharMap);
    
    if (!AFP_SUCCESS(afpError))
    {
        return( afpError );
    }

    //
    //Get the index entry for the directory we're moving the object to.
    //
    afpError = AFPIndexFindEntryWithVolIDAndPath(afpVolumeID, afpDstDirID, afpPathname, &afpDstEntry);
    
    if (!AFP_SUCCESS(afpError))
    {
        DBGWRITE(dbg_level_info, L"[afp:FPMoveAndRename]Dst dir Index entry not found!\n");
        return( afpError );
    }

    //
    //Get the (optionally) supplied new name after the move.
    //
    afpPathType = afpRequest.GetInt8();
    
    afpError = afpRequest.GetString(afpNewPathname, sizeof(afpNewPathname), true, afpPathType, newCharMap);
    
    if (!AFP_SUCCESS(afpError))
    {
        return( afpError );
    }

    //
    //Build the correct path to the newly moved and/or renamed object.
    //
    if (lstrlen(afpNewPathname) > 0)
    {
        StringCbPrintf(winDstPath, sizeof(winDstPath), TEXT("%s\\%s"), afpDstEntry->szPath, afpNewPathname);
        StringCbCopy(afpSrcEntry->szObjectName, sizeof(afpSrcEntry->szObjectName), afpNewPathname);
    }
    else
    {
        StringCbPrintf(winDstPath, sizeof(winDstPath), TEXT("%s\\%s"), afpDstEntry->szPath, afpSrcEntry->szObjectName);
    }

    //
    //Prevent file notifications otherwise we'll get conflicts.
    //
    AFP_VOLUME_INFO* volumeInfo = nullptr;
    AFPGetVolumeInfoBlock(nullptr, afpVolumeID, &volumeInfo);
    AutoLock lock(&volumeInfo->csNotifyQueue);

    
    DBGWRITE(dbg_level_info, L"[afp:FPMoveAndRename]Renaming to new path = %s\n", winDstPath);

    //
    //Impersonate the current user for permissions sakes.
    //
    if (!AFPImpersonateUser(afpSession->hUserToken))
    {
        //
        //Impersonation failed, don't allow any further operations.
        //
        return( afpAccessDenied );
    }

    //
    //Now use the windows API to perform the actual movement in the filesystem.
    //
    if (!MoveFile(afpSrcEntry->szPath, winDstPath))
    {
        DBGWRITE(dbg_level_info, L"[afp:FPMoveAndRename]MoveFile() failed! (%d)\n", GetLastError());
        
        afpError = AFPMapWinErrorToAFP(GetLastError(), static_cast<DWORD>(afpParmErr));
    }

    AFPRevertToSelf(afpSession->hUserToken);
    
    if (AFP_SUCCESS(afpError))
    {
        //
        //Now we must update the afp index for the moved directory.
        //
        AFPIndexUpdateEntry(
                NULL,
                afpVolumeID,
                afpSrcEntry,
                afpDstEntry,
                NULL
                );
                
        AFPMarkVolumeDirty(afpVolumeID);

        //
        //If we changed the name of the object, then we need to update
        //the extended char mapping table.
        //
        if (lstrlen(afpNewPathname) > 0)
        {
            AFPAddCharMapToIndexEntry(afpSrcEntry, newCharMap);
        }
        else
        {
            AFPAddCharMapToIndexEntry(afpSrcEntry, destCharMap);
        }
    }
  
    return( afpError );
}


/*
 * FPRename()
 *
 * Description:
 *		Rename a file or directory.
 *
 * Returns: AFPERROR
 */

AFPERROR FPRename(
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{
    UNREFERENCED_PARAMETER(afpReply);

    unichar			afpPathname[MAX_AFP_PATH];
    unichar			winPath[MAX_PATH];
    AFPCHARMAP		newCharMap[MAX_DIR_NAME];
    LPAFPINDEX		afpEntry		= NULL;
    int16			afpVolumeID		= 0;
    int32			afpDirID		= 0;
    int8			afpPathType		= 0;
    int16			afpAttributes	= 0;
    AFPERROR		afpError		= AFP_OK;

    WIN32_FILE_ATTRIBUTE_DATA	winAttributes;
    
    DBGWRITE(dbg_level_all, L"[afp:FPRename]Enter\n");
    
    //
    //First word is command and padding.
    //
    afpRequest.Advance(sizeof(int16));
    
    afpVolumeID	= afpRequest.GetInt16();
    afpDirID	= afpRequest.GetInt32();
    afpPathType	= afpRequest.GetInt8();

    //
    //The client must have previously opened the volume we are creating
    //the directory on.
    //
    if (!((AFPSession*)afpSession->afpSessionObject)->SessionHasVolumeOpen(afpVolumeID))
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
    afpError = afpRequest.GetString(afpPathname, sizeof(afpPathname), true, afpPathType);
    
    if (!AFP_SUCCESS(afpError))
    {
        return( afpError );
    }

    //
    //Get the entry pointer in the index for this item we're renaming.
    //
    afpError = AFPIndexFindEntryWithVolIDAndPath(afpVolumeID, afpDirID, afpPathname, &afpEntry);
        
    if (!AFP_SUCCESS(afpError))
    {
        //
        //We couldn't find the requested object.
        //
        DBGWRITE(dbg_level_info, L"[afp:FPRename]Dst entry not found! (%s)\n", afpPathname);
        return( afpError );
    }

    //
    //Check to make sure that the exiting item can be renamed.
    //
    if (!GetFileAttributesEx(afpEntry->szPath, GetFileExInfoStandard, &winAttributes))
    {
        DBGWRITE(dbg_level_info, L"[afp:FPMoveAndRename]Could not get attributes on src entry!\n");
        return( afpObjectNotFound );
    }

    afpAttributes = AFPWinToAFPAttributes(winAttributes.dwFileAttributes, NULL);

    if (afpAttributes & (kFileRenameInhibit))
    {
        DBGWRITE(dbg_level_info, L"[afp:FPMoveAndRename]Cannot rename, object locked!\n");
        return( afpObjectLocked );
    }

    //
    //Get the pathtype for the new name.
    //
    afpPathType = afpRequest.GetInt8();
    
    //
    //Get the new name of the object (required).
    //
    afpError = afpRequest.GetString(afpPathname, sizeof(afpPathname), true, afpPathType, newCharMap);
    
    if ((!AFP_SUCCESS(afpError)) || (lstrlen(afpPathname) == 0))
    {
        return( afpError );
    }

    //
    //Get the new full path to the renamed file.
    //
    StringCbPrintf(winPath, sizeof(winPath), TEXT("%s\\%s"), ((LPAFPINDEX)afpEntry->parEntry)->szPath, afpPathname); 

    DBGWRITE(dbg_level_info, L"[afp:FPRename]Renaming to new path = %s\n", winPath);

    //
    //Prevent the notification routines from interfering with us.
    //
    AFP_VOLUME_INFO* volumeInfo = nullptr;
    AFPGetVolumeInfoBlock(nullptr, afpVolumeID, &volumeInfo);
    AutoLock lock(&volumeInfo->csNotifyQueue);


    //
    //Impersonate the currently logged on user.
    //
    if (!AFPImpersonateUser(afpSession->hUserToken))
    {
        //
        //Impersonation failed, don't allow any further operations.
        //
        return( afpAccessDenied );
    }

    //
    //Call the windows API to do the actual rename on disk.
    //
    if (!MoveFile(afpEntry->szPath, winPath))
    {
        DBGWRITE(dbg_level_info, L"[AFP]MoveFile() failed! (%d)\n", GetLastError());
        
        afpError = AFPMapWinErrorToAFP(GetLastError(), static_cast<DWORD>(afpParmErr));
    }

    AFPRevertToSelf(afpSession->hUserToken);

    if (AFP_SUCCESS(afpError))
    {
        AFPIndexUpdateEntry(
                NULL,
                afpVolumeID,
                afpEntry,
                (LPAFPINDEX)afpEntry->parEntry,
                afpPathname
                );
                
        AFPMarkVolumeDirty(afpVolumeID);
        AFPAddCharMapToIndexEntry(afpEntry, newCharMap);
    }

    return( afpError );
}


