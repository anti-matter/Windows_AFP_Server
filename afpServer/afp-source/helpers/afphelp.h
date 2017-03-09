//===================================================================
// afphelp.h
//===================================================================

#pragma once

#include "afp.h"

#define	DAYS_FROM_1980_2000			7305L			// (20*365 + 5)
#define	SECONDS_PER_DAY				86400L			// 24*60*60
#define	SECONDS_FROM_1980_2000		SECONDS_PER_DAY * DAYS_FROM_1980_2000
#define	NUM_100ns_PER_SECOND		10000000L

#define REPLACEMENT_CHAR		((CHAR)'¦')
#define REPLACEMENT_WCHAR		((WCHAR)'¦')

typedef LONG int32;
typedef DWORD uint32;
typedef SHORT int16;
typedef WORD uint16;
typedef char int8;
typedef BYTE uint8;
typedef BYTE uchar;
typedef WCHAR unichar;
typedef LONGLONG int64;
typedef ULONGLONG uint64;
typedef HRESULT status_t;

#define EXTRACT_BYTE(p, n)	{									\
								n = *p;							\
								p += sizeof(BYTE);				\
}
#define EXTRACT_WORD(p, n)	{									\
								n = ntohs(*(WORD UNALIGNED*)p);	\
								p += sizeof(WORD);				\
}
#define EXTRACT_LONG(p, n)	{									\
								n = ntohl(*(DWORD UNALIGNED*)p);\
								p += sizeof(DWORD);				\
}

//
//Pushes a WORD into a buffer and advances the pointer.
//
#define PUSH_WORD_NBO(w,p) {					\
			(*(WORD*)p) = htons(w);				\
			p += sizeof(WORD);					\
}

//
//Pushes a DWORD into a buffer and advances the pointer.
//
#define PUSH_DWRD_NBO(w,p) {					\
			(*(DWORD*)p) = htonl(w);			\
			p += sizeof(DWORD);					\
}

//
//Pushes a BYTE into a buffer and advances the pointer.
//
#define PUSH_BYTE(b, p) {						\
			*p = b;								\
			p += sizeof(BYTE);					\
}

//
//Pushes a ULONGLONG into a buffer and advances the pointer
//
#define PUSH_UINT64_NBO(i, p) {						\
			(*(ULONGLONG*)p) = _byteswap_uint64(i);	\
			p += sizeof(ULONGLONG);					\
}

#define EXTRACT_STRING(p,s,c) {													\
								CHAR	_temp[MAX_AFP_STRING_LEN];				\
								RtlCopyMemory(_temp, p+sizeof(BYTE), (BYTE)*p);	\
								_temp[(BYTE)*p] = '\0';							\
								p += (sizeof(BYTE) + (BYTE)*p);					\
								AFPMacToWindowsPath(_temp, s, (c));				\
}
#define EXTRACT_STRINGEX(p,s,cchs,c) {											\
	if (AFPExtractStringAndAdvanceBuffPtr(&p,s,(cchs),(c)) != NO_ERROR) {		\
		return(afpParmErr);														\
	}																			\
}

//
//Pushes a null terminated string into the buffer and
//precedes it with a length byte (pascal style string).
//
#define PUSH_CSTRING(s,p) {											\
			CHAR	_temp[MAX_AFP_STRING_LEN];						\
			RtlZeroMemory(_temp, sizeof(_temp));					\
			AFPWindowsToMacPath(s, sizeof(_temp), NULL, _temp);		\
			*p = strlen(_temp);										\
			p += sizeof(BYTE);										\
			RtlCopyMemory(p, _temp, strlen(_temp));					\
			p += strlen(_temp);										\
}

#define PUSH_ANSI_CSTRING(s,p) {							\
			*p = strlen(s);									\
			p += sizeof(BYTE);								\
			RtlCopyMemory(p, s, strlen(s));					\
			p += strlen(s);									\
}

enum
{
	AFP_USERNAME_TYPE = 0,
	AFP_GROUPNAME_TYPE
};

