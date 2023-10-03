//===================================================================
// dsi.c
//===================================================================

#include "stdafx.h"
#include <winerror.h>

#include "dsi.h"
#include "afp.h"
#include "afphelp.h"
#include "attention.h"
#include "volumes.h"
#include "afpworker.h"
#include "AutoLock.h"
#include "AFPSession.h"
#include "AFPCriticalSection.h"
#include "counters.h"
#include "afpEvents.h"


LPSESSION			gSessionsList			= NULL;
HANDLE				gCompletionPort			= NULL;
SOCKET				gListenSocket			= INVALID_SOCKET;
SHORT				gWorkerThreadRefCount	= 0;
HANDLE				gSendEvent				= NULL;
LPSEND_QUEUE		gSendQueue				= NULL;
WORD				gNumWorkerThreads		= 0;
BOOL				gRunning				= TRUE;
CRITICAL_SECTION	gcsSessionList;
CRITICAL_SECTION	gcsSendQueue;

//---------------------------------------------------------------------
// DSIInit()
//---------------------------------------------------------------------
// Initialize the DSI. This MUST be called before making any DSI calls.

DWORD DSIInit()
{
    WSADATA 		wsaData;
    DWORD			dwError;
    HANDLE			hThread;
    DWORD			dwThreadID;
    
    gRunning = TRUE;

    dwError = WSAStartup(MAKEWORD(1,1), &wsaData);
    
    if (dwError != 0)
    {
        DBGWRITE(DbgLevelInfo, L"[DSI]WSAStartup() failed! (%d)\n", dwError);
        return(dwError);
    }
    
    InitializeCriticalSection(&gcsSessionList);
    InitializeCriticalSection(&gcsSendQueue);
    
    //
    //Create the accepting thread that will accept connection requests.
    //
    hThread = CreateThread(
                NULL,
                0,
                DSIAcceptThread,
                NULL,
                0,
                &dwThreadID
    );
    
    CloseHandle(hThread);
    
    hThread = CreateThread(
                NULL,
                0,
                AFPAttentionThread,
                NULL,
                0,
                &dwThreadID
    );
    
    CloseHandle(hThread);
    
    hThread = CreateThread(
                NULL,
                0,
                DSISendThread,
                NULL,
                0,
                &dwThreadID
    );
    
    CloseHandle(hThread);
    
    hThread = CreateThread(
                NULL,
                0,
                DSIScavangerThread,
                NULL,
                0,
                &dwThreadID
    );
    
    CloseHandle(hThread);
    
    //
    //Initialize our performance counters for use in perfmon
    //
    dwError = AFPInitializePerfCounters();
    if (dwError != ERROR_SUCCESS)
    {
    }

    return(NO_ERROR);
}


//---------------------------------------------------------------------
// DSIShutdown()
//---------------------------------------------------------------------
// We're shutting down, try to clean everything up.

void DSIShutdown(void)
{
    WORD	i;
    
    //
    //The server is shutting down. First thing to do is send
    //an attention message to all connected clients informing
    //them of the closure.
    //
    DSISendAttention(NULL, ATTN_SERVER_SHUTDOWN);
    
    //
    //Tell the listening thread to stop accepting connections.
    //
    gRunning = FALSE;
    
    //
    //This will stop all pending I/O on the listening socket
    //and allow the acceptance thread to shutdown.
    //
    closesocket(gListenSocket);

    //
    //This releases the send thread.
    //
    SetEvent(gSendEvent);
    
    //
    //Give some time for the message to go out.
    //
    Sleep(5000);
    
    //
    //This tells our worker threads to shutdown.
    //
    for (i = 0; i < gNumWorkerThreads; i++)
    {
        PostQueuedCompletionStatus(
                gCompletionPort,
                1,
                1,
                NULL
        );
    }

    AFPShutdownWorkerThread();
    
    while((gWorkerThreadRefCount > 0) && (gSessionsList))
        Sleep(500);
}


//---------------------------------------------------------------------
// AcceptThread()
//---------------------------------------------------------------------
// Thread that accepts incoming connection requests and sets up the
// I/O completion port.

