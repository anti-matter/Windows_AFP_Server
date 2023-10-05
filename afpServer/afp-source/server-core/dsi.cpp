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
#include "auto_lock.h"
#include "AFPSession.h"
#include "afp_critical_section.h"
#include "counters.h"
#include "afpEvents.h"


LPSESSION g_sessions_list = nullptr;
HANDLE g_completion_port = nullptr;
SOCKET g_listen_socket = INVALID_SOCKET;
SHORT g_worker_thread_ref_count = 0;
HANDLE g_send_event = nullptr;
LPSEND_QUEUE g_send_queue = nullptr;
WORD g_num_worker_threads = 0;
BOOL g_running = TRUE;
CRITICAL_SECTION g_cs_session_list;
CRITICAL_SECTION g_cs_send_queue;

//---------------------------------------------------------------------
// DSIInit()
//---------------------------------------------------------------------
// Initialize the DSI. This MUST be called before making any DSI calls.

DWORD dsi_init()
{
	WSADATA wsa_data;
	DWORD thread_id;

	g_running = TRUE;

	DWORD error = WSAStartup(MAKEWORD(1, 1), &wsa_data);
	if (error != 0)
	{
		DBGWRITE(dbg_level_info, L"[DSI]WSAStartup() failed! (%d)\n", error);
		return error;
	}

	InitializeCriticalSection(&g_cs_session_list);
	InitializeCriticalSection(&g_cs_send_queue);

	//
	//Create the accepting thread that will accept connection requests.
	//
	HANDLE h_thread = CreateThread(
		nullptr,
		0,
		dsi_accept_thread,
		nullptr,
		0,
		&thread_id
	);

	SAFE_CLOSE_HANDLE(h_thread);

	h_thread = CreateThread(
		nullptr,
		0,
		AFPAttentionThread,
		nullptr,
		0,
		&thread_id
	);

	SAFE_CLOSE_HANDLE(h_thread);

	h_thread = CreateThread(
		nullptr,
		0,
		DSISendThread,
		nullptr,
		0,
		&thread_id
	);

	SAFE_CLOSE_HANDLE(h_thread);

	h_thread = CreateThread(
		nullptr,
		0,
		DSIScavangerThread,
		nullptr,
		0,
		&thread_id
	);

	SAFE_CLOSE_HANDLE(h_thread);

	//
	//Initialize our performance counters for use in perfmon
	//
	error = AFPInitializePerfCounters();
	if (error != ERROR_SUCCESS)
	{
		DBGWRITE(dbg_level_warning, L"[DSI]Initializing perf counters failed!\n");
	}

	return NO_ERROR;
}


//---------------------------------------------------------------------
// DSIShutdown()
//---------------------------------------------------------------------
// We're shutting down, try to clean everything up.

void dsi_shutdown(void)
{
	//
	//The server is shutting down. First thing to do is send
	//an attention message to all connected clients informing
	//them of the closure.
	//
	DSISendAttention(nullptr, ATTN_SERVER_SHUTDOWN);

	//
	//Tell the listening thread to stop accepting connections.
	//
	g_running = FALSE;

	//
	//This will stop all pending I/O on the listening socket
	//and allow the acceptance thread to shutdown.
	//
	closesocket(g_listen_socket);

	//
	//This releases the send thread.
	//
	SetEvent(g_send_event);

	//
	//Give some time for the message to go out.
	//
	Sleep(5000);

	//
	//This tells our worker threads to shutdown.
	//
	for (WORD i = 0; i < g_num_worker_threads; i++)
	{
		PostQueuedCompletionStatus(
			g_completion_port,
			1,
			1,
			nullptr
		);
	}

	AFPShutdownWorkerThread();

	while ((g_worker_thread_ref_count > 0) && (g_sessions_list))
		Sleep(500);
}


//---------------------------------------------------------------------
// dsi_accept_thread()
//---------------------------------------------------------------------
// Thread that accepts incoming connection requests and sets up the
// I/O completion port.

