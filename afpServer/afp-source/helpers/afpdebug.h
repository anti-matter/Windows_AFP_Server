//============================================================================
// afpdebug.h
// Copyright (c) 1998-2000, Microsoft Corp. All Rights Reserved.
//============================================================================

#pragma once

#include <strsafe.h>
#include <windows.h>
#include <stdio.h>

#if defined(DBG) || defined(_DEBUG)

enum DbgLevel
{
    DbgLevelWarning = 1,
    DbgLevelInfo,
    DbgLevelAll
};

const DbgLevel CurrentDebugLevel = DbgLevelAll;

#define DBG_ALLOC(dwSize)		DBGAllocMem(dwSize, __LINE__, __FILE__)
#define DBG_FREE(ptr)			DBGMemFree((PBYTE)ptr); \
                                ptr = NULL;
            
#define CHECK_DEALLOCATIONS()	DBGCheckDeallocations();

void AFPDebugWrite(_In_ const DbgLevel debugLevel, _In_ const WCHAR* format, ...);
#define DBGWRITE(level, message, ...)      AFPDebugWrite(level, message, __VA_ARGS__);

#define ASSERT(exp) assert(exp)


void DumpBitmap(
        IN DWORD dwBitmap
);

void HexDumpBuffer(
        IN CHAR *pBuffer,
        IN DWORD dwLen
);

void DBGOutputBufferToDebugger(
        IN PBYTE pBuffer,
        IN DWORD dwSize
);

void DBGOutputBufferToDebugger(
        IN PBYTE pBuffer,
        IN DWORD dwSize
);

void DBGCheckDeallocations(void);

PVOID DBGAllocMem(
        IN 	DWORD 	dwSize,
        IN	DWORD	dwLine,
        IN	CHAR*	szFileName
);

void DBGMemFree(
        IN  PBYTE 	pPtr
);

#else

#define DBG_ALLOC(dwSize)		HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwSize)
#define DBG_FREE(ptr)			if (ptr != NULL) {						\
                                    HeapFree(GetProcessHeap(), 0, ptr);	\
                                    ptr = NULL;							\
                                }
            
#define CHECK_DEALLOCATIONS()

#define DEST
#define DBGWRITE(level, message, ...)
#define DBGWRITE_IF_ERR(e, x)
#define ASSERT(exp)
#define CHECK_ASSERT_IF(IF, exp)
#define DBGOUTPUT(b,s)

#endif

#define ERROR_DLG_TITLE TEXT("AFP Error")
void ReportError(CHAR *szError, DWORD dwError);