DWORD WINAPI DSIAcceptThread(
        IN LPVOID	pParam
)
{	
    UNREFERENCED_PARAMETER(pParam);

    SOCKET			sock		= INVALID_SOCKET;
    SOCKET			newsock 	= INVALID_SOCKET;
    DWORD			dwError		= NO_ERROR;
    DWORD			dwRecvBytes	= 0;
    DWORD			dwFlags		= 0;
    HANDLE			hThread		= INVALID_HANDLE_VALUE;
    HANDLE			compPort	= INVALID_HANDLE_VALUE;
    LPSESSION		session		= NULL;
    LPPER_IO_DATA	perIOData	= NULL;
    DWORD			dwThreadID;
    WORD			i;
    SYSTEM_INFO		systemInfo;
    SOCKADDR_IN		inetAddr;
    
    gWorkerThreadRefCount++;

    //
    //Create the completion port that I/O requests will use.
    //
    compPort = CreateIoCompletionPort(
                            INVALID_HANDLE_VALUE,
                            NULL,
                            0,
                            0
    );
    
    if (compPort == NULL)
    {
        //
        //We failed to create our port, all execution should stop.
        //
        
        dwError = GetLastError();
        
        DBGWRITE(DbgLevelWarning, L"[DSI]Completion port creation failed! (%d)\n", dwError);

        gWorkerThreadRefCount--;
        return(0);
    }
    
    gCompletionPort = compPort;
    
    //
    //Create the worker thread(s) that will service the I/O requests.
    //We create 2 threads for each processor to get the maximum
    //benefit out of the completion port model.
    //
    GetSystemInfo(&systemInfo);
    
    gNumWorkerThreads = (WORD)(systemInfo.dwNumberOfProcessors * 2);
    
    for (i = 0; i < gNumWorkerThreads; i++)
    {
        hThread = CreateThread(
                    NULL,
                    0,
                    DSIWorkerThread,
                    compPort,
                    0,
                    &dwThreadID
        );
        
        CloseHandle(hThread);
    }

    //
    //Create the listening socket for AFP connections.
    //
    sock = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    
    if (sock == INVALID_SOCKET)
    {
        //
        //For some reason we couldn't creat our socket.
        //
        DBGWRITE(DbgLevelWarning, L"[DSI]Failed to create accept socket! (%d)\n", WSAGetLastError());

        gWorkerThreadRefCount--;
        return(0);
    }
    
    inetAddr.sin_family			= AF_INET;
    inetAddr.sin_addr.s_addr	= htonl(INADDR_ANY);
    inetAddr.sin_port			= htons(AFP_TCP_PORT);
    
    dwError = bind(sock, (PSOCKADDR)&inetAddr, sizeof(inetAddr));
    
    if (dwError != ERROR_SUCCESS)
    {
        DBGWRITE(DbgLevelWarning, L"[DSI]Failed to bind accept socket! (%d)\n", WSAGetLastError());

        gWorkerThreadRefCount--;
        
        closesocket(sock);
        return(0);
    }
    
    //
    //Prepare the socket for listening. We let as many connection
    //requests as possible (or reasonable) in the queue.
    //
    dwError = listen(sock, SOMAXCONN);
    
    if (dwError == SOCKET_ERROR)
    {		
        DBGWRITE(DbgLevelWarning, L"[DSI]listen() failed! (%d)\n", WSAGetLastError());
        
        gWorkerThreadRefCount--;

        closesocket(sock);
        return(0);
    }
    
    DBGWRITE(DbgLevelInfo, L"[DSI]Ready to accept AFP connections...\n");
    
    gListenSocket = sock;
    
    while(gRunning)
    {
        newsock = WSAAccept(sock, NULL, NULL, NULL, 0);
        
        if (newsock == INVALID_SOCKET)
        {
            dwError = WSAGetLastError();
            
            if (dwError == WSAEINTR)
            {
                //
                //This error means that we have closed the socket
                //and are attempting to shut the thread down.
                //
                DBGWRITE(DbgLevelInfo, L"[DSI]Accept thread shutting down...\n");
                continue;
            }
            
            //
            //An error ocurred and the acceptance failed. Continue on
            //to the next request.
            //
            DBGWRITE(DbgLevelWarning, L"[DSI]Accept socket failed! (%d)\n", dwError);
            continue;
        }
        
        DBGWRITE(DbgLevelAll, L"[DSI]Socket number %d connected\n", newsock);
        
        //
        //Create the session structure that we use to track all
        //the open connections.
        //
        session = (LPSESSION)DBG_ALLOC(sizeof(SESSION));
        
        if (session == NULL)
        {
            //
            //Fatal error, we couldn't allocate memory for our storage.
            //
            DBGWRITE(DbgLevelWarning, L"[DSI]Failed to allocate session struct! (%d)\n", GetLastError());
            
            closesocket(newsock);
            continue;
        }
        
        //
        //We need a critical section for manipulating the session data.
        //
        InitializeCriticalSection(&session->cs);
        
        //
        //Save the socket information.
        //
        session->sock = newsock;
        
        //
        //Create an I/O data struct to handle the first I/O request. This
        //pointer will be retained for all recv's for this session's life.
        //
        perIOData = (LPPER_IO_DATA)DBG_ALLOC(sizeof(PER_IO_DATA));
        
        //
        //Setup the per-I/O data to be set/saved.
        //
        perIOData->buffer			= (PCHAR)DBG_ALLOC(DATA_BUF_SIZE);
        perIOData->bufferSize		= DATA_BUF_SIZE;
        perIOData->dataBuffer.len	= DATA_BUF_SIZE;
        perIOData->dataBuffer.buf	= perIOData->buffer;
        perIOData->offset			= 0;
        perIOData->operationType	= RECV_POSTED;
        perIOData->overflow			= NULL;
        perIOData->overflowSize		= 0;
        
        session->dwState 			= 0;
        session->pRecvIOData		= perIOData;
        session->dwLastTickleRecvd	= GetTickCount();
        session->dwLastTickleSent 	= GetTickCount();
        
        DSIAddSessionToList(session);
        
        //
        //Now associate the new, accpeted, socket with our completion port.
        //
        CreateIoCompletionPort(
                (HANDLE)newsock,
                compPort,
                (ULONG_PTR)session,
                0
        );
        
        dwError = WSARecv(
                    newsock,
                    &perIOData->dataBuffer,
                    1,
                    &dwRecvBytes,
                    &dwFlags,
                    (OVERLAPPED *)perIOData,
                    NULL
        );
        
        if (dwError == SOCKET_ERROR)
        {
            //
            //Make sure we are in a I/O pending state.
            //
            dwError = WSAGetLastError();
            
            if (dwError != WSA_IO_PENDING)
            {
                DBGWRITE(DbgLevelWarning, L"[DSI]WSARecv() returned %d instead of WSA_IO_PENDING\n", dwError);
                ASSERT(0);
            }
            else
            {
                InterlockedIncrement(&session->refCount);
            }
        }
    }//end while
    
    DSICleanupAll();

    gWorkerThreadRefCount--;
    
    DBGWRITE(DbgLevelInfo, L"[DSI]Accept thread gone!\n");
    return(NO_ERROR);
}


//---------------------------------------------------------------------
// DSIWorkerThread()
//---------------------------------------------------------------------
// Thread that accepts incoming connection requests and sets up the
// I/O completion port.
//

