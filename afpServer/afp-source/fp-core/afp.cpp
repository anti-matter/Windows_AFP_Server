/*
 *	afp.cpp
 *	Created 3/5/2005
 *
 *	Copyright © 2005, Michael J. Conrad. All Rights Reserved.
 *
 *	Dispatch routines to core FP routines.
 */

#include "StdAfx.h"

#include "afpdebug.h"
#include "afphelp.h"
#include "afp.h"
#include "afpfileio.h"
#include "afpdirectory.h"
#include "afpdesktop.h"
#include "afpvolumes.h"
#include "afpuser.h"
#include "afpsrvr.h"
#include "afpxattr.h"

#include "AfpBuffer.h"


typedef AFPERROR (*afp_func)(AfpBuffer& afpRequest, AfpBuffer& afpReply, LPSESSION afpSession);

typedef struct
{
    afp_func	func;
}AFP_TABLE;

AFP_TABLE afpTable[] = {
    {FPUnimplemented},	//0
    {FPByteRangeLock},
    {FPCloseVol},
    {FPUnimplemented},	//afpDirClose
    {FPCloseFork},
    {FPCopyFile},
    {FPCreateDir},
    {FPCreateFile},
    {FPDelete},
    {FPEnumerate},
    {FPFlush},
    {FPFlushFork},
    {FPUnimplemented},	//afpGetDirParms
    {FPUnimplemented},	//afpGetFileParms
    {FPGetForkParms},
    {FPGetSrvrInfo},
    {FPGetSrvrParms},
    {FPGetVolParms},
    {FPLogin},
    {FPContLogin},
    {FPLogout},
    {FPMapID},
    {FPUnimplemented},	//afpMapName
    {FPMoveAndRename},
    {FPOpenVol},
    {FPUnimplemented},	//afpOpenDir
    {FPOpenFork},
    {FPRead},
    {FPRename},
    {FPSetFileDirParms},
    {FPSetFileDirParms},
    {FPSetForkParms},
    {FPUnimplemented},	//afpSetVolParms
    {FPWrite},
    {FPGetFileDirParms},
    {FPSetFileDirParms},
    {FPUnimplemented},	//afpChangePwd
    {FPUnimplemented},	//afpGetUserInfo
    {FPGetSrvrMsg},
    {FPUnimplemented},	//afpCreateID
    {FPUnimplemented},	//afpDeleteID
    {FPResolveID},
    {FPUnimplemented},	//afpExchangeFiles
    {FPUnimplemented},	//43
    {FPUnimplemented},	//44
    {FPUnimplemented},	//45
    {FPUnimplemented},	//46
    {FPUnimplemented},	//47
    {FPOpenDT},
    {FPCloseDT},
    {FPUnimplemented},	//50
    {FPGetIcon},
    {FPGetIconInfo},
    {FPAddAPPL},
    {FPRemoveAPPL},
    {FPGetAPPL},
    {FPAddComment},
    {FPRemoveComment},
    {FPGetComment},
#ifdef AFP_VERSION_30
    {FPByteRangeLock},	//afpByteRangeLockExt
    {FPRead},
    {FPWrite},
    {FPUnimplemented},	//afpGetAuthMethods
    {FPLogin},
    {FPGetSessionToken},
    {FPDisconnectOldSession},
    {FPEnumerate},
    {FPUnimplemented},	//afpCatSearchExt
    {FPEnumerate},
    {FPGetExtAttribute},
    {FPSetExtAttribute},
    {FPRemoveExtAttribute},
    {FPListExtAttribute}
#endif
};


/*
 * FPUnimplemented()
 *
 * Description:
 *		Function used to catch unimplemented afp functions.
 *
 * Returns: afpCallNotSupported
 */

AFPERROR FPUnimplemented(
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{
    UNREFERENCED_PARAMETER(afpRequest);
    UNREFERENCED_PARAMETER(afpReply);
    UNREFERENCED_PARAMETER(afpSession);

    DBGWRITE(DbgLevelInfo, L"[afp:FPUnimplemented]Call not implemented (%d)\n", afpRequest.GetInt8());
    return( afpCallNotSupported );
}


/*
 * FPDispatchCommand()
 *
 * Description:
 *		Dispatch the incoming afp command to the correct FPXXXXX API
 *		implementation function.
 *
 * Returns: AFP error code
 */

AFPERROR FPDispatchCommand(
    IN  LPPER_IO_DATA 	pRecvIOData,
    IN	LPSEND_QUEUE	pReplyQueue	
    )
{
    AfpBuffer	afpRequest((uint8*)&pRecvIOData->buffer[DSI_OFFSET_DATASTART], pRecvIOData->ioSize);
    AfpBuffer	afpReply((uint8*)&pReplyQueue->sendIOData.buffer[DSI_OFFSET_DATASTART], pReplyQueue->sendIOData.bufferSize);
    BYTE		afpCommand	= (BYTE)pRecvIOData->buffer[DSI_OFFSET_DATASTART];
    AFPERROR	afpError	= afpCallNotSupported;
       
    //
    //Save this value as some AFP functions need it (such as FPWrite).
    //
    afpRequest.SetDSIDataOffset(ntohl(*(DWORD*)&pRecvIOData->buffer[DSI_OFFSET_DATAOFFSET]));

    //
    //Whether we're authenticated or not is how we determine
    //what afp commands we allow the client to execute.
    //
    if (	(pReplyQueue->pSession->hUserToken != NULL)				&&
            (pReplyQueue->pSession->dwState & sessAuthenticated)	)
    {
        if (afpCommand <= afpListExtAttr)
        {
            afpError = afpTable[afpCommand].func(afpRequest, afpReply, pReplyQueue->pSession);
        }
        else
        {
            switch(afpCommand)
            {
                case afpAddIcon:
                    afpError = FPAddIcon(afpRequest, afpReply, pReplyQueue->pSession);
                    break;

                case afpZzzz:
                    afpError = FPZzzz(afpRequest, afpReply, pReplyQueue->pSession);
                    break;
                
                default:
                    break;
            }
        }
    }
    else
    {
        switch(afpCommand)
        {
            case afpGetSInfo:
                afpError = FPGetSrvrInfo(afpRequest, afpReply, pReplyQueue->pSession);
                break;
            
            case afpLoginExt:
            case afpLogin:
                afpError = FPLogin(afpRequest, afpReply, pReplyQueue->pSession);
                break;
            
            case afpContLogin:
                afpError = FPContLogin(afpRequest, afpReply, pReplyQueue->pSession);
                break;
            
            default:
                DBGWRITE(DbgLevelInfo, L"[afp:FPDispatchCommand]Call not implemented (%d)\n", afpCommand);
                afpError = afpAccessDenied;
                break;
        }
    }

    if (AFP_SUCCESS(afpError) || (afpError == afpAuthContinue))
    {
        switch(afpCommand)
        {
            //
            //Some AFP calls return their results in the DSI Header's size field.
            //So, we have to make special checks here.
            //
            case afpRead:
            case afpReadExt:
                pReplyQueue->sendIOData.ioSize = afpReply.GetDSIReturnValue();
                break;

            default:
                pReplyQueue->sendIOData.ioSize = afpReply.GetDataLength();
                break;
        }
    }
    
    return( afpError );
}