DWORD WINAPI dsi_accept_thread(
	IN const LPVOID param
)
{
	UNREFERENCED_PARAMETER(param);

	DWORD error;
	DWORD receive_bytes = 0;
	DWORD flags = 0;
	DWORD thread_id;
	SYSTEM_INFO system_info;
	SOCKADDR_IN internet_address;

	g_worker_thread_ref_count++;

	//
	//Create the completion port that I/O requests will use.
	//
	const auto comp_port = CreateIoCompletionPort(
		INVALID_HANDLE_VALUE,
		nullptr,
		0,
		0
	);

	if (comp_port == nullptr)
	{
		//
		//We failed to create our port, all execution should stop.
		//

		error = GetLastError();

		DBGWRITE(dbg_level_warning, L"[DSI]Completion port creation failed! (%d)\n", error);

		g_worker_thread_ref_count--;
		return 0;
	}

	g_completion_port = comp_port;

	//
	//Create the worker thread(s) that will service the I/O requests.
	//We create 2 threads for each processor to get the maximum
	//benefit out of the completion port model.
	//
	GetSystemInfo(&system_info);

	g_num_worker_threads = static_cast<WORD>(system_info.dwNumberOfProcessors * 2);

	for (WORD i = 0; i < g_num_worker_threads; i++)
	{
		const auto h_thread = CreateThread(
			nullptr,
			0,
			DSIWorkerThread,
			comp_port,
			0,
			&thread_id
		);

		if (h_thread != nullptr)
		{
			CloseHandle(h_thread);
		}
	}

	//
	//Create the listening socket for AFP connections.
	//
	const auto sock = WSASocket(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);

	if (sock == INVALID_SOCKET)
	{
		//
		//For some reason we couldn't create our socket.
		//
		DBGWRITE(dbg_level_warning, L"[DSI]Failed to create accept socket! (%d)\n", WSAGetLastError());

		g_worker_thread_ref_count--;
		return 0;
	}

	internet_address.sin_family = AF_INET;
	internet_address.sin_addr.s_addr = htonl(INADDR_ANY);
	internet_address.sin_port = htons(AFP_TCP_PORT);

	error = bind(sock, reinterpret_cast<PSOCKADDR>(&internet_address), sizeof(internet_address));

	if (error != ERROR_SUCCESS)
	{
		DBGWRITE(dbg_level_warning, L"[DSI]Failed to bind accept socket! (%d)\n", WSAGetLastError());

		g_worker_thread_ref_count--;

		closesocket(sock);
		return 0;
	}

	//
	//Prepare the socket for listening. We let as many connection
	//requests as possible (or reasonable) in the queue.
	//
	error = listen(sock, SOMAXCONN);

	if (error == SOCKET_ERROR)
	{
		DBGWRITE(dbg_level_warning, L"[DSI]listen() failed! (%d)\n", WSAGetLastError());

		g_worker_thread_ref_count--;

		closesocket(sock);
		return 0;
	}

	DBGWRITE(dbg_level_info, L"[DSI]Ready to accept AFP connections...\n");

	g_listen_socket = sock;

	while (g_running)
	{
		const auto new_sock = WSAAccept(sock, nullptr, nullptr, nullptr, 0);

		if (new_sock == INVALID_SOCKET)
		{
			error = WSAGetLastError();

			if (error == WSAEINTR)
			{
				//
				//This error means that we have closed the socket
				//and are attempting to shut the thread down.
				//
				DBGWRITE(dbg_level_info, L"[DSI]Accept thread shutting down...\n");
				continue;
			}

			//
			//An error ocurred and the acceptance failed. Continue on
			//to the next request.
			//
			DBGWRITE(dbg_level_warning, L"[DSI]Accept socket failed! (%d)\n", error);
			continue;
		}

		DBGWRITE(dbg_level_all, L"[DSI]Socket number %d connected\n", new_sock);

		//
		//Create the session structure that we use to track all
		//the open connections.
		//
		auto session = static_cast<LPSESSION>(DBG_ALLOC(sizeof(SESSION)));
		if (session == nullptr)
		{
			//
			//Fatal error, we couldn't allocate memory for our storage.
			//
			DBGWRITE(dbg_level_warning, L"[DSI]Failed to allocate session struct! (%d)\n", GetLastError());

			closesocket(new_sock);
			continue;
		}

		//
		//We need a critical section for manipulating the session data.
		//
		InitializeCriticalSection(&session->cs);

		//
		//Save the socket information.
		//
		session->sock = new_sock;

		//
		//Create an I/O data struct to handle the first I/O request. This
		//pointer will be retained for all recv's for this session's life.
		//
		LPPER_IO_DATA per_io_data = static_cast<LPPER_IO_DATA>(DBG_ALLOC(sizeof(PER_IO_DATA)));

		//
		//Setup the per-I/O data to be set/saved.
		//
		per_io_data->buffer = static_cast<PCHAR>(DBG_ALLOC(DATA_BUF_SIZE));
		per_io_data->bufferSize = DATA_BUF_SIZE;
		per_io_data->dataBuffer.len = DATA_BUF_SIZE;
		per_io_data->dataBuffer.buf = per_io_data->buffer;
		per_io_data->offset = 0;
		per_io_data->operationType = RECV_POSTED;
		per_io_data->overflow = nullptr;
		per_io_data->overflowSize = 0;

		session->dwState = 0;
		session->pRecvIOData = per_io_data;
		session->dwLastTickleRecvd = GetTickCount();
		session->dwLastTickleSent = GetTickCount();

		DSIAddSessionToList(session);

		//
		//Now associate the new, accepted, socket with our completion port.
		//
		CreateIoCompletionPort(
			(HANDLE)new_sock,
			comp_port,
			(ULONG_PTR)session,
			0
		);

		error = WSARecv(
			new_sock,
			&per_io_data->dataBuffer,
			1,
			&receive_bytes,
			&flags,
			(OVERLAPPED*)per_io_data,
			nullptr
		);

		if (error == SOCKET_ERROR)
		{
			//
			//Make sure we are in a I/O pending state.
			//
			error = WSAGetLastError();

			if (error != WSA_IO_PENDING)
			{
				DBGWRITE(dbg_level_warning, L"[DSI]WSARecv() returned %d instead of WSA_IO_PENDING\n", error);
				ASSERT(0);
			}
			else
			{
				InterlockedIncrement(&session->refCount);
			}
		}
	} //end while

	DSICleanupAll();

	g_worker_thread_ref_count--;

	DBGWRITE(dbg_level_info, L"[DSI]Accept thread gone!\n");
	return (NO_ERROR);
}


