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
#include "afp_session.h"
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
        DBGWRITE(dbg_level_info, L"[logonimp]LogonUser() failed (%d)\n", GetLastError());
        
        afpError = afp_map_win_error_to_afp(GetLastError(), static_cast<DWORD>(afpUserNotAuth));
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
            DBGWRITE(dbg_level_info, L"[logonimp]DuplicateToken() failed! (%d)\n", GetLastError());
            ASSERT(0);
        }
        
        RevertToSelf();
        CloseHandle(hToken);

        afp_adjust_privilege(SE_IMPERSONATE_NAME, TRUE, afpSession->hUserToken);

        //
        //Set the flag in the sessions that tells us this session is authenticated.
        //
        afpSession->dwState |= sessAuthenticated;
        
        DBGWRITE(dbg_level_info, L"[logonimp]Login for user %s successfull!\n", afpSession->userName);
    }
    
    if (AFP_SUCCESS(afpError))
    {
        //
        //Create the AFPSession object that will track the *AFP* session.
        //
        afp_session* afpAFPSession = NULL;

        afpAFPSession = new afp_session(afpSession);

        if (afpAFPSession == NULL)
        {
            DBGWRITE(dbg_level_warning, L"[logonimp]Failed to allocate new afp session object!\n");
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

