#pragma once

#include <strsafe.h>
#include <windows.h>

#if defined(DBG) || defined(_DEBUG)

enum dbg_level
{
	dbg_level_error = 1,
	dbg_level_warning,
	dbg_level_info,
	dbg_level_trace
};

constexpr dbg_level current_debug_level = dbg_level_info;

#define DBG_ALLOC(dwSize)		dbg_alloc(dwSize, __LINE__, __FILE__)
#define DBG_FREE(ptr)			dbg_free((PBYTE)(ptr)); \
                                (ptr) = NULL

#define CHECK_DEALLOCATIONS()	DBGCheckDeallocations();

#define DBGWRITE(level, message, ...) afp_debug_write(level, __func__, message, __VA_ARGS__)
#define ASSERT(exp) assert(exp)

#define DBG_DUMP_BUFFER(buffer, cb_buffer) hex_dump_buffer(buffer, cb_buffer)


void afp_debug_write(_In_ const dbg_level debug_level, _In_ const std::string& function, _In_ const WCHAR* format, ...);

void dump_bitmap(
	IN DWORD bitmap
);

void hex_dump_buffer(
	IN const CHAR* p_buffer,
	IN DWORD length
);

void dbg_output_buffer_to_debugger(
	IN PBYTE p_buffer,
	IN DWORD size
);

void dbg_check_deallocations(void);

PVOID dbg_alloc(
	IN DWORD size,
	IN DWORD line,
	IN const CHAR* file_name
);

void dbg_free(
	IN PBYTE ptr
);

#else

#define DBG_ALLOC(dwSize)		HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwSize)
#define DBG_FREE(ptr)			if (ptr != NULL) {						\
                                    HeapFree(GetProcessHeap(), 0, ptr);	\
                                    ptr = NULL;							\
                                }
            
#define CHECK_DEALLOCATIONS()

#define DBGWRITE(level, message, ...)
#define ASSERT(exp)
#define DBG_DUMP_BUFFER(buffer, cb_buffer)

#endif

#define ERROR_DLG_TITLE TEXT("AFP Error")
void report_error(CHAR* sz_error, DWORD dw_error);
