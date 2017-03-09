/*
 *	afpuser.cpp
 *	Created 3/5/2005
 *
 *	Copyright © 2005, Michael J. Conrad. All Rights Reserved.
 *
 *	Core FP user routines (login, session, messages).
 */

#include "StdAfx.h"
#include "afpuser.h"
#include "afpEvents.h"
#include "afpdhxlogin.h"


WCHAR	gServerMessage[AFP_MSG_MAXLEN+1];
BOOL	gAllowGuests			= TRUE;
DWORD	gMaxSessions			= 0;
BOOL	gForceEncryptedLogon	= FALSE;


/*
 * afpGetIntAFPVersion()
 *
 * Description:
 *		Returns the integer-based AFP version.
 *
 * Returns: AFPERROR
 */

int8 afpGetIntAFPVersion(WCHAR* afpVersion)
{
    int8	result = -1;

    if (!lstrcmp(afpVersion, AFP_22_VERSION_STR))
        result = afpVersion22;
    else if (!lstrcmp(afpVersion, AFP_30_VERSION_STR))
        result = afpVersion30;
    else if (!lstrcmp(afpVersion, AFP_31_VERSION_STR))
        result = afpVersion31;
    else if (!lstrcmp(afpVersion, AFP_32_VERSION_STR))
        result = afpVersion32;
    else {
        ASSERT(0);
    }

    return( result );
}


/*
 * afpGetIntUAMType()
 *
 * Description:
 *		Returns the integer-based UAM type.
 *
 * Returns: AFPERROR
 */

int8 afpGetIntUAMType(WCHAR* uamString)
{
    int8	result = -1;

    if (!lstrcmp(uamString, UAM_NONE_STR))
        result = afpUAMNone;
    else if (!lstrcmp(uamString, UAM_CLEAR_TEXT))
        result = afpUAMCleartext;
    else if (!lstrcmp(uamString, UAM_DHCAST128))
        result = afpUAMDHCAST128;
    else {
        ASSERT(0);
    }

    return( result );
}


/*
 * FPGetSessionToken()
 *
 * Description:
 *		Returns a session token to the client for re-connection at
 *		a later time if the client crashes or connection is lost.
 *
 * Returns: AFPERROR
 */

