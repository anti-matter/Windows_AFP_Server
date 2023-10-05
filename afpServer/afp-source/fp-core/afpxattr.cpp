/*
 *	afpextattr.cpp
 *	Created 09/18/2005
 *
 *	Copyright © 2003-05, Michael J. Conrad. All Rights Reserved.
 */

#include "StdAfx.h"

#include "afp.h"
#include "afpdebug.h"
#include "afpxattr.h"
#include "volumes.h"
#include "fpinfo.h"
#include "AFPSession.h"

#include "AltStreams.h"

/*
 * FPGetExtAttribute()
 *
 * Description:
 *		Retieves an extended attribute from the file.
 *
 * Returns: AFPERROR
 */

AFPERROR FPGetExtAttribute(
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{
    WCHAR				afpPathname[MAX_AFP_PATH];
    WCHAR				afpAttrName[MAX_AFP_NAME];
    int16				afpVolID		= 0;
    int32				afpDirID		= 0;
    uint16				afpBitmap		= 0;
    int64				afpOffset		= 0;
    int64				afpReqCount		= 0;
    int32				afpMaxReplySize	= 0;
    int8				afpPathtype		= 0;
    uint16				afpNameLen		= 0;
    uint32				afpDataLen		= 0;
    uint32				afpActualRead	= 0;
    AFPERROR			afpError		= AFP_OK;
    LPAFPINDEX			afpEntry		= NULL;
    
    DBGWRITE(dbg_level_all, L"[FPGetExtAttribute]Enter...\n");
    
    //
    //Advance beyond command and pad byte
    //
    afpRequest.Advance(sizeof(int16));
    
    afpVolID		= afpRequest.GetInt16();
    afpDirID		= afpRequest.GetInt32();
    afpBitmap		= afpRequest.GetInt16();
    afpOffset		= afpRequest.GetInt64();
    afpReqCount		= afpRequest.GetInt64();
    afpMaxReplySize	= afpRequest.GetInt32();
    afpPathtype		= afpRequest.GetInt8();
    
    //
    //See if the user has the volume open and get a pointer to
    //the volume object.
    //	
    if (!((AFPSession*)afpSession->afpSessionObject)->SessionHasVolumeOpen(afpVolID))
    {
        DBGWRITE(dbg_level_warning, L"[FPGetExtAttribute]User does not have volume open!\n");
        return( afpParmErr );
    }
    
    //
    //Get the pathname of the object we're working on.
    //
    afpError = afpRequest.GetString(afpPathname, sizeof(afpPathname), true, afpPathtype);
    
    if (!AFP_SUCCESS(afpError))
    {
        DBGWRITE(dbg_level_warning, L"[FPGetExtAttribute]Could not retrieve pathname! (%d)\n", afpError);
        return( afpError );
    }
    
    //
    //Advance if we need to in order to land on an even boundary
    //
    afpRequest.AdvanceIfOdd();
    
    //
    //Get the size of the attribute name string.
    //
    afpNameLen = afpRequest.GetInt16();
    
    //
    //Now get the string. Note that on return afpNameLen now contains the actual
    //length of the converted string.
    //
    afpError = afpRequest.GetUniString(afpAttrName, sizeof(afpAttrName), &afpNameLen);
    
    if (!AFP_SUCCESS(afpError))
    {
        DBGWRITE(dbg_level_warning, L"[FPGetExtAttribute]Could not retrieve attribute name! (%d)\n", afpError);
        return( afpError );
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
        afpDataLen = (uint32)GetStreamLen(afpEntry->szPath, afpAttrName);
        
        if (afpDataLen != BAD_LENGTH_32)
        {
            //
            //Plum the first 2 items in the reply block. These will always be
            //returned in every case beyond here.
            //
            afpReply.AddInt16(afpBitmap);
            afpReply.AddInt32(afpDataLen);
            
            DBGWRITE(dbg_level_all, L"[FPGetExtAttribute]DataLen == %d\n", afpDataLen);
            
            if (afpMaxReplySize > 0)
            {
                if (afpReply.CheckSpaceNeeded(afpDataLen))
                {
                    HANDLE hFile = INVALID_HANDLE_VALUE;
                    
                    StringCbPrintf(afpPathname, sizeof(afpPathname), L"%s:%s", afpEntry->szPath, afpAttrName);
                    
                    if (!afp_impersonate_user(afpSession->hUserToken))
                    {
                        //
                        //Impersonation failed, don't allow any further operations.
                        //
                        return( afpAccessDenied );
                    }
                    
                    //
                    //Open a handle to the file stream object.
                    //
                    hFile = CreateFile(
                                afpPathname,
                                GENERIC_READ,
                                FILE_SHARE_READ,
                                NULL,
                                OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS,
                                NULL
                                );
                    
                    if (hFile != INVALID_HANDLE_VALUE)
                    {
                        ReadFile(
                            hFile,
                            afpReply.GetCurrentPosPtr(),
                            min((int)afpDataLen, (int)afpMaxReplySize),
                            &afpActualRead,
                            NULL
                            );
                        
                        CloseHandle(hFile);
                        
                        //
                        //We need to advance the data buffer the amount we read.
                        //
                        afpReply.Advance(afpActualRead);
                        
                        DBGWRITE(
                            dbg_level_all,
                            L"[FPGetExtAttribute]ReadFile() returned: %d, read %d bytes\n",
                            GetLastError(),
                            afpActualRead
                            );
                    }
                    
                    afp_revert_to_self(afpSession->hUserToken);
                }
                else
                {
                    //
                    //Not enough space in the reply buffer.
                    //
                    DBGWRITE(dbg_level_warning, L"[FPGetExtAttribute]Not enough space in reply buffer!\n");
                    ASSERT(0);
                    
                    return( afpMiscErr );
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
    
    return( afpError );
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
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{
    UNREFERENCED_PARAMETER(afpReply);

    WCHAR			afpPathname[MAX_AFP_PATH];
    WCHAR			afpAttrName[MAX_AFP_NAME];
    LPAFPINDEX		afpEntry		= NULL;
    int16			afpVolID		= 0;
    int32			afpDirID		= 0;
    int64			afpOffset		= 0;
    int8			afpPathtype		= 0;
    uint16			afpBitmap		= 0;
    uint16			afpNameLen		= 0;
    uint32			afpAttrDataLen	= 0;
    AFPERROR		afpError		= AFP_OK;
    DWORD			status			= AFP_OK;
    DWORD			bytesWritten	= 0;
    
    DBGWRITE(dbg_level_info, L"[afp:FPSetExtAttribute]Enter...\n");
    
    //
    //Advance beyond command and pad byte
    //
    afpRequest.Advance(sizeof(int16));
    
    afpVolID		= afpRequest.GetInt16();
    afpDirID		= afpRequest.GetInt32();
    afpBitmap		= afpRequest.GetInt16();
    afpOffset		= afpRequest.GetInt64();
    afpPathtype		= afpRequest.GetInt8();
    
    //
    //Get the pathname of the object we're working on.
    //
    afpError = afpRequest.GetString(afpPathname, sizeof(afpPathname), true, afpPathtype);
    
    if (!AFP_SUCCESS(afpError))
    {
        DBGWRITE(dbg_level_info, L"[afp:FPSetExtAttribute]Could not retrieve pathname! (%d)\n", afpError);
        return( afpError );
    }
    
    //
    //A pad byte is inserted to make the buffer end on an
    //even boundary.
    //
    afpRequest.AdvanceIfOdd();
    
    //
    //Now get the string. Note that on return afpNameLen now contains the actual
    //length of the converted string.
    //
    afpNameLen 	= afpRequest.GetInt16();
    afpError 	= afpRequest.GetUniString(afpAttrName, sizeof(afpAttrName), &afpNameLen);
    
    if (!AFP_SUCCESS(afpError))
    {
        DBGWRITE(dbg_level_info, L"[afp:FPSetExtAttribute]Could not retrieve attribute name! (%d)\n", afpError);
        return( afpError );
    }
    
    DBGWRITE(dbg_level_info, L"[afp:FPSetExtAttribute]Setting attr, file=%s, attr=%s\n", afpPathname, afpAttrName);
    
    //
    //The attribute data length we're going to write.
    //
    afpAttrDataLen = afpRequest.GetInt32();
    
    //
    //See if the user has the volume open and get a pointer to
    //the volume object.
    //
    if (!((AFPSession*)afpSession->afpSessionObject)->SessionHasVolumeOpen(afpVolID))
    {
        DBGWRITE(dbg_level_info, L"[afp:FPSetExtAttribute]User does not have volume open!\n");
        return( afpParmErr );
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
        HANDLE hFile = INVALID_HANDLE_VALUE;
        
        //
        //We have different errors to check for based on the supplied attribute
        //flags (replace and/or already exists).
        //
        status = (uint32)GetStreamLen(afpEntry->szPath, afpAttrName);
        
        if ((status == BAD_LENGTH_32) && (afpBitmap & kXAttrCreate))
        {
            return( afpObjectExists );
        }
        else if (afpBitmap & kXAttrReplace)
        {
            return( afpObjectNotFound );
        }
        
        StringCbPrintf(afpPathname, sizeof(afpPathname), L"%s:%s", afpEntry->szPath, afpAttrName);
        
        if (!afp_impersonate_user(afpSession->hUserToken))
        {
            //
            //Impersonation failed, don't allow any further operations.
            //
            return( afpAccessDenied );
        }
        
        //
        //Open a handle to the file stream object.
        //
        hFile = CreateFile(
                    afpPathname,
                    GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL,
                    CREATE_ALWAYS,
                    FILE_FLAG_BACKUP_SEMANTICS,
                    NULL
                    );
        
        if (hFile != INVALID_HANDLE_VALUE)
        {
            WriteFile(hFile, afpRequest.GetCurrentPosPtr(), afpAttrDataLen, &bytesWritten, NULL);
            CloseHandle(hFile);
            
            DBGWRITE(
                dbg_level_info,
                L"[FPGetExtAttribute]WriteFile() returned: %d\n",
                GetLastError()
                );
        }
            
        afp_revert_to_self(afpSession->hUserToken);
        
        //
        //As a last check, make sure the right amount of bytes were
        //written out to the atrribute.
        //
        if (bytesWritten != afpAttrDataLen)
        {
            DBGWRITE(dbg_level_info, L"[afp:FPSetExtAttribute]WriteFork() wrote out the wrong number of bytes!\n", afpError);
            return( afpMiscErr );
        }
    }

    DBGWRITE(dbg_level_info, L"[FPSetExtAttribute]Wrote(bytes): %d\n", bytesWritten);
    DBGWRITE(dbg_level_info, L"[FPSetExtAttribute]Returning:    %d\n", afpError);
    
    return( afpError );
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
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{
    WCHAR			afpPathname[MAX_AFP_PATH];
    LPAFPINDEX		afpEntry		= NULL;
    int16			afpVolID		= 0;
    int32			afpDirID		= 0;
    uint16			afpBitmap		= 0;
    int16			afpReqCount		= 0;
    int32			afpStartIndex	= 0;
    int32			afpMaxReplySize	= 0;
    int8			afpPathtype		= 0;
    int32			afpDataLen		= 0;
    uint32*			saveDL			= NULL;
    AFPERROR		afpError		= AFP_OK;
    
    DBGWRITE(dbg_level_info, L"[FPListExtAttribute]Enter...\n");
    
    //
    //Advance beyond command and pad byte
    //
    afpRequest.Advance(sizeof(int16));
    
    afpVolID		= afpRequest.GetInt16();
    afpDirID		= afpRequest.GetInt32();
    afpBitmap		= afpRequest.GetInt16();
    afpReqCount		= afpRequest.GetInt16();
    afpStartIndex	= afpRequest.GetInt32();
    afpMaxReplySize	= afpRequest.GetInt32();
    afpPathtype		= afpRequest.GetInt8();
    
    //
    //See if the user has the volume open and get a pointer to
    //the volume object.
    //
    if (!((AFPSession*)afpSession->afpSessionObject)->SessionHasVolumeOpen(afpVolID))
    {
        DBGWRITE(dbg_level_info, L"[FPListExtAttribute]User does not have volume open!\n");
        return( afpParmErr );
    }
    
    //
    //Get the pathname of the object we're working on.
    //
    afpError = afpRequest.GetString(afpPathname, sizeof(afpPathname), true, afpPathtype);
    
    if (!AFP_SUCCESS(afpError))
    {
        DBGWRITE(dbg_level_info, L"[FPListExtAttribute]Could not retrieve pathname! (%d)\n", afpError);
        return( afpError );
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
        std::vector<AFP_ATTR_INFO> streamArray;
        char			ansi_string[MAX_AFP_NAME];
        
        DBGWRITE(
            dbg_level_info,
            (L"[FPListExtAttribute]Getting stream list for %s\n", afpEntry->szPath)
            );
        
        if (!afp_impersonate_user(afpSession->hUserToken))
        {
            //
            //Impersonation failed, don't allow any further operations.
            //
            return( afpAccessDenied );
        }
        
        afpError = GetStreams(afpEntry->szPath, streamArray);
        
        if (AFP_SUCCESS(afpError))
        {
            //
            //The bitmap is always returned to the caller.
            //
            afpReply.AddInt16(afpBitmap);
            
            //
            //Save the position of the datalen field...
            //
            saveDL = (uint32*)afpReply.GetCurrentPosPtr();
            afpReply.Advance(sizeof(uint32));
    
            //
            //Now get the size of all the name strings and optionally plumb
            //them into the reply buffer.
            //
           
            for (auto afpAttrInfo : streamArray)
            {
                int16 cbStreamName = static_cast<int16>(lstrlen(afpAttrInfo.wszStreamName));
                
                //
                //We're only interested in streams with names (i.e not the main stream)
                //
                if (cbStreamName > 0)
                {
                    //
                    //Check to make sure the stream isn't one of our special streams. We
                    //don't want to send streams the server owns back to the client (e.g.
                    //the resource or info streams).
                    //
                    if ((!lstrcmp(afpAttrInfo.wszStreamName, AFP_INFO_STREAM_NAME))			||
                        (!lstrcmp(afpAttrInfo.wszStreamName, AFP_RESOURCE_FORK_NAME_PLAIN))	)
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
                                (L"[FPListExtAttribute]New attr name added: %s\n",
                                afpAttrInfo.wszStreamName)
                                );
                            
                            if (afp_windows_to_mac_path(afpAttrInfo.wszStreamName, sizeof(ansi_string), NULL, ansi_string) != NULL)
                            {
                                afpReply.AddRawData(ansi_string, strlen(ansi_string)+sizeof(char));
                            }
                        }
                        else
                        {
                            //
                            //Our buffer will grow larger than the client can handle. Stop
                            //including new names, adjust the reported data size and return.
                            //
                            DBGWRITE(dbg_level_info, L"[FPListExtAttribute]Data len exceeds client buffer size! (%d)\n", afpMaxReplySize);
                            
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
            if (afpMaxReplySize == 0) {
                
                afpDataLen += (sizeof(int16) + sizeof(int32));
            }
            
            //
            //Set the data length field now that we've calculated it completely.
            //
            *saveDL = htonl(afpDataLen);
        }
        
        afp_revert_to_self(afpSession->hUserToken);
    }
    
    DBGWRITE(dbg_level_info, L"[FPListExtAttribute]Name list datalen = %d\n", afpDataLen);
    DBGWRITE(dbg_level_info, L"[FPListExtAttribute]Returning %d\n", afpError);
    
    return( afpError );
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
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{
    UNREFERENCED_PARAMETER(afpReply);

    WCHAR			afpPathname[MAX_AFP_PATH];
    WCHAR			afpAttrName[MAX_PATH];
    LPAFPINDEX		afpEntry		= NULL;
    int16			afpVolID		= 0;
    int32			afpDirID		= 0;
    uint16			afpBitmap		= 0;
    int8			afpPathtype		= 0;
    uint16			afpNameLen		= 0;
    AFPERROR		afpError		= AFP_OK;
    
    DBGWRITE(dbg_level_info, L"[FPRemoveExtAttribute]Enter...\n");
    
    //
    //Advance beyond command and pad byte
    //
    afpRequest.Advance(sizeof(int16));
    
    afpVolID		= afpRequest.GetInt16();
    afpDirID		= afpRequest.GetInt32();
    afpBitmap		= afpRequest.GetInt16();
    afpPathtype		= afpRequest.GetInt8();
    
    //
    //See if the user has the volume open and get a pointer to
    //the volume object.
    //
    if (!((AFPSession*)afpSession->afpSessionObject)->SessionHasVolumeOpen(afpVolID))
    {
        DBGWRITE(dbg_level_info, L"[FPRemoveExtAttribute]User does not have volume open!\n");
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
    afpError = afpRequest.GetString(afpPathname, sizeof(afpPathname), true, afpPathtype);
    
    if (!AFP_SUCCESS(afpError))
    {
        DBGWRITE(dbg_level_info, L"[FPRemoveExtAttribute]Could not retrieve pathname! (%d)\n", afpError);
        return( afpError );
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
        
    if (!AFP_SUCCESS(afpError))
    {
        DBGWRITE(dbg_level_info, L"[FPRemoveExtAttribute]Object not found! (name = %s, dirID = %d)\n", afpPathname, afpDirID);
        return( afpError );
    }
    
    //
    //A pad byte is inserted to make the buffer end on an
    //even boundary.
    //
    afpRequest.AdvanceIfOdd();
    
    //
    //Now get the string. Note that on return afpNameLen now contains the actual
    //length of the converted string.
    //
    afpNameLen 	= afpRequest.GetInt16();
    afpError 	= afpRequest.GetUniString(afpAttrName, sizeof(afpAttrName), &afpNameLen);
    
    if (!AFP_SUCCESS(afpError))
    {
        DBGWRITE(dbg_level_info, L"[FPRemoveExtAttribute]Could not retrieve attribute name! (%d)\n", afpError);
        return( afpError );
    }
    
    DBGWRITE(dbg_level_info, L"[FPRemoveExtAttribute]Removing attr, file=%s, attr=%s\n", afpPathname, afpAttrName);

    StringCbPrintf(afpPathname, sizeof(afpPathname), L"%s:%s", afpEntry->szPath, afpAttrName);
    
    if (!afp_impersonate_user(afpSession->hUserToken))
    {
        //
        //Impersonation failed, don't allow any further operations.
        //
        return( afpAccessDenied );
    }
    
    if (!DeleteFile(afpPathname))
    {
        DBGWRITE(dbg_level_info,
                L"[FPRemoveExtAttribute]DeleteFile failed on %s! (%d)\n",
                afpEntry->szPath,
                GetLastError()
                );
        
        afpError = afp_map_win_error_to_afp(GetLastError(), static_cast<DWORD>(afpParmErr));
    }
        
    afp_revert_to_self(afpSession->hUserToken);
    
    DBGWRITE(dbg_level_info, L"[FPRemoveExtAttribute]Returning %d\n", afpError);
    
    return( afpError );
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

uint64 GetStreamLen(
    IN PWCHAR pathname,
    IN PWCHAR streamName
    )
{
    std::vector<AFP_ATTR_INFO> streams;
        
    if (AFP_SUCCESS(GetStreams(pathname, streams)))
    {
        for (auto it = streams.begin(); it < streams.end(); ++it)
        {
            if (!lstrcmp(it->wszStreamName, streamName))
            {
                return it->streamSize.QuadPart;
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
    IN std::vector<AFP_ATTR_INFO>& streams
)
{
    AFP_ATTR_INFO				afpAttrInfo;
    WCHAR						path[MAX_PATH];
    HANDLE						hFile					= INVALID_HANDLE_VALUE;	
    PFILE_STREAM_INFORMATION 	pStreamInfo 			= NULL;
    PFILE_STREAM_INFORMATION	pSavePtr				= NULL;
    NTQUERYINFORMATIONFILE 		NtQueryInformationFile	= NULL;
    NTSTATUS					status					= 0;
    DWORD						dwInfoBlockSize			= 0;
    
    //
    //First load the proc for our undocumented API.
    //
    reinterpret_cast<FARPROC&>(NtQueryInformationFile) = ::GetProcAddress(
                                            ::GetModuleHandle(L"ntdll.dll"),
                                            "NtQueryInformationFile"
                                            );
    if (NtQueryInformationFile == nullptr)
    {
        DBGWRITE(dbg_level_info, L"[afpxattr:GetStreams]GetProcAddress() failed! (%d)\n", GetLastError());
        return( afpParmErr );
    }
    
    //
    //Open a generic handle to the file object by opening it data fork.
    //
    hFile = CreateFile(
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
        
        dwInfoBlockSize	+= 4 * 1024;
        
        if (pStreamInfo) {
            delete [] pStreamInfo;
        }
        
        pStreamInfo = reinterpret_cast<PFILE_STREAM_INFORMATION>(new BYTE[dwInfoBlockSize]);
        
        pSavePtr						= pStreamInfo;
        pStreamInfo->StreamNameLength 	= 0;
        status 							= NtQueryInformationFile(
                                                hFile,
                                                &ioStatus,
                                                static_cast<LPVOID>(pStreamInfo),
                                                dwInfoBlockSize,
                                                FileStreamInformation
                                                );

    }while(status == STATUS_BUFFER_OVERFLOW);
    
    //
    //We don't need the hFile anymore
    //
    CloseHandle(hFile);
    
    for(;;)
    {
        LARGE_INTEGER	streamSize;
        DWORD			i = 0;
        
        memset(&afpAttrInfo, 0, sizeof(AFP_ATTR_INFO));
        
        //
        //If this is a directory, it may have no streams, so we just exit.
        //
        if (pStreamInfo->StreamNameLength == 0) {
            break;
        }
        
        //
        //For non-named streams, we do anything.
        //
        if (pStreamInfo->StreamNameLength > 1)
        {		
            //
            //Copy the stream name, may not be null terminated!
            //
            while(pStreamInfo->StreamName[i+1] != ':')
            {
                afpAttrInfo.wszStreamName[i] = pStreamInfo->StreamName[i+1];
                i++;
            }
                                
            //
            //Create the full path including the stream name
            //
            StringCbPrintf(path, sizeof(path), L"%s:%s", pathname, afpAttrInfo.wszStreamName);
            
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
                
                afpAttrInfo.streamSize = streamSize;
            }
            
            //
            //Finally, add the new attr info block to the array.
            //
            streams.push_back(afpAttrInfo);
        }
        
        //
        //If there's no more streams, then break out of the loop.
        //
        if (pStreamInfo->NextEntryOffset == 0) {
            break;
        }
        
        pStreamInfo = reinterpret_cast<PFILE_STREAM_INFORMATION>(reinterpret_cast<LPBYTE>(pStreamInfo) + pStreamInfo->NextEntryOffset);
    }
    
    delete [] pSavePtr;
            
    return AFP_OK;
}

