/*
 * 	logonimp.cpp
 *	Copyright (c) 2005, Some-guy Software.
 *
 * Description:
 *		Contains the implementation routines for client logon.
 *
 */

#include "StdAfx.h"
#include "afp.h"
#include "LAFPSession.h"
#include "logonimp.h"
#include "afpEvents.h"

#include <ntsecapi.h>


/*
 * afpImpUserLogon()
 *
 * Description:
 *		The client is attempting to authenticate to this afp server.
 *
 * Returns: AFPERROR
 */

AFPERROR afpImpUserLogon(
    LPSESSION	afpSession,
    AfpBuffer&	afpReply,
    WCHAR*		afpPasswordW,
    WORD		cbAfpPasswordW
)
{
    UNREFERENCED_PARAMETER(afpReply);

    HANDLE		hToken		= NULL;
    AFPERROR	afpError	= AFP_OK;
    
    if (LogonUser(
                afpSession->userName,
                (lstrlen(afpSession->domainName)) ? afpSession->domainName : NULL,
                afpPasswordW,
                LOGON32_LOGON_NETWORK,
                LOGON32_PROVIDER_DEFAULT,
                &hToken) == 0)
    {
        //
        //Logon failed, return the appropriate afp error code.
        //
        DBGWRITE(DbgLevelInfo, L"[logonimp]LogonUser() failed (%d)\n", GetLastError());
        
        afpError = AFPMapWinErrorToAFP(GetLastError(), static_cast<DWORD>(afpUserNotAuth));
    }
    else
    {
        //
        //Clear out the password buffer for security purposes.
        //
        SecureZeroMemory(afpPasswordW, cbAfpPasswordW);

        if (!DuplicateTokenEx(hToken, MAXIMUM_ALLOWED, NULL,
                    SecurityImpersonation, TokenPrimary, &afpSession->hUserToken))
        {
            DBGWRITE(DbgLevelInfo, L"[logonimp]DuplicateToken() failed! (%d)\n", GetLastError());
            ASSERT(0);
        }
        
        RevertToSelf();
        CloseHandle(hToken);

        AFPAdjustPrivilege(SE_IMPERSONATE_NAME, TRUE, afpSession->hUserToken);

        //
        //Set the flag in the sessions that tells us this session is authenticated.
        //
        afpSession->dwState |= sessAuthenticated;
        
        DBGWRITE(DbgLevelInfo, L"[logonimp]Login for user %s successfull!\n", afpSession->userName);
    }
    
    if (AFP_SUCCESS(afpError))
    {
        //
        //Create the LAFPSession object that will track the *AFP* session.
        //
        LAFPSession* afpAFPSession = NULL;

        afpAFPSession = new LAFPSession(afpSession);

        if (afpAFPSession == NULL)
        {
            DBGWRITE(DbgLevelWarning, L"[logonimp]Failed to allocate new afp session object!\n");
            ASSERT(0);

            afpError = afpMiscErr;
        }
        else
        {
            afpSession->afpSessionObject = afpAFPSession;

            //
            //If we get here, then everything succeeded. We'll log an event
            //notifying the administrator that this user has logged on.
            //

            EventWriteAFPServerEvt_ClientLoggedOn( afpSession->userName );
        }
    }
    
    if (!AFP_SUCCESS(afpError))
    {
        //
        //If we had a failure to logon, then set a flag that
        //tells DSI to close the session down.
        //
        afpSession->dwState |= sessKillSession;		
    }
    
    return( afpError );
}