AFPERROR FPGetSessionToken(
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{
    LAFPSession*	afpSessObject	= (LAFPSession*)afpSession->afpSessionObject;
    LAFPSession*	afpOldSession	= NULL;
    int16			afpType			= 0;
    int32			afpIDSize		= 0;
    int8*			afpID			= NULL;
    uint32			afpTimeStamp	= 0;
    int32			afpToken		= afpSessObject->GetToken();
    bool			fSavedID		= false;
    
    DBGWRITE(DbgLevelInfo, L"[afp:FPGetSessionToken]Enter...\n");
    
    //
    //First byte is afp command and padding.
    //
    afpRequest.Advance(sizeof(int16));
    afpType	= afpRequest.GetInt16();

    if (afpType != kLoginWithoutID)
    {
        afpIDSize = afpRequest.GetInt32();

        DBGWRITE(DbgLevelInfo, L"[afp:FPGetSessionToken]ID size from client = %d\n", afpIDSize);

        //
        //For types 3 & 4, a time stamp is included for additional comparison.
        //
        if ((afpType == kLoginWithTimeAndID) || (afpType == kReconnWithTimeAndID)) {

            afpTimeStamp = afpRequest.GetInt32();
        }

        //
        //Save the AFP id in the AFPSession object for later comparison.
        //
        afpID = new int8[afpIDSize];

        if (afpID != NULL) {

            afpRequest.GetRawData(afpID, (int16)afpIDSize);
        }
    }

    switch(afpType)
    {
        case kLoginWithoutID:
            //
            //Now we find the session corresponding to the token and kill it.
            //
            break;

        //
        //The AFP client can set the Type parameter to kLoginWithID, set the ID 
        //parameter to a unique client-defined value, and send this command. The server 
        //will associate the value of ID with the session. Later, if the local system crashes 
        //and the AFP client logs in again, the AFP client can set the Type parameter to 
        //kLoginWithID, set the ID parameter to the ID that it previously sent to the remote 
        //server, and send this command again. The remote server will look for a session that 
        //matches the value of ID. If a match is found, it will close any files associated with 
        //the session that are open, free any locked resources, and disconnect the matching session
        //
        case kLoginWithID:
            DBGWRITE(DbgLevelInfo, L"[afp:FPGetSessionToken]kLoginWithID...\n");

            afpOldSession = LAFPSession::FindSessionWithID(afpID, afpIDSize);

            if (afpOldSession != NULL)
            {
                DBGWRITE(DbgLevelInfo, L"[afp:FPGetSessionToken]Found old session!!!\n");
                DSICleanupSession(afpOldSession->GetDSISession());
                break;
            }

            afpSessObject->SetAFPID(afpID, afpIDSize);
            fSavedID = true;
            break;

        case kReconnWithID:
            
            break;

        //
        //The client wants his old session to be discarded. The client is sending
        //an IDLength, an ID, and a Timestamp. We only discard the old session if
        //the timestamps DO NOT match.
        //
        case kLoginWithTimeAndID:
            DBGWRITE(DbgLevelInfo, L"[afp:FPGetSessionToken]kLoginWithTimeAndID...\n");

            afpOldSession = LAFPSession::FindSessionWithID(afpID, afpIDSize);

            if (afpOldSession != NULL)
            {
                DBGWRITE(DbgLevelInfo, L"[afp:FPGetSessionToken]Found old session!!!\n");

                if (afpOldSession->GetAFPTimeStamp() != afpTimeStamp)
                {
                    //
                    //Time stamps do not match, discard the old session.
                    //
                    DSICleanupSession(afpOldSession->GetDSISession());
                    break;
                }
            }

            afpSessObject->SetAFPTimeStamp(afpTimeStamp);
            afpSessObject->SetAFPID(afpID, afpIDSize);

            fSavedID = true;
            break;

        //
        //The client has just finished a successful reconnect, is sending an IDLength,
        //an ID, and a Timestamp parameter, and wants the session to be updated
        //with the ID parameter.
        //
        case kReconnWithTimeAndID:
            DBGWRITE(DbgLevelInfo, L"[afp:FPGetSessionToken]kReconnWithTimeAndID...\n");

            afpSessObject->SetAFPTimeStamp(afpTimeStamp);
            afpSessObject->SetAFPID(afpID, afpIDSize);

            fSavedID = true;
            break;

        //
        //All these id's are unsupported by this AFP Server.
        //
        case kRecon1Login:
        case kRecon1ReconnectLogin:
        case kRecon1Refresh:
        case kGetKerberosSessionKey:
            break;
        
        default:
            DBGWRITE(DbgLevelInfo, L"[afp:FPGetSessionToken]Bad type! (%d)\n", afpType);
            return( afpParmErr );
    }

    afpReply.AddInt32(AFP_SESSION_TOKEN_SIZE);
    afpReply.AddInt32(afpToken);
    
    DBGWRITE(DbgLevelInfo, L"[afp:FPGetSessionToken]Token = %x\n", afpToken);

    if (!fSavedID) {

        //
        //Since we didn't save off the afpID passed, we need to dispose of it.
        //
        delete [] afpID;
    }
        
    return( AFP_OK );
}


/*
 * FPDisconnectOldSession()
 *
 * Description:
 *		AFP clients call this when they are inadvertantly cutoff from
 *		the file server. The client calls this to transfer all resources
 *		from the old session to this new one.
 *
 * Returns: AFPERROR
 */

AFPERROR FPDisconnectOldSession(
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{
    UNREFERENCED_PARAMETER(afpReply);
    UNREFERENCED_PARAMETER(afpSession);

    LAFPSession*	afpLostSession	= NULL;
    int8			afpType			= 0;
    int32			afpTokenLength	= 0;
    int32			afpToken		= 0;

    DBGWRITE(DbgLevelInfo, L"[afp:FPDisconnectOldSession]Enter...\n");
    
    afpRequest.Advance(sizeof(int16));
    
    afpType			= afpRequest.GetInt8();
    afpTokenLength	= afpRequest.GetInt32();
    
    if (afpTokenLength != AFP_SESSION_TOKEN_SIZE)
    {
        DBGWRITE(DbgLevelInfo, L"[afp:FPDisconnectOldSession]Bad token length\n");
        return( afpMiscErr );
    }
    
    afpToken 		= afpRequest.GetInt32();
    afpLostSession 	= LAFPSession::FindSessionByToken(afpToken);
    
    if (afpLostSession != NULL)
    {
        DBGWRITE(DbgLevelInfo, L"[afp:FPDisconnectOldSession]Found diconnected session!\n");
        DSICleanupSession(afpLostSession->GetDSISession());
    }
    
    return( AFP_OK );
}


/*
 * FPLogin()
 *
 * Description:
 *		The client is attempting to authenticate to this afp server.
 *
 * Returns: AFPERROR
 */

AFPERROR FPLogin(
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{
    unichar				afpVersion[MAX_AFP_VERSION_LEN+1];
    unichar				afpUAM[MAX_UAM_STRING_LEN+1];
    char				afpPassword[UAM_CLRTXTPWDLEN+1];
    unichar				afpPasswordW[UAM_CLRTXTPWDLEN+1];
    unichar				szUserName[UAM_USERNAMELEN+1];
    unichar				szDomainName[MAX_DOMAIN_NAME_LEN+1];
    unichar				szPathname[MAX_AFP_PATH];
    int8				afpPathType		= 0;
    int16				afpFlags		= 0;
    int8				afpCommand		= 0;
    AFPERROR			afpError		= afpUserNotAuth;
        
    //
    //Check for maximum logon sessions limit being reached. If it is, we
    //just return afpAccessDenied.
    //
    if (gMaxSessions != 0)
    {
        //
        //Note that we only count authenticated sessions on the server. There could be
        //some "utility" sessions still looming around that we don't want to count.
        //
        if (DSICountSessions(TRUE) >= gMaxSessions)
        {
            //
            //Too many sessions open on the server. The spec doesn't say what error
            //code to return in this case.
            //
            return( afpAccessDenied );
        }
    }

    //
    //The first byte in the buffer is the afp command byte.
    //
    afpCommand = afpRequest.GetInt8();
    
    if (afpCommand == afpLoginExt)
    {
        afpRequest.Advance(sizeof(int8));
        afpFlags = afpRequest.GetInt16();
    }

    if (!AFP_SUCCESS(afpRequest.GetString(afpVersion, sizeof(afpVersion)))) return( afpParmErr );
    if (!AFP_SUCCESS(afpRequest.GetString(afpUAM, sizeof(afpUAM))))			return( afpParmErr );

    DBGWRITE(DbgLevelInfo, L"[afp:FPLogin]AFP Version = %s\n", afpVersion);
    DBGWRITE(DbgLevelInfo, L"[afp:FPLogin]UAM type    = %s\n", afpUAM);

    //
    //Get the AFP version passed by the client into a more usable type
    //
    afpSession->bAFPVersion = afpGetIntAFPVersion(afpVersion);

    if (afpSession->bAFPVersion == -1)
    {
        DBGWRITE(DbgLevelWarning, L"[afp:FPLogin]Bad AFP version, terminating login\n");
        return( afpBadUAM );
    }

    //
    //Get the UAM version passed by the client into a more usable type
    //
    afpSession->uamType = afpGetIntUAMType(afpUAM);

    if (afpSession->uamType == -1)
    {
        DBGWRITE(DbgLevelWarning, L"[afp:FPLogin]Bad UAM type, terminating login\n");
        return( afpBadUAM );
    }
        
    RtlZeroMemory(afpPassword, sizeof(afpPassword));
    
    //
    //Every UAM other then guest requires the user name, so retrieve it now.
    //
    if (afpSession->uamType != afpUAMNone)
    {
        if (afpCommand == afpLoginExt)
        {
            afpPathType	= afpRequest.GetInt8();			
            if (!AFP_SUCCESS(afpRequest.GetString(szUserName, sizeof(szUserName), false, afpPathType)))
            {
                DBGWRITE(DbgLevelWarning, L"[afp:FPLogin]Failed to retrieve user name, terminating login\n");
                return( afpParmErr );
            }
            
            afpPathType = afpRequest.GetInt8();
            afpRequest.GetString(szPathname, sizeof(szPathname), false, afpPathType);
        }
        else
        {
            afpRequest.GetString(szUserName, sizeof(szUserName));
        }
        
        //
        //Extract the domain name (if there is one) from the username buffer.
        //
        AFPExtractDomainAndUserName(
            szUserName, 
            sizeof(szUserName), 
            szDomainName, 
            sizeof(szDomainName)
            );

        DBGWRITE(DbgLevelInfo, L"[afp:FPLogin]Username    = %s\n", szUserName);
        DBGWRITE(DbgLevelInfo, L"[afp:FPLogin]Domain name = %s\n", szDomainName);

        //
        //We need the name and domain for later to get the user SID.
        //
        lstrcpyn(afpSession->userName, szUserName, lstrlen(szUserName)+1);
        lstrcpyn(afpSession->domainName, szDomainName, lstrlen(szDomainName)+1);

        afpRequest.AdvanceIfOdd();	//Padding byte
    }

    //
    //Now, let's check to see which UAM the client is using to log
    //into our server.
    //

    switch(afpSession->uamType)
    {
        case afpUAMNone:
        {
            //
            //Check to see if guests are allowed. If not, we break and exit.
            //
            if ((!gAllowGuests) || (gForceEncryptedLogon))
            {
                return( afpBadUAM );
            }

            //
            //BUGBUG: This will break on international systems! Fix it!
            //
            RtlZeroMemory(szUserName, sizeof(szUserName));
            StringCbCopy(szUserName, sizeof(szUserName), TEXT("Guest"));
            break;
        }

        case afpUAMCleartext:
        {
            //
            //This is a cleartext logon. Make sure we're not supposed to force
            //unencrypted logons. This may be done programatically to work around
            //the mac client!
            //
            if (gForceEncryptedLogon) 
            {
                DBGWRITE(DbgLevelWarning, L"[afp:FPLogin]Attempted cleartext login when encryption is required\n");
                return( afpBadUAM );
            }

            //
            //The password is sent to us as an 8 byte array. This array is not length
            //byte preceeded or null terminated.
            //
            afpRequest.GetRawData(afpPassword, UAM_CLRTXTPWDLEN);
            AFPMacToWindowsString(afpPassword, sizeof(afpPasswordW), afpPasswordW);

            DBGWRITE(DbgLevelInfo, L"[afp:FPLogin]Cleartext password = %s\n", afpPasswordW);
            break;
        }//afpUAMCleartext

        case afpUAMDHCAST128:
        {
            //
            //This login UAM uses SSL to encrypt the password buffer using
            //public and private keys.
            //
            
            DHXInfo*	dhxInfo = NULL;
            
            //
            //This is where we store all the information we'll need for the login continue.
            //
            dhxInfo = (DHXInfo*)LocalAlloc(LMEM_ZEROINIT, sizeof(DHXInfo));
            
            if (dhxInfo == NULL)
            {
                //
                //We failed to allocated the necessary block of data to store information
                //between login api calls. No choice but to bail out.
                //
                
                DBGWRITE(DbgLevelWarning, L"[afp:FPLogin]Failed to allocate DHXInfo structure, exiting.\n");
                return( afpMiscErr );
            }
                                    
            afpError = DHXLogin(
                            afpRequest,
                            afpReply,
                            afpSession,
                            dhxInfo
                            );

            if (afpError != afpAuthContinue)
            {
                //
                //Free the memory we allocated, otherwise we'll leak it.
                //
                LocalFree(dhxInfo);
            }
            else
            {
                //
                //There should never be two logins for the same session happening
                //simultaneously. If we find we already have an info buffer,
                //it's bad news.
                //

                if (afpSession->extLoginInfo != NULL)
                {
                    DBGWRITE(DbgLevelWarning, L"[afp:FPLogin]WARNING: Alread have extended login info!\n");
                    ASSERT(0);

                    LocalFree(afpSession->extLoginInfo);
                }

                afpSession->extLoginInfo = dhxInfo;
            }

            return( afpError );
        }

        default:
            return( afpBadUAM );

    }//switch

    //
    //Now do the actual logon, the implementation routine "does the right thing".
    //
    return( afpImpUserLogon(afpSession, afpReply, afpPasswordW, sizeof(afpPasswordW)) );
}


/*
 * FPContLogin()
 *
 * Description:
 *		Continue the login sequence from the encrypted UAM.
 *
 * Returns: AFPERROR
 */

AFPERROR FPContLogin(
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{	
    AFPERROR	afpError = afpUserNotAuth;
    
    afpRequest.Advance(sizeof(int16)); //command and padding
    
    DBGWRITE(DbgLevelInfo, L"[afp:FPContLogin]Enter\n");
    DBGWRITE(DbgLevelInfo, L"[afp:FPContLogin]LogonUAM = %d\n", afpSession->uamType);

    switch(afpSession->uamType)
    {
        case afpUAMDHCAST128:
        {
            char 		szPassword[DHX_PASSWDLEN+1];
            WCHAR		szPasswordW[DHX_PASSWDLEN+1];
            DHXInfo*	dhxInfo = (DHXInfo*)afpSession->extLoginInfo;
            
            //
            //The DHXInfo structure should have been allocated and filled in already
            //from the FPLogin() sequence.
            //
            if (dhxInfo == NULL)
            {
                DBGWRITE(DbgLevelInfo, L"[afp:FPContLogin]DHXInfo is invalid\n");
                return( afpMiscErr );
            }
            
            memset(szPassword, 0, sizeof(szPassword));
            
            //
            //The heavy lifting of all the encryption/decryption goop is done by the
            //routines in afpdhxlogin.cpp.
            //
            afpError = DHXLoginContinue(
                            afpRequest,
                            afpReply,
                            afpSession,
                            szPassword,
                            sizeof(szPassword),
                            dhxInfo
                            );
            
            if (AFP_SUCCESS(afpError))
            {
                //
                //We've had a successful key exchange and decryption of passwords. Now
                //convert the password to a unicode string and authenticate this user
                //using the Windows Logon API.
                //

                if (!AFPMacToWindowsString(szPassword, sizeof(szPasswordW), szPasswordW))
                {
                    DBGWRITE(DbgLevelWarning, L"[afp:FPContLogin]Error converting password to unicode\n");
                    return( afpMiscErr );
                }

                afpError = afpImpUserLogon(afpSession, afpReply, szPasswordW, sizeof(szPasswordW));
            }
            
            SecureZeroMemory(szPassword, sizeof(szPassword));
            SecureZeroMemory(szPasswordW, sizeof(szPasswordW));

            //
            //We don't require the extended login info anymore. Free it and
            //be sure the clear the pointer for the next go-around.
            //
            LocalFree(afpSession->extLoginInfo);
            afpSession->extLoginInfo = NULL;

            break;
        }
        
        default:
            return( afpParmErr );
    }
    
    DBGWRITE(DbgLevelInfo, L"[afp:FPContLogin]Returning: %d\n", afpError);
    
    return( afpError );
}


/*
 * FPChangePassword()
 *
 * Description:
 *		Change the password for the user using the supplied UAM.
 *
 * Returns: AFPERROR
 */

AFPERROR FPChangePassword(
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	dsiSession
)
{
    WCHAR			szUAMVersion[MAX_UAM_STRING_LEN+1];
    WCHAR			szUserName[UAM_USERNAMELEN+1];
    int8			uamType		= 0;
    AFPERROR		afpError 	= afpParmErr;
    
    DBGWRITE(DbgLevelInfo, L"[afpuser:FPChangePswd]Enter...\n");
    
    afpRequest.Advance(sizeof(int16));
    
    afpRequest.GetString(szUAMVersion, sizeof(szUAMVersion));
    uamType = afpGetIntUAMType(szUAMVersion);
    
    afpRequest.AdvanceIfOdd();
        
    //
    //Starting with AFP3.0, the username is just 2 bytes of zeros.
    //
    if (dsiSession->bAFPVersion > afpVersion22)
    {
        afpRequest.Advance(sizeof(int16));
        StringCbCopy(szUserName, sizeof(szUserName), dsiSession->userName);
    }
    else
    {		
        afpRequest.GetString(szUserName, sizeof(szUserName));
        afpRequest.AdvanceIfOdd();
    }

    DBGWRITE(DbgLevelInfo, L"[afpuser:FPChangePswd]UAM Str:  %s\n", szUAMVersion);
    DBGWRITE(DbgLevelInfo, L"[afpuser:FPChangePswd]Username: %s\n", szUserName);
    
    switch(uamType)
    {
        case afpUAMNone:
            return( afpBadUAM );
        
        case afpUAMCleartext:
        {
            char	szOldPassword[UAM_CLRTXTPWDLEN+1];
            char	szNewPassword[UAM_CLRTXTPWDLEN+1];

            //
            //Grab the old and new cleartext passwords from the buffer.
            //
            afpRequest.GetRawData(szOldPassword, UAM_CLRTXTPWDLEN);
            szOldPassword[UAM_CLRTXTPWDLEN+1] = 0;
            afpRequest.GetRawData(szNewPassword, UAM_CLRTXTPWDLEN);
            szNewPassword[UAM_CLRTXTPWDLEN+1] = 0;

            //**** TO BE IMPLEMENTED ****
            //afpError = afpImpChangePswd(
            //				szUserName,
            //				szOldPassword,
            //				szNewPassword
            //				);

            SecureZeroMemory(szOldPassword, sizeof(szOldPassword));
            SecureZeroMemory(szNewPassword, sizeof(szNewPassword));

            afpError = AFP_OK;
            break;
        }
        
        case afpUAMDHCAST128:
        {
            char 		szOldPassword[DHX_PASSWDLEN+1];
            char		szNewPassword[DHX_PASSWDLEN+1];
            WCHAR		szNewPasswordW[DHX_PASSWDLEN+1];
            DHXInfo*	dhxInfo = NULL;
            
            //
            //This may be our second time through in which case dxhInfo is already
            //created and filled with information.
            //
            dhxInfo = (DHXInfo*)dsiSession->extLoginInfo;
            
            if (dhxInfo == NULL)
            {
                //
                //Must be our first time through, so no blob info saved yet.
                //
                dhxInfo = (DHXInfo*)LocalAlloc(LMEM_ZEROINIT, sizeof(DHXInfo));
                
                if (dhxInfo == NULL)
                {
                    //
                    //We failed to allocated the necessary block of data to store information
                    //between login api calls. No choice but to bail out.
                    //
                    
                    DBGWRITE(DbgLevelWarning, L"[afp:FPChangePswd]Failed to allocate DHXInfo structure, exiting.\n");
                    return( afpMiscErr );
                }
            }
            
            memset(szOldPassword, 0, sizeof(szOldPassword));
            memset(szNewPassword, 0, sizeof(szNewPassword));
            
            //
            //This routine does 2 things:
            //	1) re-logs into the server via DH key exchanges for auth
            //	2) decrypts received old and new passwords based on DH key exchange
            //
            afpError = DHXChangePassword(
                            afpRequest,
                            afpReply,
                            dsiSession,
                            szOldPassword,
                            sizeof(szOldPassword),
                            szNewPassword,
                            sizeof(szNewPassword),
                            dhxInfo
                            );
            
            if (afpError == afpAuthContinue)
            {
                //
                //We've only complete the first step in the key exchange/encryption
                //process. Returning afpAuthContinue and we'll be called again with
                //the new/old password encrypted payload.
                //

                dsiSession->extLoginInfo = dhxInfo;

                return( afpError );
            }
            
            //
            //We're done with the DHXInfo so free it. Also be sure to clear
            //the blob field in the DSI session, or we'll be in trouble
            //next time we change password.
            //
            LocalFree(dhxInfo);
            dsiSession->extLoginInfo = NULL;
            
            //
            //We successfully re-logged onto the server and successfully decrypted
            //the old and new passwords. Now convert to unicode strings and
            //call the change password implementation routine.
            //
            
            if (AFP_SUCCESS(afpError))
            {
                if (!AFPMacToWindowsString(szNewPassword, sizeof(szNewPasswordW), szNewPasswordW))
                {
                    DBGWRITE(DbgLevelWarning, L"[afp:FPChangePswd]Error converting new password to unicode\n");
                    afpError = afpMiscErr;
                }
                else
                {
                    //**** TO BE IMPLEMENTED ****
                    //afpError = afpImpChangePswd(
                    //				szUserName,
                    //				szOldPassword,
                    //				szNewPassword
                    //				);
                }
            }

            SecureZeroMemory(szOldPassword, sizeof(szOldPassword));
            SecureZeroMemory(szNewPassword, sizeof(szNewPassword));
            SecureZeroMemory(szNewPasswordW, sizeof(szNewPasswordW));
            break;
        }
        
        default:
            break;
    }

    DBGWRITE(DbgLevelInfo, L"[afpuser:FPChangePswd]Returning: %d\n", afpError);
    
    return( afpError );
}


/*
 * FPLogout()
 *
 * Description:
 *		The user is logging out of the server. Clean up the token handle
 *		and user state.
 *
 * Returns: AFPERROR
 */

AFPERROR FPLogout(
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{	
    UNREFERENCED_PARAMETER(afpReply);
    UNREFERENCED_PARAMETER(afpRequest);
    //
    //Close the handle to the token we're using for impersenation.
    //
    if (	(afpSession->hUserToken != NULL)			&&
            (afpSession->dwState & sessAuthenticated)	)
    {
        CloseHandle(afpSession->hUserToken);

        afpSession->hUserToken	= NULL;
        afpSession->dwState		&= ~sessAuthenticated;

        if (afpSession->afpSessionObject)
        {
            delete (LAFPSession*)afpSession->afpSessionObject;
            afpSession->afpSessionObject = NULL;
        }
        
        //
        //Notify the administrator of the client logoff the system.
        //

        EventWriteAFPServerEvt_ClientLogoff(afpSession->userName);
    }
    else
    {
        return( afpParmErr );
    }

    return( AFP_OK );
}


/*
 * FPMapID()
 *
 * Description:
 *		Map a user ID to a user name.
 *
 * Returns: AFPERROR
 */

AFPERROR FPMapID(
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{
    UNREFERENCED_PARAMETER(afpSession);

    AFPERROR	afpError	= AFP_OK;
    uint8		afpSubFunc	= 0;
    uint32		afpUserID	= 0;
    WCHAR		szName[64];

    afpRequest.Advance(sizeof(int8));
    
    //
    //What are we doing, user or group?
    //
    afpSubFunc = afpRequest.GetInt8();
    
    //
    //Retrieve the id of the user or group to obtain.
    //
    afpUserID = afpRequest.GetInt32();

    DBGWRITE(DbgLevelInfo, L"[afp:FPMapID]Looking for ID %d\n", afpUserID);
        
    switch(afpSubFunc)
    {
        //Looking for user name
        case 1:
            afpError = AFPGetUserNameByID(afpUserID, szName);

            if (AFP_SUCCESS(afpError))
            {
                DBGWRITE(DbgLevelInfo, L"[afp:FPMapID]Mapped id to name %s\n", szName);
                afpReply.AddUniStringAsPascal(szName);
            }
            break;
        
        //Looking for group name
        case 2:
            afpReply.AddCStringAsPascal("Everyone");
            break;

        //Unicode user name
        case 3:
            afpError = AFPGetUserNameByID(afpUserID, szName);

            if (AFP_SUCCESS(afpError))
            {
                DBGWRITE(DbgLevelInfo, L"[afp:FPMapID]Mapped id to name %s\n", szName);
                afpReply.AddUniString(szName);
            }
            break;
        
        //Unicode group name
        case 4:
            afpReply.AddUniString(TEXT("Everyone"));
            break;
        
        default:
            ASSERT(0);
            break;
    }
        
    return( AFP_OK );
}


/*
 * FPGetSrvrMsg()
 *
 * Description:
 *		The client calls this API to check and see if there is a server
 *		message pending. The tells the server what type of message it's
 *		looking for, as follows:
 *
 *		Login message	= 0
 *		Server message	= 1  (in response to a message attn code)
 *		
 *		For now, we just ignore the msg bitmap since it really servers
 *		no purpose.
 *
 * Returns: AFPERROR
 */

AFPERROR FPGetSrvrMsg(
    IN	AfpBuffer&	afpRequest,
    IN	AfpBuffer&	afpReply,
    IN	LPSESSION	afpSession
    )
{
    UNREFERENCED_PARAMETER(afpSession);

    unichar			message[AFP_MSG_MAXLEN+1];
    LRegistry		theRegistry;
    uint16			afpMessageType		= 0;
    uint16			afpMessageBitmap	= 0;
    
    afpRequest.Advance(sizeof(int16));

    afpMessageType		= afpRequest.GetInt16();
    afpMessageBitmap	= afpRequest.GetInt16();

    switch (afpMessageType)
    {
        case AFP_MSG_LOGINTYPE:
            DBGWRITE(DbgLevelInfo, L"[AFP]Message type = Logon\n");

            theRegistry.GetLogonMessage(message);
            break;

        case AFP_MSG_SERVERTYPE:
            DBGWRITE(DbgLevelInfo, L"[AFP]Message type = Server\n");

            //
            //Since the server message is not saved anywhere, we just grab
            //it from our global source.
            //
            StringCbCopy(message, sizeof(message), gServerMessage);
            break;

        default:
            DBGWRITE(DbgLevelWarning, L"[AFP]Bad message type in FPGetSrvrMsg!\n");
            ASSERT(0);
            break;
    }
    
    DBGWRITE(DbgLevelInfo, L"[AFP]Message = %s\n", message);

    //
    //Pack in all the parameters.
    //
    afpReply.AddInt16(afpMessageType);
    afpReply.AddInt16(afpMessageBitmap);
    
    //
    //Starting with AFP3.0, the client sets bit 1 of the message
    //bitmap to indicate that it wants the message string in
    //unicode format.
    //
    if (afpMessageBitmap & kSendMessageAsUnicode)
    {
        afpReply.AddUniString(message);
    }
    else
    {
        afpReply.AddUniStringAsPascal(message);
    }
    
    return( AFP_OK );
}