typedef struct
{
	BYTE type;
	DWORD id;
	WCHAR name[64];
} AFP_USERGROUP_NAME, *PAFP_USERGROUP_NAME;

BOOL AFPGetLogonSID(
	IN HANDLE hToken,
	   OUT PSID* ppSID
);

BOOL IsUserAdmin(
	IN HANDLE token
);

PSID AFPGetSIDforUser(
	IN PWCHAR userName,
	   IN PWCHAR domainName
);

AFPERROR AFPGetDirectoryRights(
	IN HANDLE token, //login token
	   IN PWCHAR pDirWinPath, //directory to check
	   IN PWCHAR pUserName,
	   IN PWCHAR pDomainName,
	   IN DWORD* afpRights
);

AFPERROR AFPGetUserNameByID(
	IN DWORD dwID,
	   IN PWCHAR szName
);

AFPERROR AFPGetDirectoryOwnerAndGroupID(
	IN PWCHAR pDirWinPath,
	   OUT DWORD* dwOwnerID,
	   OUT DWORD* dwGroupID
);

void AFPExtractDomainAndUserName(
	IN PWCHAR pUserName,
	   IN size_t cbUserName,
	   OUT PWCHAR pDomain,
	   IN size_t cbDomain
);

ULONG AFPWindowsToMacTime(
	IN PFILETIME filetime
);

ULONG AFPWindowsToMacLocalTime(
	IN PFILETIME fileTime
);

void AFPMacToWindowsTime(
	IN ULONG dwMacTime,
	   IN PFILETIME filetime
);

void AFPMacToWindowsLocalTime(
	IN ULONG dwMacTime,
	   IN PFILETIME fileTime
);

BOOL OpenCurrentToken(
	IN OUT PHANDLE Token
);

BOOL AFPAdjustPrivilege(
	IN PWCHAR szPrivilege,
	   IN BOOL bEnable,
	   IN HANDLE hToken = INVALID_HANDLE_VALUE
);

BOOL AFPAdjustAccountRight(
	IN HANDLE hUserToken,
	   PWCHAR szPriv,
	   USHORT cbPriv //count in bytes the size of the szPriv buffer
);

BOOL AFPImpersonateUser(
	IN HANDLE hUserToken
);

BOOL AFPRevertToSelf(
	IN HANDLE hUserToken
);

PWCHAR AFPMacToWindowsString(
	IN PCHAR pMacString,
	   IN WORD cbWideString,
	   OUT PWCHAR pWideString
);

PWCHAR AFPMacToWindowsPath(
	IN PCHAR pAFPPath,
	   OUT PWCHAR pWidePath,
	   OUT AFPCHARMAPARRAY pCharMap
);

PCHAR AFPWindowsToMacPath(
	IN PWCHAR pWinPath,
	   IN WORD wAFPBuffSize,
	   IN AFPCHARMAPARRAY pCharMap,
	   OUT PCHAR pAFPPath
);

bool IsWhiteSpace(char c);

void AFPGetObjectNameFromPath(
	IN PWCHAR pPath,
	   OUT PWCHAR pNameBuff,
	   IN size_t cbNameBuff
);

void AFPGetObjectDot3Extension(
	IN PWCHAR pPath,
	   OUT PWCHAR pNameBuff,
	   IN size_t cbNameBuff
);

void AFPGetObjectPath(
	IN PWCHAR pPath,
	   OUT PWCHAR pPathBuff
);

AFPERROR AFPMapWinErrorToAFP(
	IN DWORD dwWinError,
	   IN DWORD dwDefaultError
);

PVOID AFPReAlloc(
	IN PCHAR pOldBuffer,
	   IN DWORD dwOldBufferSize,
	   IN DWORD dwNewSize,
	   IN BOOL fCopyOldToNew
);

HRESULT AFPExtractStringAndAdvanceBuffPtr(
	IN PCHAR* pBuffer,
	   IN PWCHAR pWinString,
	   IN WORD cchWinString,
	   IN BOOL fIsPath = true,
	   OPTIONAL LPAFPCHARMAP pCharMap = nullptr
);