DWORD WINAPI DSIWorkerThread(
        IN LPVOID	pParam
)
{
    HANDLE			compPort		= (HANDLE)pParam;
    LPSESSION		pSession		= NULL;
    LPPER_IO_DATA	perIOData		= NULL;
    DWORD			dwBytesXferd	= 0;
    DWORD			dwFlags			= 0;
    DWORD			dwError			= 0;
    WORD			wNextAction		= 0;
    
    gWorkerThreadRefCount++;
    
    //
    //Adjust the privilege of this thread so we can accept
    //logons over the network.
    //
    if (!AFPAdjustPrivilege(SE_TCB_NAME, TRUE)) {
        DBGWRITE(DbgLevelInfo, L"[DSI]AdjustPrivilege() in worker failed enabling SE_TCB_NAME!\n");
    }

    if (!AFPAdjustPrivilege(SE_SECURITY_NAME, TRUE)) {
        DBGWRITE(DbgLevelInfo, L"[DSI]AdjustPrivilege() in worker failed enabling SE_SECURITY_NAME!\n");
    }

    while(gRunning)
    {
        GetQueuedCompletionStatus(
                compPort,
                &dwBytesXferd,
                (LPDWORD)&pSession,
                (LPOVERLAPPED*)&perIOData,
                INFINITE
        );
                                
        if ((perIOData == NULL) || (gRunning == FALSE))
        {
            DBGWRITE(DbgLevelInfo, L"[DSI]Shutting down worker thread...\n");
            ASSERT(gRunning == FALSE);
            
            //
            //The reason we call continue below is because gRunning
            //should now be false.
            //
            continue;
        }
        
        //
        //We need to make sure we're not disposing of the session
        //memory before we go on.
        //
        {
            AutoLock lock(&pSession->cs);

            if ((dwBytesXferd == 0) 						&& 
                ((perIOData->operationType == RECV_POSTED)	||
                (perIOData->operationType  == SEND_POSTED))	)
            {
                //
                //Zero byts transferred indicates socket has been closed
                //by the client. We free our handles and close the socket.
                //
                DBGWRITE(DbgLevelInfo, L"[DSI]Closing connection by client request (%d)\n", pSession->refCount);
            
                //
                //Signal to all other threads that this session is going away.
                //
                pSession->dwState |= sessClosing;

                //
                //Get rid of the initial ref count increment.
                //
                InterlockedDecrement(&pSession->refCount);
                continue;
            }
        }

        //
        //Service the I/O request completed.
        //
        switch(perIOData->operationType)
        {
            case RECV_POSTED:
                perIOData->ioSize = dwBytesXferd;

                if (DSIProcessRecvBytes(pSession, dwBytesXferd, perIOData) == FALSE)
                {
                    AutoLock lock(&pSession->cs);

                    //DBGWRITE(DbgLevelAll, L"[DSI]Request processed for session 0x%x\n", pSession));

                    //
                    //We're done processing the request, the reply should already
                    //be on it's way to the client. Now check for any data in
                    //the overflow buffer.
                    //
                    if (perIOData->overflowSize > 0)
                    {
                        BOOL	succeeded = FALSE;

                        //
                        //We have overflow data from the last request,
                        //we must process it.
                        //
                        ASSERT(perIOData->overflowSize <= perIOData->bufferSize);
                        
                        RtlCopyMemory(
                                perIOData->buffer,
                                perIOData->overflow,
                                perIOData->overflowSize);
                                
                        dwBytesXferd = perIOData->overflowSize;
                        
                        DBG_FREE(perIOData->overflow);
                        
                        perIOData->overflow			= NULL;
                        perIOData->overflowSize		= 0;
                        perIOData->offset			= 0;
                        perIOData->operationType 	= RECV_POSTED;

                        ZeroMemory(&perIOData->overLapped, sizeof(OVERLAPPED));
                        
                        InterlockedIncrement(&pSession->refCount);
                        
                        //
                        //Now process the new data just like above.
                        //
                        succeeded = PostQueuedCompletionStatus(
                                            gCompletionPort,
                                            dwBytesXferd,
                                            (ULONG_PTR)pSession,
                                            (LPOVERLAPPED)perIOData
                                            );
                        
                        if (!succeeded)
                        {
                            DBGWRITE(DbgLevelInfo, L"[DSI]PostQueuedCompletionStatus() failed! (%d)\n", GetLastError());
                            ASSERT(0);
                        }

                        wNextAction = NO_ACTION;
                    }
                    else
                    {
                        //
                        //Post a recv for the next request from the client.
                        //
                        wNextAction = POST_RECV;
                        
                        //
                        //Need to do this so we look in the right spot in our buffer.
                        //
                        perIOData->offset = 0;
                    }
                }
                else
                {
                    //
                    //There is more data for this request, go get it.
                    //
                    wNextAction = POST_RECV;
                }

                InterlockedDecrement(&pSession->refCount);
                break;
            
            case SEND_POSTED:
                //
                //We are being notified that a send operation has completed. dwBytesXferd
                //contains the number of bytes that were sent to the receiving client.
                //

                //
                //Update the number of bytes sent for the performance counters.
                //
                AFPUpdateBytesPerSecond(NULL, &dwBytesXferd);

                //
                //Set our write offset to account for the data already sent so
                //we can calculate if we need to continue to send more data.
                //
                perIOData->offset += dwBytesXferd;
                
                //
                //If we didn't send all the data AND we are not killing the session due
                //to logon failure or getcmdstatus call, continue to send.
                //
                if ((perIOData->offset < perIOData->ioSize) && ((pSession->dwState & sessKillSession) == 0))
                {
                    //
                    //We didn't send all the data in the last go around. Note that since
                    //we're using overlapped I/O we should never get here.
                    //
                    wNextAction = POST_SEND;
                }
                else
                {
                    //
                    //The send operation is complete. We don't need this
                    //per I/O buffer anymore.
                    //
                    DBG_FREE(perIOData->buffer);
                    DBG_FREE(perIOData);
                    
                    wNextAction = NO_ACTION;
                }

                InterlockedDecrement(&pSession->refCount);
                break;
                
            case NO_ACTION:
                break;
            
            default:
                DBGWRITE(DbgLevelInfo, L"[DSI]Bad operation type! (%d)\n", perIOData->operationType);
                ASSERT(0);
                break;
        }
        
        //
        //The server was told to shutdown while we were in the middle of a
        //network operation. Close down gracefully.
        //
        if (!gRunning)
        {
            //
            //Zero byts transferred indicates socket has been closed
            //by the client. We free our handles and close the socket.
            //
            DBGWRITE(DbgLevelInfo, L"[DSI]Closing connection due to shutdown (%d)\n", pSession->refCount);
            
            //
            //Signal to all other threads that this session is going away adn
            //get rid of the initial ref count increment.
            //

            pSession->dwState |= sessClosing;
            InterlockedDecrement(&pSession->refCount);

            break;
        }
        
        //
        //Now, based on the new operation type as set above, post the next
        //I/O operation on the socket.
        //
        switch(wNextAction)
        {
            
            case POST_SEND:
                ASSERT(0);
                break;
            
            case POST_RECV:
                //
                //Setup the next overlapped call.
                //
                ZeroMemory(&perIOData->overLapped, sizeof(OVERLAPPED));
                
                perIOData->dataBuffer.len	= perIOData->bufferSize - perIOData->offset;
                perIOData->dataBuffer.buf	= &perIOData->buffer[perIOData->offset];
                perIOData->operationType	= RECV_POSTED;
                
                //
                //Reset flags as they may have been set on last output
                //
                dwFlags = 0;

                dwError = WSARecv(
                            pSession->sock,
                            &perIOData->dataBuffer,
                            1,
                            NULL,
                            &dwFlags,
                            &perIOData->overLapped,
                            NULL
                );
                
                if (dwError == SOCKET_ERROR)
                {
                    //
                    //Make sure we are in a I/O pending state.
                    //
                    dwError = WSAGetLastError();
                    
                    if (dwError != WSA_IO_PENDING)
                    {
                        DBGWRITE(DbgLevelInfo, L"[DSI]WSARecv() returned %d instead of WSA_IO_PENDING\n", dwError);
                    }
                    else
                    {
                        InterlockedIncrement(&pSession->refCount);
                    }
                }
                else
                    InterlockedIncrement(&pSession->refCount);

                DBGWRITE(DbgLevelAll, L"[DSI]Receive posted for session 0x%x\n", pSession);
                break;
                
            case NO_ACTION:
                break;
                
            default:
                ASSERT(0);
                break;
        }
    }
    
    gWorkerThreadRefCount--;
    
    DBGWRITE(DbgLevelInfo, L"[DSI]Worker thread gone!\n");
    
    return(0);
}


//---------------------------------------------------------------------
// DSITerminateSession()
//---------------------------------------------------------------------
// Terminate the DSI session, release all memory and remove it from
// the session list.

void DSITerminateSession(
        IN LPSESSION	pSession,	//pointer to session to terminate
        IN BOOL			fTimeout	//set if session terminated due to timeout
        )
{
    DBGWRITE(DbgLevelInfo, L"[DSI]Closing session per scavenger request\n");
                        
    //
    //Check if we were called as a result of a session timing out.
    //

    if (fTimeout == TRUE)
    {
        //
        //The session has timed out, so we shouldn't rely on hearing
        //from the session again. Set things so we have a successful
        //forced closure of the session.
        //

        DBGWRITE(DbgLevelInfo, L"[DSI]Session timed out, setting ref count to zero and closing...\n");

        InterlockedExchange(&pSession->refCount, 0);
        pSession->dwState |= sessClosing;

        //
        //Notify the administrator that a session was kicked off.
        //

        EventWriteAFPServerEvt_ClientTimeout(pSession->userName);
    }
        
    //
    //Upon return from this call, pSession is no longer a valid pointer.
    //

    DSICleanupSession(pSession);
}


//---------------------------------------------------------------------
// DSICheckIfSessionMarkedForClosure()
//---------------------------------------------------------------------
// Check to see if a session is marked for closure. If so, call the
// DSITerminate() function.
//
// RETURNS: TRUE if session was closed.

bool DSICheckIfSessionMarkedForClosure(
        IN LPSESSION	pSession
        )
{
    //
    //Check to see if this session is marked for deletion. If so,
    //terminate it immediately.
    //
    if (pSession->dwState & sessClosing)
    {
        if (pSession->refCount > 0)
        {
            //
            //The sessions ref count is not 0 which means something else
            //is still busy with the session. Go around again and we'll
            //catch it next time through.
            //

            DBGWRITE(
                DbgLevelInfo,
                L"[DSI]Session marked for deletion still has pending transactions!! (%d)\n",
                pSession->refCount
                );

            assert(0);

            //
            //If we get here, it could be due to a counter bug or perhaps a bad
            //client. So, we decerement the counter and go around again and
            //if it is a bad client, we'll at least not hang here forever.
            //
            InterlockedDecrement(&pSession->refCount);

            return( false );
        }
        
        //
        //Call the routine that does the dirty work of terminating the session.
        //
        
        DSITerminateSession(pSession, false);

        return( true );
    }

    return( false );
}


