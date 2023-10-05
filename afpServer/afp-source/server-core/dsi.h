//===================================================================
// dsi.h
//===================================================================

#pragma once

#include <winsock2.h>
#include "afpdebug.h"

//
//AFP over TCP/IP port number
//
#define AFP_TCP_PORT			548

//
//Offsets for the DSI header fields
//
#define DSI_OFFSET_FLAGS		0
#define DSI_OFFSET_COMMAND		1
#define DSI_OFFSET_REQUESTID	2
#define DSI_OFFSET_ERRORCODE	4
#define DSI_OFFSET_DATAOFFSET	4
#define DSI_OFFSET_DATALEN		8
#define DSI_OFFSET_RESERVED		12
#define DSI_OFFSET_DATASTART	16
#define DSI_HEADER_SIZE			16

//
//This is the largest request that the server can receive
//from the client. It does not include the DSI header size.
//
#define SRVR_REQUEST_QUANTUM_SIZE	(MAXSHORT-DSI_HEADER_SIZE)

//
//DSIOpen option fields
//
#define SRVR_REQUEST_QUANTUM	0x00
#define ATTN_QUANTUM			0x01

//
//DSI commands
//
enum
{
	DSI_CMD_CloseSession	= 1,
	DSI_CMD_Command,
	DSI_CMD_GetStatus,
	DSI_CMD_OpenSession,
	DSI_CMD_Tickle,
	DSI_CMD_Write,
	DSI_CMD_Attention 		= 8
};

//
//Session states.
//
enum
{
	sessOpen				= 0x01,
	sessHasDSIHeader		= 0x02,
	sessClosing				= 0x04,
	sessKillSession			= 0x08,
	sessMemReAlloced		= 0x10,
	sessProcessingRequest	= 0x20,
	sessAuthenticated		= 0x40,
	sessClientSleeping		= 0x80
};

//DSI flag possibilities
#define DSI_REQUEST_FLAG	0x00
#define DSI_REPLY_FLAG		0x01

//Maximum request ID
#define DSI_MAX_REQUEST_ID	65535

typedef struct
{
	SOCKET	sock;
}PER_SOCK_DATA, *LPPER_SOCK_DATA;

#define AFP_TICKLE_TIME				(1000*15)
#define AFP_CLIENT_LOST				(1000*120)
#define AFP_CLIENT_LOST_SLEEPING	(1000*60*60*24)
#define RECV_POSTED					1
#define SEND_POSTED					2
#define ARBI_POSTED					3
#define POST_SEND					1
#define POST_RECV					2
#define NO_ACTION					0
#define SEND_BUF_SIZE				MAXWORD
#define OVER_BUF_SIZE				4096
#define	RECV_BUF_SIZE				MAXWORD
#define DATA_BUF_SIZE				(RECV_BUF_SIZE+DSI_HEADER_SIZE)
#define SAVANGE_THREAD_SLEEP		5000 //scavenger scavenges every 5 seconds

typedef struct
{
	OVERLAPPED			overLapped;
	WSABUF				dataBuffer;
	PCHAR				buffer;
	DWORD				bufferSize;
	PCHAR				overflow;
	DWORD				overflowSize;
	DWORD				offset;
	BYTE				operationType;
	DWORD				ioSize;
}PER_IO_DATA, *LPPER_IO_DATA;

typedef struct _Session
{
	SOCKET				sock;
	CRITICAL_SECTION	cs;
	DWORD				dwState;
	DWORD				dwLastTickleRecvd;
	DWORD				dwLastTickleSent;
	WORD				wClientRequestID;
	WORD				wServerRequestID;
	LONG volatile       refCount;
	DWORD				dwAttentionSize;
	BYTE				bAFPVersion;
	HANDLE				hUserToken;
	PVOID				next;

	//
	//Save the user and domain name so we can find the SID for the
	//user. We also need this for event messages.
	//
	WCHAR				userName[64];
	WCHAR				domainName[64];
	
	//
	//Use extLoginInfo as a place to hold blob storage for temporary
	//storage of information between the FPLogin() and FPLoginCont()
	//AFP API calls. The data stored will be UAM specific.
	//
	PVOID				extLoginInfo;

	//
	//The UAM type used for login should be set in FPLogin(). You
	//should not have to modify this field, ever.
	//
	BYTE				uamType;
	
	//
	//This is the I/O buffer that is used for
	//all recv's for this session.
	//
	LPPER_IO_DATA		pRecvIOData;
	
	//
	//Users of DSI can use this pointer to
	//point to any data they want.
	//
	PVOID				afpSessionObject;
}SESSION, *LPSESSION;

typedef struct
{
	PER_IO_DATA			sendIOData;
	LPSESSION			pSession;
	SOCKET				sock;
	PVOID				next;
}SEND_QUEUE, *LPSEND_QUEUE;

#define DWORD2DWORD(p, d)	*((DWORD UNALIGNED*)p) = d;
#define WORD2WORD(p, w)		*((WORD UNALIGNED*)p) = w;

DWORD dsi_init(void);

void dsi_shutdown(void);

DWORD WINAPI dsi_accept_thread(
	LPVOID	param
);

DWORD WINAPI DSIWorkerThread(
		LPVOID	pParam
);

DWORD WINAPI DSIScavangerThread(
		IN LPVOID	pParam
);

DWORD DSISendTickle(
	IN	LPSESSION	pSession
);

DWORD DSISendAttention(
	IN	LPSESSION	pSession,
	IN	WORD		wMessage
);

DWORD WINAPI DSISendThread(
		IN LPVOID	pParam
);

BOOL DSIGetNewSendQueueElement(
	IN	LPSESSION		pSession,
	OUT	LPSEND_QUEUE	*ppSendQueue
);

BOOL DSIPostToSendQueue(
	IN	LPSEND_QUEUE	pSendQueue
);

BOOL DSIProcessRecvBytes(
	IN LPSESSION 		pSession,
	IN DWORD			dwBytesRead,
	IN LPPER_IO_DATA	perIOData
);

void DSIProcessRequest(
	IN LPSESSION		pSession,
	IN LPPER_IO_DATA	recvIOData
);

void DSIFormatAndSendReply(
	IN LPSEND_QUEUE	pSendQueue,
	IN PVOID		pReplyData,
	IN BYTE			bDSICommand,
	IN LONG			lError
);

BOOL DSIFormatAndSendRequest(
		IN	LPSEND_QUEUE		pSendQueue,
		IN 	DWORD				dwDataLen,
		IN 	PVOID				pData,
		IN	BYTE				bCommand
);

void DSICleanupAll(void);

void DSICleanupSession(
	IN LPSESSION	pSession
);

LPSESSION DSIFindSessionBySocket(
	IN SOCKET sock
);

void DSIAddSessionToList(
	IN LPSESSION	pSession
);

void DSIRemoveSessionFromList(
	IN LPSESSION 	pSession
);

DWORD DSICountSessions(
	IN BOOL			fCountAuthenticatedOnly
);

DWORD DSIOpenSession(
	IN LPPER_IO_DATA 	recvIOData,
	IN LPSEND_QUEUE		pSendQueue
);

PVOID DSIRealloc(
	IN	PCHAR	pOldBuffer,
	IN	DWORD	dwNewSize
);


