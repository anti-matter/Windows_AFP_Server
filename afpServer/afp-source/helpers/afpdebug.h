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

constexpr dbg_level current_debug_level = dbg_level_trace;

#define DBGWRITE(level, message, ...) afp_debug_write(level, __func__, message, __VA_ARGS__)
#define ASSERT(exp) assert(exp)

#define DBG_DUMP_BUFFER(buffer, cb_buffer) hex_dump(buffer, cb_buffer)


void afp_debug_write(_In_ const dbg_level debug_level, _In_ const std::string& function, _In_ const WCHAR* format, ...);

void dump_bitmap(
	IN DWORD bitmap
);

void hex_dump(const char* buffer, const size_t length);

#else

#define DBGWRITE(level, message, ...)
#define ASSERT(exp)
#define DBG_DUMP_BUFFER(buffer, cb_buffer)

#endif

#define ERROR_DLG_TITLE TEXT("AFP Error")
void report_error(CHAR* sz_error, DWORD dw_error);