//---------------------------------------------------------------------
// DSICheckIfSessionTimedOut()
//---------------------------------------------------------------------
// Check to see if a session has timed out and needs closure. If so, 
// call the DSITerminate() function.
//
// RETURNS: TRUE if session was closed.

bool DSICheckIfSessionTimedOut(
        IN LPSESSION		pSession
        )
{
    bool	fCloseSession	= true;
    DWORD	dwTicks			= GetTickCount();

    EnterCriticalSection(&pSession->cs);

    if ((dwTicks - pSession->dwLastTickleRecvd) > AFP_CLIENT_LOST)
    {
        if ((pSession->bAFPVersion >= afpVersion30) && ((pSession->dwState & sessClientSleeping) != 0))
        {
            //
            //We don't close sessions where the client has just gone to sleep
            //if the client connected using an AFP 3.0 or later client and the
            //extended timeout has not expired.
            //

            if ((dwTicks - pSession->dwLastTickleRecvd) < AFP_CLIENT_LOST_SLEEPING) {

                fCloseSession = false;
            }
        }
        
        if (fCloseSession == true)
        {
            DBGWRITE(
                DbgLevelInfo,
                (L"[DSI]Session timed out and is being terminated\n")
                );

            LeaveCriticalSection(&pSession->cs);
            
            //
            //Call the routine that does the dirty work of terminating the session.
            //
            
            DSITerminateSession(pSession, true);

            return( true );
        }
    }

    LeaveCriticalSection(&pSession->cs);

    return( false );
}


//---------------------------------------------------------------------
// DSICheckIfTickleNeeded()
//---------------------------------------------------------------------
// Check to see if a session requires a tickle to stay alive.
//
// RETURNS: none

void DSICheckIfTickleNeeded(
        IN LPSESSION pSession
        )
{
    DWORD	dwTicks	= GetTickCount();

    //
    //Check and see if we need to tickle a client connection. We need
    //to do this regularly otherwise the client will tear down the connection.
    //

    if ((dwTicks - pSession->dwLastTickleSent) > AFP_TICKLE_TIME)
    {
        //
        //We don't send a tickle to a client that won't hear it because
        //it is sleeping.
        //

        if ((pSession->dwState & sessClientSleeping) == 0)
        {
            //
            //Call the routine that will send out the tickle. We don't need
            //to check for a return value since clients don't respond to
            //tickle messages.
            //

            DSISendTickle(pSession);
        }
    }
}


//---------------------------------------------------------------------
// DSIScavangerThread()
//---------------------------------------------------------------------
// The savanger's job is to contantly look for ways to free up unused
// resources. It has 3 primary jobs:
//
//	1. Check for clients that haven't talked to us for over 2 minutes.
//	2. Reduce memory in the recv I/O data if it was reallocaed larger.
//	3. Kill sessions that are marked for closure.
//

DWORD WINAPI DSIScavangerThread(
        IN LPVOID	pParam
)
{	
    UNREFERENCED_PARAMETER(pParam);

    LPSESSION	pSession;
    bool		fStatus;
    
    gWorkerThreadRefCount++;
    
    while((gRunning) || (gSessionsList))
    {
        EnterCriticalSection(&gcsSessionList);
        
        pSession = gSessionsList;
        
        while(pSession)
        {
            //
            //Since we may kill the current pointer, get the pointer to the
            //next session in the list now.
            //
            LPSESSION	pNext = (LPSESSION)pSession->next;

            //
            //Check to see if this session is marked for deletion.
            //
            fStatus = DSICheckIfSessionMarkedForClosure(pSession);
            if (fStatus == FALSE)
            {
                //
                //Check for sessions that have timed out.
                //
                fStatus = DSICheckIfSessionTimedOut(pSession);
                if (fStatus == FALSE)
                {
                    //
                    //Finally, check to see if the client requires a tickle.
                    //

                    DSICheckIfTickleNeeded(pSession);
                }
            }

            pSession = pNext;
        }

        LeaveCriticalSection(&gcsSessionList);
        
        //
        //If we still have active sessions, tell the system not to sleep.
        //
        if (DSICountSessions(false) > 0)
        {
            //
            //This flag tells the system that we are active and it resets
            //the system sleep clock. We don't want the system going into
            //hibernation or standby when a session is connected.
            //

            SetThreadExecutionState(ES_SYSTEM_REQUIRED);
        }
        
        //
        //Go to sleep for while and check back later.
        //
        if (gRunning)
        {
            //
            //If we're in a running state (ie. not shutting down) we sleep
            //a while before running again since this is just a maintenance
            //task and not required to run immediately.
            //

            Sleep(SAVANGE_THREAD_SLEEP);
        }
        else
        {
            //
            //If we're shutting down, we sleep for a very short period of
            //time if waiting for other threads to cleanup their sessions.
            //

            Sleep( 100 );
        }
    }
    
    gWorkerThreadRefCount--;
    
    DBGWRITE(DbgLevelInfo, L"[DSI]Scavanger thread terminated!\n");
    
    return(0);
}


//---------------------------------------------------------------------
// DSISendTickle()
//---------------------------------------------------------------------
// Send a tickle packet to the client.
//

DWORD DSISendTickle(
    IN	LPSESSION	pSession
)
{
    LPSEND_QUEUE	pSendQueue;
    
    if (!DSIGetNewSendQueueElement(pSession, &pSendQueue))
    {
        //
        //We failed to allocate a new send queue struct.
        //
        return(GetLastError());
    }

    //
    //Send the volume update attention message to 
    //the client.
    //
    DSIFormatAndSendRequest(
            pSendQueue,
            0,
            NULL,
            DSI_CMD_Tickle
            );
                
    //DBGWRITE(DbgLevelInfo, L"[DSI]Tickle sent\n");
    return(NO_ERROR);
}


//---------------------------------------------------------------------
// DSISendAttention()
//---------------------------------------------------------------------
// Send an attention message to a client. If the input session is NULL,
// this routine will send the message to all connected clients.
//

DWORD DSISendAttention(
    IN	LPSESSION	pSession,
    IN	WORD		wMessage
)
{
    LPSEND_QUEUE	pSendQueue	= NULL;
    WORD 			wAttention 	= htons(wMessage);
    
    if (pSession != NULL)
    {
        if (!DSIGetNewSendQueueElement(pSession, &pSendQueue))
        {
            //
            //We failed to allocate a new send queue struct.
            //
            return(GetLastError());
        }

        //
        //Send the volume update attention message to 
        //the client.
        //
        DSIFormatAndSendRequest(
                pSendQueue,
                sizeof(WORD),
                &wAttention,
                DSI_CMD_Attention
                );
    }
    else
    {
        LPSESSION	pList	= gSessionsList;
        
        EnterCriticalSection(&gcsSessionList);
        
        while(pList)
        {
            //
            //We only want to send messages to authenticated sessions
            //
            if (pList->dwState & sessAuthenticated)
            {
                if (!DSIGetNewSendQueueElement(pList, &pSendQueue))
                {
                    //
                    //We failed to allocate a new send queue struct.
                    //
                    LeaveCriticalSection(&gcsSessionList);
                    return(GetLastError());
                }

                //
                //Send the volume update attention message to 
                //the client.
                //
                DSIFormatAndSendRequest(
                        pSendQueue,
                        sizeof(WORD),
                        &wAttention,
                        DSI_CMD_Attention
                        );
                
                DBGWRITE(DbgLevelInfo, L"[DSI]Attention message sent (0x%x)\n", wMessage);
            }
            
            pList = (LPSESSION)pList->next;
        }

        LeaveCriticalSection(&gcsSessionList);
    }
    
    return(NO_ERROR);
}



