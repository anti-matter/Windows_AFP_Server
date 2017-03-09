/*
 *	afpsrvr.cpp
 *	Created 3/5/2005
 *
 *	Copyright © 2005, Michael J. Conrad. All Rights Reserved.
 *
 *	Core FP srvr routines.
 */

#include "StdAfx.h"
#include "afpsrvr.h"

extern BOOL	gAllowGuests;
extern BOOL	gForceEncryptedLogon;


/*
 * FPGetSrvrParms()
 *
 * Description:
 *		Returns server parameters, mainly, what volumes are availalbe
 *		for this server.
 *
 * Returns: AFP error code
 */

AFPERROR FPGetSrvrParms(
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{
    UNREFERENCED_PARAMETER(afpRequest);

    int8				afpNumVolumesShared	= AFPNumVolumesShared();
    int8				afpNumVolumesActive = AFPNumVolumesActive();
    int32				afpTime				= 0;
    uint16				i					= 0;
    PAFP_VOLUME_INFO	afpVolumeInfo		= NULL;
    SYSTEMTIME			sysTime;
    FILETIME			fileTime;

    //
    //Make sure the session requesting parms is an authenticated
    //user (logged on).
    //
    if ((afpSession->dwState & sessAuthenticated) == 0)
    {
        //
        //Not authenticated, prevent access.
        //
        return( afpAccessDenied );
    }
        
    //
    //Get the Windows system time for the first part of our buffer.
    //
    GetSystemTime(&sysTime);
    SystemTimeToFileTime(&sysTime, &fileTime);
    
    //
    //Convert the Windows system time to local AFP time.
    //
    afpTime = AFPWindowsToMacTime(&fileTime);
        
    //
    //Stuff in the AFP server time value.
    //
    afpReply.AddInt32(afpTime);
    
    //
    //Stuff in the number of volumes our server has shared.
    //
    afpReply.AddInt8(afpNumVolumesActive);
    
    //
    //Stuff in all the volume names and their flags.
    //
    for(i = 1; i <= afpNumVolumesShared; i++)
    {
        if (AFPGetVolumeInfoAtIndex(i, &afpVolumeInfo) == NO_ERROR)
        {
            //
            //First, we check to make sure the volume has been fully indexed. If not
            //we cannot offer it to the clients.
            //
            if (!(afpVolumeInfo->wVolumeFlags & FLAG_VOLUME_INDEXINGCOMPLETE))
            {
                //
                //Not done indexing, can't show this one now.
                //
                continue;
            }

            DBGWRITE(DbgLevelAll, L"[AFP]Stuffing volume: %s\n", afpVolumeInfo->szShareName);
            
            afpReply.AddInt8(afpVolumeInfo->bAFPFlags);
            afpReply.AddUniStringAsPascal(afpVolumeInfo->szShareName);
        }
    }
        
    return( AFP_OK );
}


/*
 * FPGetSrvrInfo()
 *
 * Description:
 *		Returns a block of information about our server.
 *
 * Returns: AFP error code
 */

AFPERROR FPGetSrvrInfo( 
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{	
    UNREFERENCED_PARAMETER(afpSession);
    UNREFERENCED_PARAMETER(afpRequest);

    unichar	szComputerName[MAX_COMPUTERNAME_LENGTH+1];
    int16*	pAddressCountOffset	= NULL;
    uint32	dwSize				= MAX_COMPUTERNAME_LENGTH+1;
    int16	afpFlags			= 0;
    int8	afpUAMCount			= UAM_COUNT;

    GetComputerName(szComputerName, &dwSize);
        
    //
    //Set the flags which tells what our server supports.
    //
    afpFlags |= kSupportsTCPIP;
    afpFlags |= kSupportsSrvrNotification;
    afpFlags |= kSupportsCopyFile;
    afpFlags |= kSupportsSrvrMsgs;
    afpFlags |= kSupportsReconnect;

    afpReply.Advance(sizeof(int16));	//Machine type offset
    afpReply.Advance(sizeof(int16));	//Afp version count offset
    afpReply.Advance(sizeof(int16));	//UAM count offset

    afpReply.AddInt16(0);				//Volume icon and mask offset
    afpReply.AddInt16(afpFlags);		//Server flags

    //
    //Paste in the name of the computer as the server name.
    //
    afpReply.AddUniStringAsPascal(szComputerName);
    
    //
    //Add an extra padding byte if the AFP buffer is not on
    //an even boundary.
    //
    afpReply.AdvanceIfOdd();
    
    //
    //Network address count offset, save it to set later.
    //
    pAddressCountOffset = (int16*)afpReply.GetCurrentPosPtr();
    afpReply.Advance(sizeof(int16));
    
    //
    //Set the machine type string and offset
    //
    afpReply.AddParameterOffset(SRVRINFO_OFFSET_MACHTYPE, (int16)afpReply.GetDataLength());
    afpReply.AddUniStringAsPascal(AFP_MACHINE_TYPE);
        
    //AFP versions
    afpReply.AddParameterOffset(SRVRINFO_OFFSET_AFPVERSCOUNT, (int16)afpReply.GetDataLength());
    afpReply.AddInt8(AFP_VERSION_COUNT);
    afpReply.AddUniStringAsPascal(AFP_22_VERSION_STR);
#ifdef AFP_VERSION_30
    afpReply.AddUniStringAsPascal(AFP_30_VERSION_STR);
    afpReply.AddUniStringAsPascal(AFP_31_VERSION_STR);
    afpReply.AddUniStringAsPascal(AFP_32_VERSION_STR);
#endif

    //UAMs supported
    afpReply.AddParameterOffset(SRVRINFO_OFFSET_UAMCOUNT, (int16)afpReply.GetDataLength());
    
    //
    //If we only allow encrypted logons (e.g. no clear text), then we
    //need to reduce the count of uams we're sending over.
    //
    if (gForceEncryptedLogon) {
        --afpUAMCount;
    }
    
    //
    //If no guest support, reduce the count yet again as above.
    //
    if (!gAllowGuests) {
        --afpUAMCount;
    }
    
    afpReply.AddInt8(afpUAMCount);
    
    if (gAllowGuests) {
        afpReply.AddUniStringAsPascal(UAM_NONE_STR);
    }
    
    if (!gForceEncryptedLogon) {
        afpReply.AddUniStringAsPascal(UAM_CLEAR_TEXT);
    }
    
    //
    //The DHCAST128 UAM is Apple's SSL encrypted password scheme, it
    //is always passed as supported, no matter what.
    //
    afpReply.AddUniStringAsPascal(UAM_DHCAST128);
    
    //Network Address: Note, there is no reason for us to pass
    //back address since we only support connections over TCP/IP.
    //This feature is only for connection where the initial connection
    //is done over AppleTalk.

    *pAddressCountOffset = (int16)afpReply.GetDataLength();
    afpReply.AddInt8(0);
        
    return(NO_ERROR);
}


/*
 * FPZzzz()
 *
 * Description:
 *		The client is going to sleep, we need to signal this so
 *		we don't kill the client when we don't hear from it.
 *
 * Returns: AFPERROR
 */

AFPERROR FPZzzz(
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{
    UNREFERENCED_PARAMETER(afpReply);

    uint32			afpFlag		= 0;

    DBGWRITE(DbgLevelInfo, L"[AFP:FpZzzz]Enter...\n");
    
    afpRequest.Advance(sizeof(int16));

    //
    //Nab the flag parameter, this is a reserved field by
    //Apple and is ignored now.
    //
    afpFlag = afpRequest.GetInt32();

    //
    //Set the client sleeping flag so we don't kill it when we
    //don't hear from it.
    //
    afpSession->dwState |= sessClientSleeping;

    return( AFP_OK );
}



