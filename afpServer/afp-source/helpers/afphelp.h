//===================================================================
// afphelp.h
//===================================================================

#pragma once

#include "afp.h"

constexpr auto days_from_1980_2000 = 7305L; // (20*365 + 5);
constexpr auto seconds_per_day = 86400L; // 24*60*60;
constexpr auto seconds_from_1980_2000 = seconds_per_day * days_from_1980_2000;
constexpr auto num_100_ns_per_second = 10000000L;

#define REPLACEMENT_CHAR		((CHAR)'¦')
#define REPLACEMENT_WCHAR		((WCHAR)'¦')

using int32 = LONG;
using uint32 = DWORD;
using int16 = SHORT;
using uint16 = WORD;
using int8 = char;
using uint8 = BYTE;
using uchar = BYTE;
using unichar = WCHAR;
using int64 = LONGLONG;
using uint64 = ULONGLONG;
using status_t = HRESULT;

#define EXTRACT_BYTE(p, n)	{ \
			(n) = *(p); \
			(p) += sizeof(BYTE); \
}
#define EXTRACT_WORD(p, n)	{ \
			(n) = ntohs(*(WORD UNALIGNED*)(p));	\
			(p) += sizeof(WORD); \
}
#define EXTRACT_LONG(p, n)	{ \
			(n) = ntohl(*(DWORD UNALIGNED*)(p)); \
			(p) += sizeof(DWORD); \
}

//
//Pushes a WORD into a buffer and advances the pointer.
//
#define PUSH_WORD_NBO(w,p) { \
			(*(WORD*)(p)) = htons(w); \
			(p) += sizeof(WORD); \
}

//
//Pushes a DWORD into a buffer and advances the pointer.
//
#define PUSH_DWRD_NBO(w,p) { \
			(*(DWORD*)(p)) = htonl(w); \
			(p) += sizeof(DWORD); \
}

//
//Pushes a BYTE into a buffer and advances the pointer.
//
#define PUSH_BYTE(b, p) { \
			*(p) = b; \
			(p) += sizeof(BYTE); \
}

//
//Pushes a ULONGLONG into a buffer and advances the pointer
//
#define PUSH_UINT64_NBO(i, p) { \
			(*(ULONGLONG*)(p)) = _byteswap_uint64(i); \
			(p) += sizeof(ULONGLONG); \
}

#define EXTRACT_STRING(p,s,c) { \
			CHAR	_temp[MAX_AFP_STRING_LEN]; \
			RtlCopyMemory(_temp, p+sizeof(BYTE), (BYTE)*(p));	\
			_temp[(BYTE)*(p)] = '\0'; \
			(p) += (sizeof(BYTE) + (BYTE)*(p)); \
			AFPMacToWindowsPath(_temp, s, (c)); \
}
#define EXTRACT_STRINGEX(p,s,cchs,c) { \
			if (AFPExtractStringAndAdvanceBuffPtr(&(p),s,(cchs),(c)) != NO_ERROR) { \
				return(afpParmErr); \
			} \
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

/*
 * Convenience macros
 */
#define SAFE_CLOSE_HANDLE(h) if ((h) != nullptr)  CloseHandle(h)

enum
{
	afp_user_name_type = 0,
	afp_group_name_type
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