//---------------------------------------------------------------------
// DSISendThread()
//---------------------------------------------------------------------
// Thread which gets packets to send off of a queue and sends them
// in an orderly fashion.
//

DWORD WINAPI DSISendThread(
        IN LPVOID	pParam
)
{	
    UNREFERENCED_PARAMETER(pParam);

    LPPER_IO_DATA	sendIOData;
    LPSEND_QUEUE	sendQueue;
    LPSEND_QUEUE	nextQueue;
    DWORD			dwError;
    
    gWorkerThreadRefCount++;
    
    gSendEvent = CreateEvent(NULL, TRUE, TRUE, NULL);
    ResetEvent(gSendEvent);
    
    while((gRunning) || (gSendQueue))
    {
        //
        //The event will be signaled when there is data queued up to be sent.
        //
        WaitForSingleObject(gSendEvent, INFINITE);
                
        //
        //Check to see if we're shutting down or not. If so, then we close
        //our event handle and break out.
        //
        if ((!gRunning) && (gSendQueue == NULL))
        {
            CloseHandle(gSendEvent);
            break;
        }
        
        EnterCriticalSection(&gcsSendQueue);
        
        sendQueue 	= gSendQueue;
        gSendQueue	= NULL;
        
        ResetEvent(gSendEvent);
        
        LeaveCriticalSection(&gcsSendQueue);
        
        while(sendQueue != NULL)
        {			
            sendIOData	= &sendQueue->sendIOData;
            
            //
            //We need to get the next queue now because the current queue
            //could be freed by the worker thread before we complete the
            //send below!
            //
            nextQueue	= (LPSEND_QUEUE)sendQueue->next;
            
            //
            //Send out the data in our storage buffer for this I/O
            //operation.
            //
            RtlZeroMemory(&sendIOData->overLapped, sizeof(OVERLAPPED));

            sendIOData->dataBuffer.len	= sendIOData->ioSize - sendIOData->offset;
            sendIOData->dataBuffer.buf	= sendIOData->buffer + sendIOData->offset;
            sendIOData->operationType 	= SEND_POSTED;
            
            dwError = WSASend(
                        sendQueue->sock,
                        &sendIOData->dataBuffer,
                        1,
                        NULL,
                        0,
                        &sendIOData->overLapped,
                        NULL
                        );
            
            if (dwError == SOCKET_ERROR)
            {
                //
                //Make sure we are in a I/O pending state.
                //
                dwError = WSAGetLastError();
                
                if (dwError != WSA_IO_PENDING)
                {
                    DBGWRITE(DbgLevelInfo, L"[DSI]WSASend() returned %d instead of WSA_IO_PENDING\n", dwError);
                    DBGWRITE(DbgLevelInfo, L"[DSI]sendIOData->dataBuffer.len = %d\n", sendIOData->dataBuffer.len);
                    
                    //
                    //We need to decrement the refcount for this send request.
                    //
                    InterlockedExchange(&sendQueue->pSession->refCount, 0);

                    DSICleanupSession(sendQueue->pSession);

                    DBG_FREE(sendQueue->sendIOData.buffer);
                    DBG_FREE(sendQueue);
                }
            }
            
            sendQueue = nextQueue;
        }
    }
    
    gWorkerThreadRefCount--;
    
    DBGWRITE(DbgLevelInfo, L"[DSI]Send thread gone!\n");
    
    return(0);
}


//---------------------------------------------------------------------
// DSIGetNewSendQueueElement()
//---------------------------------------------------------------------
// Creates a SEND_QUEUE element that can be added to the send queue
// for transmision.
//

BOOL DSIGetNewSendQueueElement(
    IN	LPSESSION		pSession,
    OUT	LPSEND_QUEUE	*ppSendQueue
)
{
    LPSEND_QUEUE	sendQueue = NULL;
    
    //
    //Create the memory structure that will hold the send data.
    //
    sendQueue = (LPSEND_QUEUE)DBG_ALLOC(sizeof(SEND_QUEUE));
    if (sendQueue == NULL)
    {
        ASSERT(0);
        return(FALSE);
    }
    
    sendQueue->pSession	= pSession;
    sendQueue->sock		= pSession->sock;
    sendQueue->next		= NULL;
    
    //
    //Zero out all values in the I/O data structure.
    //
    RtlZeroMemory(&sendQueue->sendIOData, sizeof(PER_IO_DATA));
    
    //
    //Lastly, create the buffer that will hold the actual data to send.
    //
    sendQueue->sendIOData.buffer 		= (PCHAR)DBG_ALLOC(SEND_BUF_SIZE);
    sendQueue->sendIOData.bufferSize	= SEND_BUF_SIZE;
    
    if (sendQueue->sendIOData.buffer == NULL)
    {
        ASSERT(0);
        return(FALSE);
    }

    *ppSendQueue = sendQueue;
    
    return(TRUE);
}


//---------------------------------------------------------------------
// DSIPostToSendQueue()
//---------------------------------------------------------------------
// Put a SEND_QUEUE structure in the queue of data to be sent.
//

BOOL DSIPostToSendQueue(
    IN	LPSEND_QUEUE	pSendQueue
)
{
    LPSEND_QUEUE	sendQueue;
        
    EnterCriticalSection(&gcsSendQueue);
        
    if (gSendQueue == NULL)
    {
        gSendQueue = pSendQueue;
    }
    else
    {
        sendQueue = gSendQueue;
        
        while(sendQueue->next)
        {
            sendQueue = (LPSEND_QUEUE)sendQueue->next;
        }
        
        sendQueue->next = pSendQueue;
    }
    
    //
    //Increment the refcount so we know we have to wait on this send
    //to complete before freeing the session.
    //
    InterlockedIncrement(&pSendQueue->pSession->refCount);

    //
    //Set the last tickle sent to the client for this session.
    //
    pSendQueue->pSession->dwLastTickleSent = GetTickCount();

    //
    //Now that we've added the element to the queue, we need to 
    //release the sending thread.
    //
    SetEvent(gSendEvent);
    
    LeaveCriticalSection(&gcsSendQueue);

    return(TRUE);
}


//---------------------------------------------------------------------
// DSIProcessRecvBytes()
//---------------------------------------------------------------------
// Process the recieved bytes after the I/O completion.
//
// Returns: TRUE if we are expecting more bytes for this AFP request.