//---------------------------------------------------------------------
// DSIWorkerThread()
//---------------------------------------------------------------------
// Thread that accepts incoming connection requests and sets up the
// I/O completion port.
//

DWORD WINAPI DSIWorkerThread(
	IN LPVOID pParam
)
{
	auto compPort = pParam;
	LPSESSION pSession = nullptr;
	LPPER_IO_DATA perIOData = nullptr;
	DWORD dwBytesXferd = 0;
	DWORD dwFlags = 0;
	DWORD dwError = 0;
	WORD wNextAction = 0;

	g_worker_thread_ref_count++;

	//
	//Adjust the privilege of this thread so we can accept
	//logons over the network.
	//
	if (!afp_adjust_privilege(SE_TCB_NAME, TRUE))
	{
		DBGWRITE(dbg_level_info, L"[DSI]AdjustPrivilege() in worker failed enabling SE_TCB_NAME!\n");
	}

	if (!afp_adjust_privilege(SE_SECURITY_NAME, TRUE))
	{
		DBGWRITE(dbg_level_info, L"[DSI]AdjustPrivilege() in worker failed enabling SE_SECURITY_NAME!\n");
	}

	while (g_running)
	{
		GetQueuedCompletionStatus(
			compPort,
			&dwBytesXferd,
			(LPDWORD)&pSession,
			(LPOVERLAPPED*)&perIOData,
			INFINITE
		);

		if ((perIOData == nullptr) || (g_running == FALSE))
		{
			DBGWRITE(dbg_level_info, L"[DSI]Shutting down worker thread...\n");
			ASSERT(g_running == FALSE);

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
			auto_lock lock(&pSession->cs);

			if ((dwBytesXferd == 0) &&
				((perIOData->operationType == RECV_POSTED) ||
					(perIOData->operationType == SEND_POSTED)))
			{
				//
				//Zero byts transferred indicates socket has been closed
				//by the client. We free our handles and close the socket.
				//
				DBGWRITE(dbg_level_info, L"[DSI]Closing connection by client request (%d)\n", pSession->refCount);

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
		switch (perIOData->operationType)
		{
		case RECV_POSTED:
			perIOData->ioSize = dwBytesXferd;

			if (DSIProcessRecvBytes(pSession, dwBytesXferd, perIOData) == FALSE)
			{
				auto_lock lock(&pSession->cs);

				//DBGWRITE(DbgLevelAll, L"[DSI]Request processed for session 0x%x\n", pSession));

				//
				//We're done processing the request, the reply should already
				//be on it's way to the client. Now check for any data in
				//the overflow buffer.
				//
				if (perIOData->overflowSize > 0)
				{
					BOOL succeeded = FALSE;

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

					perIOData->overflow = nullptr;
					perIOData->overflowSize = 0;
					perIOData->offset = 0;
					perIOData->operationType = RECV_POSTED;

					ZeroMemory(&perIOData->overLapped, sizeof(OVERLAPPED));

					InterlockedIncrement(&pSession->refCount);

					//
					//Now process the new data just like above.
					//
					succeeded = PostQueuedCompletionStatus(
						g_completion_port,
						dwBytesXferd,
						(ULONG_PTR)pSession,
						(LPOVERLAPPED)perIOData
					);

					if (!succeeded)
					{
						DBGWRITE(dbg_level_info, L"[DSI]PostQueuedCompletionStatus() failed! (%d)\n", GetLastError());
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
			AFPUpdateBytesPerSecond(nullptr, &dwBytesXferd);

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
			DBGWRITE(dbg_level_info, L"[DSI]Bad operation type! (%d)\n", perIOData->operationType);
			ASSERT(0);
			break;
		}

		//
		//The server was told to shutdown while we were in the middle of a
		//network operation. Close down gracefully.
		//
		if (!g_running)
		{
			//
			//Zero byts transferred indicates socket has been closed
			//by the client. We free our handles and close the socket.
			//
			DBGWRITE(dbg_level_info, L"[DSI]Closing connection due to shutdown (%d)\n", pSession->refCount);

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
		switch (wNextAction)
		{
		case POST_SEND:
			ASSERT(0);
			break;

		case POST_RECV:
			//
			//Setup the next overlapped call.
			//
			ZeroMemory(&perIOData->overLapped, sizeof(OVERLAPPED));

			perIOData->dataBuffer.len = perIOData->bufferSize - perIOData->offset;
			perIOData->dataBuffer.buf = &perIOData->buffer[perIOData->offset];
			perIOData->operationType = RECV_POSTED;

		//
		//Reset flags as they may have been set on last output
		//
			dwFlags = 0;

			dwError = WSARecv(
				pSession->sock,
				&perIOData->dataBuffer,
				1,
				nullptr,
				&dwFlags,
				&perIOData->overLapped,
				nullptr
			);

			if (dwError == SOCKET_ERROR)
			{
				//
				//Make sure we are in a I/O pending state.
				//
				dwError = WSAGetLastError();

				if (dwError != WSA_IO_PENDING)
				{
					DBGWRITE(dbg_level_info, L"[DSI]WSARecv() returned %d instead of WSA_IO_PENDING\n", dwError);
				}
				else
				{
					InterlockedIncrement(&pSession->refCount);
				}
			}
			else
				InterlockedIncrement(&pSession->refCount);

			DBGWRITE(dbg_level_all, L"[DSI]Receive posted for session 0x%x\n", pSession);
			break;

		case NO_ACTION:
			break;

		default:
			ASSERT(0);
			break;
		}
	}

	g_worker_thread_ref_count--;

	return (0);
}


//---------------------------------------------------------------------
// DSITerminateSession()
//---------------------------------------------------------------------
// Terminate the DSI session, release all memory and remove it from
// the session list.

void DSITerminateSession(
	IN LPSESSION pSession, //pointer to session to terminate
	IN BOOL fTimeout //set if session terminated due to timeout
)
{
	DBGWRITE(dbg_level_info, L"[DSI]Closing session per scavenger request\n");

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

		DBGWRITE(dbg_level_info, L"[DSI]Session timed out, setting ref count to zero and closing...\n");

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
	IN LPSESSION pSession
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
				dbg_level_info,
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

			return (false);
		}

		//
		//Call the routine that does the dirty work of terminating the session.
		//

		DSITerminateSession(pSession, false);

		return (true);
	}

	return (false);
}


//---------------------------------------------------------------------
// DSICheckIfSessionTimedOut()
//---------------------------------------------------------------------
// Check to see if a session has timed out and needs closure. If so, 
// call the DSITerminate() function.
//
// RETURNS: TRUE if session was closed.

bool DSICheckIfSessionTimedOut(
	IN LPSESSION pSession
)
{
	bool fCloseSession = true;
	DWORD dwTicks = GetTickCount();

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

			if ((dwTicks - pSession->dwLastTickleRecvd) < AFP_CLIENT_LOST_SLEEPING)
			{
				fCloseSession = false;
			}
		}

		if (fCloseSession == true)
		{
			DBGWRITE(
				dbg_level_info,
				(L"[DSI]Session timed out and is being terminated\n")
			);

			LeaveCriticalSection(&pSession->cs);

			//
			//Call the routine that does the dirty work of terminating the session.
			//

			DSITerminateSession(pSession, true);

			return (true);
		}
	}

	LeaveCriticalSection(&pSession->cs);

	return (false);
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
	DWORD dwTicks = GetTickCount();

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
	IN LPVOID pParam
)
{
	UNREFERENCED_PARAMETER(pParam);

	LPSESSION pSession;
	bool fStatus;

	g_worker_thread_ref_count++;

	while ((g_running) || (g_sessions_list))
	{
		EnterCriticalSection(&g_cs_session_list);

		pSession = g_sessions_list;

		while (pSession)
		{
			//
			//Since we may kill the current pointer, get the pointer to the
			//next session in the list now.
			//
			auto pNext = static_cast<LPSESSION>(pSession->next);

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

		LeaveCriticalSection(&g_cs_session_list);

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
		if (g_running)
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

			Sleep(100);
		}
	}

	g_worker_thread_ref_count--;

	DBGWRITE(dbg_level_info, L"[DSI]Scavanger thread terminated!\n");

	return (0);
}


//---------------------------------------------------------------------
// DSISendTickle()
//---------------------------------------------------------------------
// Send a tickle packet to the client.
//

DWORD DSISendTickle(
	IN LPSESSION pSession
)
{
	LPSEND_QUEUE pSendQueue;

	if (!DSIGetNewSendQueueElement(pSession, &pSendQueue))
	{
		//
		//We failed to allocate a new send queue struct.
		//
		return (GetLastError());
	}

	//
	//Send the volume update attention message to 
	//the client.
	//
	DSIFormatAndSendRequest(
		pSendQueue,
		0,
		nullptr,
		DSI_CMD_Tickle
	);

	//DBGWRITE(DbgLevelInfo, L"[DSI]Tickle sent\n");
	return (NO_ERROR);
}


//---------------------------------------------------------------------
// DSISendAttention()
//---------------------------------------------------------------------
// Send an attention message to a client. If the input session is NULL,
// this routine will send the message to all connected clients.
//

DWORD DSISendAttention(
	IN LPSESSION pSession,
	IN WORD wMessage
)
{
	LPSEND_QUEUE pSendQueue = nullptr;
	WORD wAttention = htons(wMessage);

	if (pSession != nullptr)
	{
		if (!DSIGetNewSendQueueElement(pSession, &pSendQueue))
		{
			//
			//We failed to allocate a new send queue struct.
			//
			return (GetLastError());
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
		LPSESSION pList = g_sessions_list;

		EnterCriticalSection(&g_cs_session_list);

		while (pList)
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
					LeaveCriticalSection(&g_cs_session_list);
					return (GetLastError());
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

				DBGWRITE(dbg_level_info, L"[DSI]Attention message sent (0x%x)\n", wMessage);
			}

			pList = static_cast<LPSESSION>(pList->next);
		}

		LeaveCriticalSection(&g_cs_session_list);
	}

	return (NO_ERROR);
}


//---------------------------------------------------------------------
// DSISendThread()
//---------------------------------------------------------------------
// Thread which gets packets to send off of a queue and sends them
// in an orderly fashion.
//

DWORD WINAPI DSISendThread(
	IN LPVOID pParam
)
{
	UNREFERENCED_PARAMETER(pParam);

	LPPER_IO_DATA sendIOData;
	LPSEND_QUEUE sendQueue;
	LPSEND_QUEUE nextQueue;
	DWORD dwError;

	g_worker_thread_ref_count++;

	g_send_event = CreateEvent(nullptr, TRUE, TRUE, nullptr);
	ASSERT(g_send_event != nullptr);

	ResetEvent(g_send_event);

	while ((g_running) || (g_send_queue))
	{
		//
		//The event will be signaled when there is data queued up to be sent.
		//
		WaitForSingleObject(g_send_event, INFINITE);

		//
		//Check to see if we're shutting down or not. If so, then we close
		//our event handle and break out.
		//
		if ((!g_running) && (g_send_queue == nullptr))
		{
			CloseHandle(g_send_event);
			break;
		}

		EnterCriticalSection(&g_cs_send_queue);

		sendQueue = g_send_queue;
		g_send_queue = nullptr;

		ResetEvent(g_send_event);

		LeaveCriticalSection(&g_cs_send_queue);

		while (sendQueue != nullptr)
		{
			sendIOData = &sendQueue->sendIOData;

			//
			//We need to get the next queue now because the current queue
			//could be freed by the worker thread before we complete the
			//send below!
			//
			nextQueue = static_cast<LPSEND_QUEUE>(sendQueue->next);

			//
			//Send out the data in our storage buffer for this I/O
			//operation.
			//
			RtlZeroMemory(&sendIOData->overLapped, sizeof(OVERLAPPED));

			sendIOData->dataBuffer.len = sendIOData->ioSize - sendIOData->offset;
			sendIOData->dataBuffer.buf = sendIOData->buffer + sendIOData->offset;
			sendIOData->operationType = SEND_POSTED;

			dwError = WSASend(
				sendQueue->sock,
				&sendIOData->dataBuffer,
				1,
				nullptr,
				0,
				&sendIOData->overLapped,
				nullptr
			);

			if (dwError == SOCKET_ERROR)
			{
				//
				//Make sure we are in a I/O pending state.
				//
				dwError = WSAGetLastError();

				if (dwError != WSA_IO_PENDING)
				{
					DBGWRITE(dbg_level_info, L"[DSI]WSASend() returned %d instead of WSA_IO_PENDING\n", dwError);
					DBGWRITE(dbg_level_info, L"[DSI]sendIOData->dataBuffer.len = %d\n", sendIOData->dataBuffer.len);

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

	g_worker_thread_ref_count--;

	DBGWRITE(dbg_level_info, L"[DSI]Send thread gone!\n");

	return (0);
}


//---------------------------------------------------------------------
// DSIGetNewSendQueueElement()
//---------------------------------------------------------------------
// Creates a SEND_QUEUE element that can be added to the send queue
// for transmision.
//

BOOL DSIGetNewSendQueueElement(
	IN LPSESSION pSession,
	OUT LPSEND_QUEUE* ppSendQueue
)
{
	LPSEND_QUEUE sendQueue = nullptr;

	//
	//Create the memory structure that will hold the send data.
	//
	sendQueue = static_cast<LPSEND_QUEUE>(DBG_ALLOC(sizeof(SEND_QUEUE)));
	if (sendQueue == nullptr)
	{
		ASSERT(0);
		return (FALSE);
	}

	sendQueue->pSession = pSession;
	sendQueue->sock = pSession->sock;
	sendQueue->next = nullptr;

	//
	//Zero out all values in the I/O data structure.
	//
	RtlZeroMemory(&sendQueue->sendIOData, sizeof(PER_IO_DATA));

	//
	//Lastly, create the buffer that will hold the actual data to send.
	//
	sendQueue->sendIOData.buffer = static_cast<PCHAR>(DBG_ALLOC(SEND_BUF_SIZE));
	sendQueue->sendIOData.bufferSize = SEND_BUF_SIZE;

	if (sendQueue->sendIOData.buffer == nullptr)
	{
		ASSERT(0);
		return (FALSE);
	}

	*ppSendQueue = sendQueue;

	return (TRUE);
}


//---------------------------------------------------------------------
// DSIPostToSendQueue()
//---------------------------------------------------------------------
// Put a SEND_QUEUE structure in the queue of data to be sent.
//

BOOL DSIPostToSendQueue(
	IN LPSEND_QUEUE pSendQueue
)
{
	LPSEND_QUEUE sendQueue;

	EnterCriticalSection(&g_cs_send_queue);

	if (g_send_queue == nullptr)
	{
		g_send_queue = pSendQueue;
	}
	else
	{
		sendQueue = g_send_queue;

		while (sendQueue->next)
		{
			sendQueue = static_cast<LPSEND_QUEUE>(sendQueue->next);
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
	SetEvent(g_send_event);

	LeaveCriticalSection(&g_cs_send_queue);

	return (TRUE);
}


//---------------------------------------------------------------------
// DSIProcessRecvBytes()
//---------------------------------------------------------------------
// Process the recieved bytes after the I/O completion.
//
// Returns: TRUE if we are expecting more bytes for this AFP request.

BOOL DSIProcessRecvBytes(
	IN LPSESSION pSession,
	IN DWORD dwBytesRead,
	IN LPPER_IO_DATA perIOData
)
{
	DWORD dwDataLen;
	DWORD dwBytesExpected;
	DWORD dwCurrentBytes;

	//
	//Update the perf counters with the number of bytes read
	//
	AFPUpdateBytesPerSecond(&dwBytesRead, nullptr);

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
		return (TRUE);
	}

	//
	//This is the size of the data following the DSI header.
	//
	dwDataLen = ntohl(*(DWORD*)&perIOData->buffer[DSI_OFFSET_DATALEN]);

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
		PCHAR pTemp = nullptr;

		pTemp = static_cast<PCHAR>(DSIRealloc(
			perIOData->buffer,
			dwBytesExpected
		));

		if (pTemp == nullptr)
		{
			DBGWRITE(dbg_level_info, L"[DSI]Reallocation of read buffer failed! (%d)\n", GetLastError());
			ASSERT(0);

			DSICleanupSession(pSession);
			return (FALSE);
		}

		perIOData->buffer = pTemp;
		perIOData->bufferSize = dwBytesExpected;

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
		return (TRUE);
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

		perIOData->overflowSize = dwCurrentBytes - dwBytesExpected;
		perIOData->overflow = static_cast<PCHAR>(DBG_ALLOC(perIOData->overflowSize));

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

		return (FALSE);
	}

	return (FALSE);
}


//---------------------------------------------------------------------
// DSIProcessRequest()
//---------------------------------------------------------------------
// We have all the request data we need in our recv buffer now, so
// parse the request.

void DSIProcessRequest(
	IN LPSESSION pSession,
	IN LPPER_IO_DATA recvIOData
)
{
	LPSEND_QUEUE sendQueue = nullptr;
	WORD wPassedReqID;
	BYTE bDSICommand;
	DWORD lError = 0;

	pSession->dwLastTickleRecvd = GetTickCount();

	if (static_cast<BYTE>(recvIOData->buffer[DSI_OFFSET_FLAGS]) == DSI_REQUEST_FLAG)
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
			DBGWRITE(dbg_level_info, L"[DSI]Request ID mismatch! (expected: %d, got: %d)\n",
			         pSession->wClientRequestID, wPassedReqID);

			DSICleanupSession(pSession);
			return;
		}

		DBGWRITE(dbg_level_all, L"[DSI]Processing request ID: %d\n", wPassedReqID);

		bDSICommand = static_cast<BYTE>(recvIOData->buffer[DSI_OFFSET_COMMAND]);

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
		if (((bDSICommand != DSI_CMD_OpenSession) && (bDSICommand != DSI_CMD_GetStatus)) &&
			((pSession->dwState & sessOpen) == 0))
		{
			//
			//No open session, let the user (hacker) know he's in error.
			//
			if (bDSICommand != DSI_CMD_Tickle)
			{
				DSIFormatAndSendReply(sendQueue, nullptr, bDSICommand, afpParmErr);
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
		switch (bDSICommand)
		{
		case DSI_CMD_OpenSession:
			DBGWRITE(dbg_level_all, L"[DSI]DSIOpenSession request received...\n");
			DSIOpenSession(recvIOData, sendQueue);
			break;

		case DSI_CMD_CloseSession:
			DBGWRITE(dbg_level_all, L"[DSI]DSICloseSession request received...\n");

		//
		//Set the closed flag, the scavenger thread will clean
		//up this session.
		//
			pSession->dwState &= ~sessOpen;
			DSIFormatAndSendReply(sendQueue, nullptr, bDSICommand, NO_ERROR);
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
			DSIFormatAndSendReply(sendQueue, nullptr, bDSICommand, lError);
			break;

		case DSI_CMD_GetStatus:
			DBGWRITE(dbg_level_all, L"[DSI]DSIGetStatus request received...\n");

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
			DSIFormatAndSendReply(sendQueue, nullptr, bDSICommand, lError);
			break;

		case DSI_CMD_Write:
			//
			//Since writing is a "performance" operation, we skip the overhead
			//of the standard AFPCmd searching and make the calls directly.
			//
			switch (static_cast<BYTE>(recvIOData->buffer[DSI_OFFSET_DATASTART]))
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
			DSIFormatAndSendReply(sendQueue, nullptr, bDSICommand, lError);
			break;

		default:
			DBGWRITE(dbg_level_warning, L"[DSI]Unknown DSI request! (%d)\n", recvIOData->buffer[DSI_OFFSET_COMMAND]);
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
	IN LPSEND_QUEUE pSendQueue,
	IN PVOID pReplyData,
	IN BYTE bDSICommand,
	IN LONG lError
)
{
	LPPER_IO_DATA perIOData = &pSendQueue->sendIOData;
	LPSESSION pSession = pSendQueue->pSession;

	//
	//Bad news if the session is null, this means it got
	//cleared by another thread!
	//
	ASSERT(pSendQueue != NULL);
	ASSERT(pSession != NULL);

	//
	//We are replying, so set the reply flag in the header.
	//
	perIOData->buffer[DSI_OFFSET_FLAGS] = DSI_REPLY_FLAG;
	perIOData->buffer[DSI_OFFSET_COMMAND] = bDSICommand;

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
	if ((perIOData->ioSize > 0) && (pReplyData != nullptr))
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
	IN LPSEND_QUEUE pSendQueue,
	IN DWORD dwDataLen,
	IN PVOID pData,
	IN BYTE bCommand
)
{
	LPPER_IO_DATA sendIOData = &pSendQueue->sendIOData;
	LPSESSION pSession = pSendQueue->pSession;

	//
	//Set the proper request ID for our sequence.
	//
	WORD2WORD(&sendIOData->buffer[DSI_OFFSET_REQUESTID], htons(pSession->wServerRequestID));

	//
	//We are sending, so set the request flag in the header.
	//
	sendIOData->buffer[DSI_OFFSET_FLAGS] = DSI_REQUEST_FLAG;
	sendIOData->buffer[DSI_OFFSET_COMMAND] = bCommand;

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
	if ((dwDataLen > 0) && (pData != nullptr))
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

	return (TRUE);
}


//---------------------------------------------------------------------
// DSICleanupSession()
//---------------------------------------------------------------------
// Clean up a single session. Note that it kills the session no matter
//what, with no checks!!

void DSICleanupSession(
	IN LPSESSION pSession
)
{
	DBGWRITE(dbg_level_all, L"[DSI]Cleaning up session\n");
	ASSERT(pSession != NULL);
	ASSERT(pSession->refCount <= 0);

	if (pSession != nullptr)
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
			delete static_cast<AFPSession*>(pSession->afpSessionObject);
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
	LPSESSION pSession = g_sessions_list;
	LPSESSION pNext = pSession;

	while (pNext)
	{
		pNext = static_cast<LPSESSION>(pSession->next);
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
	LPSESSION pSession = nullptr;

	EnterCriticalSection(&g_cs_session_list);

	pSession = g_sessions_list;

	while (pSession)
	{
		EnterCriticalSection(&pSession->cs);

		if (pSession->sock == sock)
		{
			LeaveCriticalSection(&pSession->cs);
			break;
		}

		LeaveCriticalSection(&pSession->cs);

		pSession = static_cast<LPSESSION>(pSession->next);
	}

	LeaveCriticalSection(&g_cs_session_list);

	return (pSession);
}


//---------------------------------------------------------------------
// DSIAddSessionToList()
//---------------------------------------------------------------------
// Add a session to the timer linked list.

void DSIAddSessionToList(
	IN LPSESSION pSession
)
{
	LPSESSION pList;

	EnterCriticalSection(&g_cs_session_list);

	if (g_sessions_list == nullptr)
	{
		g_sessions_list = pSession;
	}
	else
	{
		pList = g_sessions_list;

		while (pList->next)
			pList = static_cast<LPSESSION>(pList->next);

		pList->next = pSession;
	}

	LeaveCriticalSection(&g_cs_session_list);
}


//---------------------------------------------------------------------
// DSIRemoveSessionFromList()
//---------------------------------------------------------------------
// Remove a session from the timer linked list.

void DSIRemoveSessionFromList(
	IN LPSESSION pSession
)
{
	LPSESSION pList;
	LPSESSION pPrev;

	EnterCriticalSection(&g_cs_session_list);

	ASSERT(g_sessions_list != NULL);
	ASSERT(pSession != NULL);

	if (g_sessions_list == nullptr)
	{
		LeaveCriticalSection(&g_cs_session_list);
		return;
	}

	pList = g_sessions_list;
	pPrev = nullptr;

	while ((pList != pSession) && (pList))
	{
		pPrev = pList;
		pList = static_cast<LPSESSION>(pList->next);
	}

	//
	//We have to make sure we found the pointer
	//in our list.
	//
	if (pList)
	{
		if (pPrev != nullptr)
		{
			pPrev->next = pList->next;
		}
		else
		{
			//
			//If pPrev is NULL, then the link at the head of the
			//list is being removed.
			//
			g_sessions_list = static_cast<LPSESSION>(pList->next);
		}
	}

	LeaveCriticalSection(&g_cs_session_list);
}


//---------------------------------------------------------------------
// DSICountSessions()
//---------------------------------------------------------------------
// Remove a session from the timer linked list.

DWORD DSICountSessions(
	IN BOOL fCountAuthenticatedOnly
)
{
	LPSESSION pList = nullptr;
	DWORD dwCount = 0;

	EnterCriticalSection(&g_cs_session_list);

	//
	//NOTE: this must be assigned here in case the session is freed
	//while we're blocked on the critical section above.
	//
	pList = g_sessions_list;

	while (pList)
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

		pList = static_cast<LPSESSION>(pList->next);
	}

	LeaveCriticalSection(&g_cs_session_list);

	return (dwCount);
}


//---------------------------------------------------------------------
// DSIOpenSession()
//---------------------------------------------------------------------
// Handles a DSIOpenSession call.

DWORD DSIOpenSession(
	IN LPPER_IO_DATA recvIOData,
	IN LPSEND_QUEUE pSendQueue
)
{
	BYTE bOption;
	BYTE bSize;
	PCHAR pBuffer;
	LPPER_IO_DATA sendIOData = &pSendQueue->sendIOData;
	LPSESSION pSession = pSendQueue->pSession;

	bOption = recvIOData->buffer[DSI_OFFSET_DATASTART];
	bSize = recvIOData->buffer[DSI_OFFSET_DATASTART + sizeof(BYTE)];

	//
	//We expect these options to have a size of DWORD
	//
	if (bSize == sizeof(DWORD))
	{
		switch (bOption)
		{
		case ATTN_QUANTUM:
			pSession->dwAttentionSize = ntohl(
				*(DWORD UNALIGNED*)&recvIOData->buffer[DSI_OFFSET_DATASTART + sizeof(WORD)]);
			DBGWRITE(dbg_level_all, L"[DSI]Attention quantum size requested = %d\n", pSession->dwAttentionSize);
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

	pSendQueue->sendIOData.ioSize = sizeof(DWORD) + sizeof(WORD);

	DSIFormatAndSendReply(pSendQueue, nullptr, DSI_CMD_OpenSession, NO_ERROR);

	return (NO_ERROR);
}


//---------------------------------------------------------------------
// DSIRealloc()
//---------------------------------------------------------------------
// Windows realloc sucks! Here's my own inefficient one. :-(
//

PVOID DSIRealloc(
	IN PCHAR pOldBuffer,
	IN DWORD dwNewSize
)
{
	PCHAR pNewBuffer = nullptr;

	pNewBuffer = static_cast<PCHAR>(HeapReAlloc(
		GetProcessHeap(),
		HEAP_ZERO_MEMORY,
		pOldBuffer,
		dwNewSize
	));

	if (pNewBuffer != nullptr)
	{
		return (pNewBuffer);
	}

	return (nullptr);
}
