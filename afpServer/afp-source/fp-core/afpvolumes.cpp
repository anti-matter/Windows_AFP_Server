/*
 *	afpvolumes.cpp
 *	Created 3/5/2005
 *
 *	Copyright © 2005, Michael J. Conrad. All Rights Reserved.
 *
 *	Core FP volume routines. Mostly FP wrappers to the implementations
 *	routines that live elsewhere.
 */

#include "StdAfx.h"
#include "afpvolumes.h"


/*
 * FPFlush()
 *
 * Description:
 *		Flushes the volume. For Windows, we don't do anything here since Windows
 *		doesn't have a flush concept.
 *
 * Returns: AFPERROR
 */

AFPERROR FPFlush(
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{
    UNREFERENCED_PARAMETER(afpReply);

    uint16	afpVolumeID	= 0;
    
    //
    //First WORD is afp command and padding byte.
    //
    afpRequest.Advance(sizeof(int16));
    
    //
    //Extract the volume identifier that we're working with.
    //
    afpVolumeID = afpRequest.GetInt16();

    //
    //The client must have previously opened the volume we are operating on.
    //
    if (!((LAFPSession*)afpSession->afpSessionObject)->SessionHasVolumeOpen(afpVolumeID))
    {
        //
        //The volume has not been opened, we need to return error.
        //
        return( afpParmErr );
    }

    return( AFP_OK );
}


/*
 * FPOpenVol()
 *
 * Description:
 *		Opens a volume so a workstation can access it.
 *
 * Returns: AFPERROR
 */

AFPERROR FPOpenVol(
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{
    unichar			afpVolumeName[MAX_VOLNAME_LEN+1];
    LAFPSession*	afpAFPSession	= (LAFPSession*)afpSession->afpSessionObject;
    int16			afpBitmap		= 0;
    uint16			afpVolumeID		= 0;
    AFPERROR		afpError		= AFP_OK;
    
    //
    //First WORD is afp command and padding byte.
    //
    afpRequest.Advance(sizeof(int16));
    
    //
    //Extract the bitmap that requests vol data from the buffer.
    //
    afpBitmap = afpRequest.GetInt16();
    
    //
    //Extract the volume name from the buffer.
    //
    if (!AFP_SUCCESS(afpRequest.GetString(afpVolumeName, sizeof(afpVolumeName)))) {

        return( afpParmErr );
    }

    DBGWRITE(DbgLevelAll, L"[afp:FPOpenVol]Opening volume: %s\n", afpVolumeName);
    
    //
    //A padding byte is added if the volume name doesn't end on an
    //even boudary.
    //
    //afpRequest.AdvanceIfOdd();
    
    //
    //Extract the volume password.
    //
    //afpRequest.GetRawData(pswd, sizeof(pswd));
        
    //
    //Get the volume ID for the volume to be opened.
    //
    if (!AFPMapVolumeNameToID(afpVolumeName, &afpVolumeID))
    {
        DBGWRITE(DbgLevelWarning, L"[afp:FPOpenVol]Requested volume doesn't exist! (%s)\n", afpVolumeName);
        return( afpParmErr );
    }

//Vol passwords not supported in AFP2.2+ ??
#if 0
    //
    //Check the password for access permissions.
    //
    afpError = AFPCheckVolumePassword(afpVolumeID, &pswd);

    if (!AFP_SUCCESS(afpError))
    {
        DBGWRITE(DbgLevelWarning, L"[afp:FPOpenVol]Bad volume password!!\n");
        return( afpError );
    }
#endif

    //
    //Get the volume parameters requested in the open call via the bitmap.
    //
    afpError = AFPGetVolumeParameters(afpVolumeID, afpBitmap, afpReply);
    
    if (AFP_SUCCESS(afpError))
    {
        //
        //Now call the volume routine to actually flag the vol as open.
        //Note, we don't error check here since we wouldn't get here with
        //bad data (see above).
        //
        afpError = afpAFPSession->SessionOpenVolume(afpVolumeID);
    }
    else
    {
        DBGWRITE(DbgLevelWarning, L"[afp:FPOpenVol]AFPGetVolumeParameters() failed (%d)\n", afpError);
        afpReply.Rewind();
    }
    
    return( afpError );
}


/*
 * FPCloseVol()
 *
 * Description:
 *		Closes an open afp volume.
 *
 * Returns: AFPERROR
 */

AFPERROR FPCloseVol(
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{
    UNREFERENCED_PARAMETER(afpReply);

    LAFPSession*	afpAFPSession	= (LAFPSession*)afpSession->afpSessionObject;
    uint16			afpVolumeID		= 0;
    AFPERROR		afpError		= AFP_OK;
    
    //
    //First WORD is afp command and padding byte.
    //
    afpRequest.Advance(sizeof(int16));
    
    //
    //Extract the volume identifier that we're working with.
    //
    afpVolumeID = afpRequest.GetInt16();
        
    //
    //Call on the volume routine to do the dirty work.
    //
    afpError = afpAFPSession->SessionCloseVolume(afpVolumeID);
    
    return( afpError );
}


/*
 * FPGetVolParms()
 *
 * Description:
 *		Returns the requested volume parameters for a specified volume.
 *
 * Returns: AFPERROR
 */

AFPERROR FPGetVolParms(
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{
    int16		afpBitmap	= 0;
    uint16		afpVolumeID	= 0;
    AFPERROR	afpError	= AFP_OK;
    
    DBGWRITE(DbgLevelAll, L"[afp:FPGetVolParms]FPGetVolParms...\n");
    
    //
    //First WORD is afp command and padding byte.
    //
    afpRequest.Advance(sizeof(int16));
    
    //
    //Extract the volume identifier that we're working with.
    //
    afpVolumeID = afpRequest.GetInt16();
    
    //
    //The client must have previously opened the volume we are operating on.
    //
    if (!((LAFPSession*)afpSession->afpSessionObject)->SessionHasVolumeOpen(afpVolumeID))
    {
        //
        //The volume has not been opened, we need to return error.
        //
        DBGWRITE(DbgLevelInfo, L"[afp:FPGetVolParms]Bad volume ID passed by client!\n");
        return( afpParmErr );
    }

    //
    //Extract the bitmap that requests vol data from the buffer.
    //
    afpBitmap = afpRequest.GetInt16();
    
    //
    //Get the volume parameters requested in the open call via the bitmap.
    //
    afpError = AFPGetVolumeParameters(afpVolumeID, afpBitmap, afpReply);
    
    if (!AFP_SUCCESS(afpError))
    {
        DBGWRITE(DbgLevelInfo, L"[afp:FPGetVolParms]AFPGetVolumeParameters() failed (%d)\n", afpError);
        afpReply.Rewind();
    }
    
    return( afpError );
}