BOOL DSIProcessRecvBytes(
    IN LPSESSION 		pSession,
    IN DWORD			dwBytesRead,
    IN LPPER_IO_DATA	perIOData
)
{
    DWORD			dwDataLen;
    DWORD			dwBytesExpected;
    DWORD			dwCurrentBytes;

    //
    //Update the perf counters with the number of bytes read
    //
    AFPUpdateBytesPerSecond(&dwBytesRead, NULL);
        
    //
    //The total number of bytes read for this request are the number
    //of bytes read for this I/O completion + our current offset into
    //the recv buffer.
    //
    dwCurrentBytes = (dwBytesRead + perIOData->offset);
    
    //
    //Check and see if we have the DSI header already in our buffer.
    //
    if (!(pSession->dwState & sessHasDSIHeader) && (dwCurrentBytes >= DSI_HEADER_SIZE))
    {
        //
        //Set the state saying we have the DSI header.
        //
        pSession->dwState |= sessHasDSIHeader;
    }
    else if (!(pSession->dwState & sessHasDSIHeader))
    {
        //
        //This else is for the special (and rare??) case of
        //when the data received is smaller than the DSI header.
        //
        perIOData->offset = dwBytesRead;
        return(TRUE);
    }
                
    //
    //This is the size of the data following the DSI header.
    //
    dwDataLen = ntohl(*(DWORD *)&perIOData->buffer[DSI_OFFSET_DATALEN]);
    
    //
    //Now calculate how many bytes total we expect for this request.
    //
    dwBytesExpected = (DSI_HEADER_SIZE + dwDataLen);
    
    if (dwBytesExpected > perIOData->bufferSize)
    {
        //
        //The read buffer we have is too small for the incoming request,
        //reallocate it to a large enough size.
        //
        PCHAR	pTemp = NULL;
        
        pTemp = (PCHAR)DSIRealloc(
                            perIOData->buffer,
                            dwBytesExpected
                            );
        
        if (pTemp == NULL)
        {
            DBGWRITE(DbgLevelInfo, L"[DSI]Reallocation of read buffer failed! (%d)\n", GetLastError());
            ASSERT(0);
                        
            DSICleanupSession(pSession);
            return(FALSE);
        }
        
        perIOData->buffer 		= pTemp;
        perIOData->bufferSize	= dwBytesExpected;
        
        //
        //Flag that we have increased the buffer size so we can
        //reduce it to normal later.
        //
        pSession->dwState |= sessMemReAlloced;
    }
    
    //
    //The total number of bytes read for this request are the number
    //of bytes read for this I/O completion + our current offset into
    //the recv buffer.
    //
    if (dwCurrentBytes < dwBytesExpected)
    {
        //
        //We didn't get the entire request, save our place
        //and go get the rest of it.
        //
        perIOData->offset += dwBytesRead;
        return(TRUE);
    }
    
    if (dwCurrentBytes > dwBytesExpected)
    {
        //
        //We've read more bytes in than expected, this means we've
        //possibly read a partial request for the next request. Save
        //it and finish processing this request.
        //
        
        //
        //We better not still have overflow stuff in the buffer!
        //
        ASSERT(perIOData->overflowSize == 0);
        ASSERT(perIOData->overflow == NULL);
                
        perIOData->overflowSize = dwCurrentBytes-dwBytesExpected;
        perIOData->overflow		= (PCHAR)DBG_ALLOC(perIOData->overflowSize);
                
        RtlCopyMemory(
                perIOData->overflow,
                &perIOData->buffer[dwBytesExpected],
                perIOData->overflowSize
        );
    }
    
    if (dwCurrentBytes >= dwBytesExpected)
    {
        //
        //We're done receiving this request, reset all tracking
        //info for the next recv.
        //
        pSession->dwState &= ~sessHasDSIHeader;
        perIOData->offset = 0;
        
        //
        //Now we can process the actual DSI request and
        //associated AFP payload.
        //
        DSIProcessRequest(pSession, perIOData);

        return(FALSE);
    }
    
    return(FALSE);
}


//---------------------------------------------------------------------
// DSIProcessRequest()
//---------------------------------------------------------------------
// We have all the request data we need in our recv buffer now, so
// parse the request.

void DSIProcessRequest(
    IN LPSESSION		pSession,
    IN LPPER_IO_DATA	recvIOData
)
{
    LPSEND_QUEUE		sendQueue = NULL;
    WORD				wPassedReqID;
    BYTE				bDSICommand;
    DWORD				lError = 0;
    
    pSession->dwLastTickleRecvd	= GetTickCount();
    
    if ((BYTE)recvIOData->buffer[DSI_OFFSET_FLAGS] == DSI_REQUEST_FLAG)
    {
        //
        //We check the request ID sent by the client to make sure
        //it is the one we are expecting and that we are not out
        //of synch.
        //
        if (pSession->wClientRequestID >= DSI_MAX_REQUEST_ID)
        {
            pSession->wClientRequestID = 0;
        }
        
        wPassedReqID = ntohs(*(WORD UNALIGNED*)&recvIOData->buffer[DSI_OFFSET_REQUESTID]);
        
        //
        //If the session is new, start out where the client does.
        //
        if (pSession->wClientRequestID == 0)
        {
            pSession->wClientRequestID = wPassedReqID;
        }
        else 
        {
            pSession->wClientRequestID++;
        }
        
        if (pSession->wClientRequestID != wPassedReqID)
        {
            //
            //The request ID we got from the client is not the one
            //we are expecting. We're out of synch, get outta here!
            //
            DBGWRITE(DbgLevelInfo, L"[DSI]Request ID mismatch! (expected: %d, got: %d)\n",
                            pSession->wClientRequestID, wPassedReqID);
            
            DSICleanupSession(pSession);
            return;
        }

        DBGWRITE(DbgLevelAll, L"[DSI]Processing request ID: %d\n", wPassedReqID);
        
        bDSICommand = (BYTE)recvIOData->buffer[DSI_OFFSET_COMMAND];
        
        //
        //For all commands other than tickles, we need to reply, so create
        //a new send queue buffer. If we fail, we drop the request.
        //
        if (bDSICommand != DSI_CMD_Tickle)
        {
            if (!DSIGetNewSendQueueElement(pSession, &sendQueue))
            {
                ASSERT(0);
                return;
            }
        }

        //
        //Before we process any requests, make sure that DSIOpenSession has been
        //called and the session is open.
        //
        if (	((bDSICommand != DSI_CMD_OpenSession) && (bDSICommand != DSI_CMD_GetStatus)) && 
                ((pSession->dwState & sessOpen) == 0)	)
        {
            //
            //No open session, let the user (hacker) know he's in error.
            //
            if (bDSICommand != DSI_CMD_Tickle)
            {
                DSIFormatAndSendReply(sendQueue, NULL, bDSICommand, afpParmErr);
            }

            return;
        }

        //
        //If the client was sleeping we signal now that it is awake.
        //
        if (pSession->dwState & sessClientSleeping)
        {
            pSession->dwState &= ~sessClientSleeping;
        }
        
        //
        //What we have is a AFP request from the client.
        //
        switch(bDSICommand)
        {
            case DSI_CMD_OpenSession:
                DBGWRITE(DbgLevelAll, L"[DSI]DSIOpenSession request received...\n");
                DSIOpenSession(recvIOData, sendQueue);
                break;
            
            case DSI_CMD_CloseSession:
                DBGWRITE(DbgLevelAll, L"[DSI]DSICloseSession request received...\n");

                //
                //Set the closed flag, the scavenger thread will clean
                //up this session.
                //
                pSession->dwState &= ~sessOpen;
                DSIFormatAndSendReply(sendQueue, NULL, bDSICommand, NO_ERROR);
                break;
            
            case DSI_CMD_Tickle:
                //DBGWRITE(DbgLevelAll, L"[DSI]DSITickle request received...\n");
                break;
            
            case DSI_CMD_Command:
                //
                //Call on the AFP handler to dispatch to the correct
                //AFP API call.
                //
                lError = FPDispatchCommand(recvIOData, sendQueue);
                
                //
                //Format our DSI reply packet and send prepare to send it.
                //
                DSIFormatAndSendReply(sendQueue, NULL, bDSICommand, lError);
                break;
            
            case DSI_CMD_GetStatus:
                DBGWRITE(DbgLevelAll, L"[DSI]DSIGetStatus request received...\n");
                
                //
                //Call the AFP function that packs the server data.
                //
                recvIOData->buffer[DSI_OFFSET_DATASTART] = afpGetSInfo;
                lError = FPDispatchCommand(recvIOData, sendQueue);
                                
                //
                //Per the spec, we immediately tear down the connection
                //after this call. The client usually beats us to it.
                //
                pSession->dwState |= sessClosing;

                //
                //We have to decrement the ref counter here since we're flagging the
                //session termination from here.
                //
                InterlockedDecrement(&pSession->refCount);

                //
                //Use the standard function to format the DSI reply
                //
                DSIFormatAndSendReply(sendQueue, NULL, bDSICommand, lError);
                break;
            
            case DSI_CMD_Write:
                //
                //Since writing is a "performance" operation, we skip the overhead
                //of the standard AFPCmd searching and make the calls directly.
                //
                switch((BYTE)recvIOData->buffer[DSI_OFFSET_DATASTART])
                {
                    case afpWrite:
                    case afpWriteExt:
                        lError = FPDispatchCommand(recvIOData, sendQueue);
                        break;
                    case afpAddIcon:
                        lError = FPDispatchCommand(recvIOData, sendQueue);
                        break;
                    
                    default:
                        ASSERT(0);
                        break;
                }
                
                //
                //Format our DSI reply packet and send prepare to send it.
                //
                DSIFormatAndSendReply(sendQueue, NULL, bDSICommand, lError);
                break;

            default:
                DBGWRITE(DbgLevelWarning, L"[DSI]Unknown DSI request! (%d)\n", recvIOData->buffer[DSI_OFFSET_COMMAND]);
                ASSERT(0);
                
                DBG_FREE(sendQueue);
                DSICleanupSession(pSession);
                break;
        }

        //
        //Update the performance counter that tracks AFP requests processed.
        //

        AFPUpdateAFPRequestsProcessed();

    }
    else if (recvIOData->buffer[DSI_OFFSET_FLAGS] == DSI_REPLY_FLAG)
    {
        //
        //What we have is a reply from a request we sent to the client.
        //
    }
    else
    {
        //
        //Big time problems here, something really bad has happened if
        //the request is neither a reply or a request.
        //
        ASSERT(0);
        
        DSICleanupSession(pSession);
    }
}


