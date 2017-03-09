//===================================================================
// afpdebug.c
// Copyright (c) 1998-2000, Microsoft Corp. All Rights Reserved.
//
// Michael J. Conrad (mconrad@microsoft.com)
//===================================================================

#include "StdAfx.h"
#include "afpdebug.h"
#include <stdlib.h>


#if defined(DBG) || defined(_DEBUG)

//----------------------------------------------------------------------------
// AFPDebugWrite()
//----------------------------------------------------------------------------
// Debug string cmd for macro.
void AFPDebugWrite(_In_ const DbgLevel debugLevel, _In_ const WCHAR* format, ...)
{
    if (debugLevel <= CurrentDebugLevel)
    {
        va_list args = nullptr;
        va_start(args, format);

		// Len + terminating character
		auto len = _vscwprintf(format, args) + 1;
		auto buffer = std::unique_ptr<WCHAR>(new WCHAR[len]);

		vswprintf_s(buffer.get(), len, format, args);
        va_end(args);

        OutputDebugString(buffer.get());
    }
}

//----------------------------------------------------------------------------
// DumpBitmap()
//----------------------------------------------------------------------------
// Dump a bitmap to the debugger window in readable form.

void DumpBitmap(DWORD dwBitmap)
{
    DWORD	bit = 0x01;
    WORD	x;
    CHAR	buffer[64];
    
    StringCbPrintfA(buffer, sizeof(buffer), "Bitmap dump:\n\n");
    OutputDebugStringA(buffer);
    
    StringCbPrintfA(buffer, sizeof(buffer), "0        10        20        30\n");
    OutputDebugStringA(buffer);
    
    StringCbPrintfA(buffer, sizeof(buffer), "12345678901234567890123456789012\n");
    OutputDebugStringA(buffer);
    
    RtlZeroMemory(buffer, 64);
    
    for(x = 1; x <= 32; x++, bit += bit)
    {
        if (dwBitmap & bit) {
            StringCbCatA(buffer, sizeof(buffer), "^");
        }
        else {
            StringCbCatA(buffer, sizeof(buffer), " ");
        }
    }

    StringCbCatA(buffer, sizeof(buffer), "\n");
    OutputDebugStringA(buffer);
    
    bit = 0x1;
    
    for(x = 1; x <= 32; x++, bit += bit)
    {
        StringCbPrintfA(buffer, sizeof(buffer), "%#-10x = %d\n", bit, ((dwBitmap & bit)?1:0));
        OutputDebugStringA(buffer);
    }
}


//
//This is needed in BloodShed since it sin't in the stdlib
//
bool isprint(UCHAR c)
{
    if (    ((c >= 'a') || (c <= 'z'))      ||
            ((c >= 'A') || (c <= 'Z'))      ||
            ((c >= '0') || (c <= '9'))      )
    {
        return( true );
    }
    
    return( false );
}


//----------------------------------------------------------------------------
//		HexDumpBuffer()
//----------------------------------------------------------------------------
// Dumps a buffer in hex and chars to the output text window.

void HexDumpBuffer(CHAR *pBuffer, DWORD dwLen)
{
    CHAR	szLine[128];
    UCHAR	szLittle[16];
    CHAR	szPart[16];
    DWORD	i,j,k;
    CHAR	foo[2];
    
    szLine[0] = 0;
    
    j = 0;
    
    for (i = 0; i < dwLen; i++)
    {
        if (!(i % 16) && j)
        {
            StringCbCatA(szLine, sizeof(szLine), "   ");
            
            for (k = 0; k < j; k++)
            {
                if (isprint(szLittle[k]))
                {
                    foo[0] = szLittle[k];
                    foo[1] = 0;
                    StringCbCatA(szLine, sizeof(szLine), foo);
                }
                else
                    StringCbCatA(szLine, sizeof(szLine), ".");
            }
            
            StringCbCatA(szLine, sizeof(szLine), "\n");
            OutputDebugStringA(szLine);
            
            szLine[0] = 0;
            j = 0;
        }
        
        StringCbPrintfA(szPart, sizeof(szPart), "%02x ", (UCHAR)(pBuffer[i] & 0xFF));
        StringCbCatA(szLine, sizeof(szLine), szPart);
        szLittle[j++] = (UCHAR)pBuffer[i];
    }
    
    if (j)
    {
        for (i = j; i < 16; i++)
            StringCbCatA(szLine, sizeof(szLine), "   ");
        
        StringCbCatA(szLine, sizeof(szLine), "    ");
        
        for (k = 0; k < j; k++)
        {
            if (isprint(szLittle[k]))
            {
                foo[0] = szLittle[k];
                foo[1] = 0;
                StringCbCatA(szLine, sizeof(szLine), foo);
            }
            else
                StringCbCatA(szLine, sizeof(szLine), ".");
        }
        
        StringCbCatA(szLine, sizeof(szLine), "\n");
        OutputDebugStringA(szLine);
    }
}


//---------------------------------------------------------------------
// DBGOutputBufferToDebugger()
//---------------------------------------------------------------------
// Outputs the entire contents of a buffer to the debuffer.

void DBGOutputBufferToDebugger(
        IN PBYTE pBuffer,
        IN DWORD dwSize
)
{
    DWORD 	i;
    CHAR	c[15];

    for (i = 0; i < dwSize; i++)
    {
        StringCbPrintfA(c, sizeof(c), "%-4x ", pBuffer[i]);		
        if (((i+1) % 8) == 0) {
            StringCbCatA(c, sizeof(c), "\n");
        }
        
        OutputDebugStringA(c);
    }

    OutputDebugStringA("\n");
}


//---------------------------------------------------------------------
// DBGMemFree()
//---------------------------------------------------------------------
// Internal use memory release routine.

void DBGMemFree(
        IN  PBYTE 	pPtr
)
{	
    if (pPtr == NULL)
    {
        DBGWRITE(DbgLevelInfo, L"[DBG]Attempted to free NULL block!\n");
        ASSERT(0);
        
        return;
    }
            
    HeapFree(GetProcessHeap(), 0, pPtr);
}


//---------------------------------------------------------------------
// DBGAllocMem()
//---------------------------------------------------------------------
// Internal use memory allocation routine.

PVOID DBGAllocMem(
        IN 	DWORD 	dwSize,
        IN	DWORD	dwLine,
        IN	CHAR*	szFileName
)
{
    PBYTE	pReturnBuff = NULL;	
    
    pReturnBuff = (PBYTE)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwSize);
    
    if (pReturnBuff == NULL)
    {
        WCHAR	temps[1024];
        size_t	numconv;

        mbstowcs_s(
            &numconv, 
            temps, 
            sizeof(temps), 
            szFileName, 
            strlen(szFileName)
            );

        DBGWRITE(
                DbgLevelInfo,
                L"[DBG]Mem alloc of %d bytes failed in file: %s line: %d error: %d\n",
                temps,
                dwLine,
                dwSize,
                GetLastError()
                );

        return(NULL);
    }

    return((PVOID)pReturnBuff);
}
#endif


//----------------------------------------------------------------------------
//		ReportError()
//----------------------------------------------------------------------------
// Put up a message box to report an error.

void ReportError(CHAR *szError, DWORD dwError)
{
    TCHAR	 szErrorMsg[256];

    StringCbPrintf(szErrorMsg, sizeof(szErrorMsg), TEXT("%s\n\nError Code: %d"), szError, dwError);
    
    MessageBox(
         NULL,
         szErrorMsg,
         ERROR_DLG_TITLE,
         MB_OK | MB_ICONERROR | MB_SETFOREGROUND
         );
}