//---------------------------------------------------------------------
// DSIFormatAndSendReply()
//---------------------------------------------------------------------
// Puts together a reply packet to send back to the client.

void DSIFormatAndSendReply(
    IN LPSEND_QUEUE	pSendQueue,
    IN PVOID		pReplyData,
    IN BYTE			bDSICommand,
    IN LONG			lError
)
{
    LPPER_IO_DATA	perIOData 	= &pSendQueue->sendIOData;
    LPSESSION		pSession	= pSendQueue->pSession;
    
    //
    //Bad news if the session is null, this means it got
    //cleared by another thread!
    //
    ASSERT(pSendQueue != NULL);
    ASSERT(pSession != NULL);
        
    //
    //We are replying, so set the reply flag in the header.
    //
    perIOData->buffer[DSI_OFFSET_FLAGS]		= DSI_REPLY_FLAG;
    perIOData->buffer[DSI_OFFSET_COMMAND] 	= bDSICommand;
    
    //
    //Set the afp data length and error fields in the header.
    //
    WORD2WORD(&perIOData->buffer[DSI_OFFSET_REQUESTID], htons(pSession->wClientRequestID));
    DWORD2DWORD(&perIOData->buffer[DSI_OFFSET_DATALEN], htonl(perIOData->ioSize));
    DWORD2DWORD(&perIOData->buffer[DSI_OFFSET_ERRORCODE], htonl(lError));
        
    //
    //If there is payload data for the DSI reply, copy it into
    //the buffer so it can be sent.
    //
    if ((perIOData->ioSize > 0) && (pReplyData != NULL))
    {
        RtlCopyMemory(
                &perIOData->buffer[DSI_OFFSET_DATASTART],
                pReplyData,
                perIOData->ioSize
        );
    }
    
    //
    //Save the size of the entire reply for our WSASend(). This means
    //we have to add on the size of the DSI_HEADER to the buffer size.
    //
    perIOData->ioSize += DSI_HEADER_SIZE;

    //
    //Now, put this send on the queue for the send worker thread
    //to send out.
    //
    DSIPostToSendQueue(pSendQueue);
}


//---------------------------------------------------------------------
// DSIFormatAndSendRequest()
//---------------------------------------------------------------------
// Format a packet for sending to the client. This function does all
// the dirty work of setting up the DSI Header.
//
// RETURNS: The total size of the newly formatted buffer.
//

BOOL DSIFormatAndSendRequest(
        IN	LPSEND_QUEUE		pSendQueue,
        IN 	DWORD				dwDataLen,
        IN 	PVOID				pData,
        IN	BYTE				bCommand
)
{
    LPPER_IO_DATA	sendIOData	= &pSendQueue->sendIOData;
    LPSESSION		pSession	= pSendQueue->pSession;
    
    //
    //Set the proper request ID for our sequence.
    //
    WORD2WORD(&sendIOData->buffer[DSI_OFFSET_REQUESTID], htons(pSession->wServerRequestID));

    //
    //We are sending, so set the request flag in the header.
    //
    sendIOData->buffer[DSI_OFFSET_FLAGS]				= DSI_REQUEST_FLAG;
    sendIOData->buffer[DSI_OFFSET_COMMAND]				= bCommand;
    
    //
    //The total size of the DSI packet is the DSI Header size plus
    //the size of DSI payload (AFP data).
    //
    sendIOData->ioSize = dwDataLen + DSI_HEADER_SIZE;
    
    //
    //Set the afp data length and error fields in the header.
    //
    DWORD2DWORD(&sendIOData->buffer[DSI_OFFSET_DATALEN], htonl(dwDataLen));
    DWORD2DWORD(&sendIOData->buffer[DSI_OFFSET_ERRORCODE], 0);
        
    //
    //If there is payload data for the DSI request, copy it into
    //the buffer so it can be sent.
    //
    if ((dwDataLen > 0) && (pData != NULL))
    {		
        RtlCopyMemory(
                &sendIOData->buffer[DSI_OFFSET_DATASTART],
                pData,
                dwDataLen
        );
    }
    
    //
    //Update the request ID to be used the next time we send something.
    //
    if (pSession->wServerRequestID == DSI_MAX_REQUEST_ID)
    {
        pSession->wServerRequestID = 0;
    }
    else
    {
        pSession->wServerRequestID++;
    }
    
    //
    //Now, put this send on the queue for the send worker thread
    //to send out.
    //
    DSIPostToSendQueue(pSendQueue);
    
    return(TRUE);
}


//---------------------------------------------------------------------
// DSICleanupSession()
//---------------------------------------------------------------------
// Clean up a single session. Note that it kills the session no matter
//what, with no checks!!

void DSICleanupSession(
    IN LPSESSION	pSession
)
{	
    DBGWRITE(DbgLevelAll, L"[DSI]Cleaning up session\n");
    ASSERT(pSession != NULL);
    ASSERT(pSession->refCount <= 0);

    if (pSession != NULL)
    {
        EnterCriticalSection(&pSession->cs);
        
        if (pSession->sock != INVALID_SOCKET)
        {
            closesocket(pSession->sock);
        }
        
        DSIRemoveSessionFromList(pSession);
        
        DBG_FREE(pSession->pRecvIOData->buffer);
        DBG_FREE(pSession->pRecvIOData);
        
        if (pSession->hUserToken)
        {
            CloseHandle(pSession->hUserToken);
        }

        if (pSession->afpSessionObject)
        {
            delete (AFPSession*)pSession->afpSessionObject;
        }

        LeaveCriticalSection(&pSession->cs);
        DeleteCriticalSection(&pSession->cs);
        
        DBG_FREE(pSession);
    }
}


//---------------------------------------------------------------------
// DSICleanupAll()
//---------------------------------------------------------------------
// Clean up all sessions/connections.

void DSICleanupAll(void)
{
    LPSESSION	pSession 	= gSessionsList;
    LPSESSION	pNext		= pSession;
    
    while(pNext)
    {
        pNext = (LPSESSION)pSession->next;
        pSession->dwState |= sessClosing;
    }
}


//---------------------------------------------------------------------
// DSIFindSessionBySocket()
//---------------------------------------------------------------------
// Finds a session ptr by socket.

LPSESSION DSIFindSessionBySocket(
    IN SOCKET sock
)
{
    LPSESSION pSession = NULL;
    
    EnterCriticalSection(&gcsSessionList);
    
    pSession = gSessionsList;
    
    while(pSession)
    {
        EnterCriticalSection(&pSession->cs);

        if (pSession->sock == sock)
        {
            LeaveCriticalSection(&pSession->cs);
            break;
        }

        LeaveCriticalSection(&pSession->cs);
        
        pSession = (LPSESSION)pSession->next;
    }
    
    LeaveCriticalSection(&gcsSessionList);
    
    return(pSession);
}


//---------------------------------------------------------------------
// DSIAddSessionToList()
//---------------------------------------------------------------------
// Add a session to the timer linked list.

void DSIAddSessionToList(
    IN LPSESSION	pSession
)
{
    LPSESSION	pList;
    
    EnterCriticalSection(&gcsSessionList);
    
    if (gSessionsList == NULL)
    {
        gSessionsList = pSession;
    }
    else
    {
        pList = gSessionsList;
        
        while(pList->next)
            pList = (LPSESSION)pList->next;
        
        pList->next = pSession;
    }

    LeaveCriticalSection(&gcsSessionList);
}


//---------------------------------------------------------------------
// DSIRemoveSessionFromList()
//---------------------------------------------------------------------
// Remove a session from the timer linked list.

void DSIRemoveSessionFromList(
        IN LPSESSION 	pSession
)
{
    LPSESSION	pList;
    LPSESSION	pPrev;
    
    EnterCriticalSection(&gcsSessionList);
    
    ASSERT(gSessionsList != NULL);
    ASSERT(pSession != NULL);
    
    if (gSessionsList == NULL)
    {
        LeaveCriticalSection(&gcsSessionList);
        return;
    }
    
    pList = gSessionsList;
    pPrev = NULL;
    
    while((pList != pSession) && (pList))
    {
        pPrev	= pList;
        pList 	= (LPSESSION)pList->next;
    }
    
    //
    //We have to make sure we found the pointer
    //in our list.
    //
    if (pList)
    {
        if (pPrev != NULL)
        {
            pPrev->next = pList->next;
        }
        else
        {
            //
            //If pPrev is NULL, then the link at the head of the
            //list is being removed.
            //
            gSessionsList = (LPSESSION)pList->next;
        }
    }
    
    LeaveCriticalSection(&gcsSessionList);
}


//---------------------------------------------------------------------
// DSICountSessions()
//---------------------------------------------------------------------
// Remove a session from the timer linked list.

DWORD DSICountSessions(
    IN BOOL		fCountAuthenticatedOnly
)
{
    LPSESSION	pList	= NULL;
    DWORD		dwCount = 0;
    
    EnterCriticalSection(&gcsSessionList);

    //
    //NOTE: this must be assigned here in case the session is freed
    //while we're blocked on the critical section above.
    //
    pList = gSessionsList;
        
    while(pList)
    {
        if (fCountAuthenticatedOnly)
        {
            if (pList->dwState & sessAuthenticated)
            {
                dwCount++;
            }
        }
        else
        {
            dwCount++;
        }

        pList = (LPSESSION)pList->next;
    }

    LeaveCriticalSection(&gcsSessionList);

    return(dwCount);
}


//---------------------------------------------------------------------
// DSIOpenSession()
//---------------------------------------------------------------------
// Handles a DSIOpenSession call.

DWORD DSIOpenSession(
    IN LPPER_IO_DATA 	recvIOData,
    IN LPSEND_QUEUE		pSendQueue
)
{
    BYTE			bOption;
    BYTE			bSize;
    PCHAR			pBuffer;
    LPPER_IO_DATA	sendIOData	= &pSendQueue->sendIOData;
    LPSESSION		pSession 	= pSendQueue->pSession;
    
    bOption 	= recvIOData->buffer[DSI_OFFSET_DATASTART];
    bSize		= recvIOData->buffer[DSI_OFFSET_DATASTART+sizeof(BYTE)];
    
    //
    //We expect these options to have a size of DWORD
    //
    if (bSize == sizeof(DWORD))
    {
        switch(bOption)
        {
            case ATTN_QUANTUM:
                pSession->dwAttentionSize = ntohl(*(DWORD UNALIGNED*)&recvIOData->buffer[DSI_OFFSET_DATASTART+sizeof(WORD)]);
                DBGWRITE(DbgLevelAll, L"[DSI]Attention quantum size requested = %d\n", pSession->dwAttentionSize);
                break;
            
            default:
                ASSERT(0);
                break;
        }
    }
    
    pBuffer = &sendIOData->buffer[DSI_OFFSET_DATASTART];
    
    *pBuffer++ = SRVR_REQUEST_QUANTUM;
    *pBuffer++ = sizeof(DWORD);
    
    PUSH_DWRD_NBO(SRVR_REQUEST_QUANTUM_SIZE, pBuffer);

    pSession->dwState |= sessOpen;

    pSendQueue->sendIOData.ioSize = sizeof(DWORD)+sizeof(WORD);
            
    DSIFormatAndSendReply(pSendQueue, NULL, DSI_CMD_OpenSession, NO_ERROR);
    
    return(NO_ERROR);
}


//---------------------------------------------------------------------
// DSIRealloc()
//---------------------------------------------------------------------
// Windows realloc sucks! Here's my own inefficient one. :-(
//

PVOID DSIRealloc(
    IN	PCHAR	pOldBuffer,
    IN	DWORD	dwNewSize
)
{
    PCHAR	pNewBuffer = NULL;
    
    pNewBuffer = (PCHAR)HeapReAlloc(
                            GetProcessHeap(),
                            HEAP_ZERO_MEMORY,
                            pOldBuffer,
                            dwNewSize
                            );

    if (pNewBuffer != NULL)
    {
        return( pNewBuffer );
    }

    return( NULL );
}




