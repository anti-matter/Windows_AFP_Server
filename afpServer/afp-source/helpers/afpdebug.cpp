#include "StdAfx.h"
#include "afpdebug.h"

#include <codecvt>
#include <future>

#include "afphelp.h"


#if defined(DBG) || defined(_DEBUG)

//----------------------------------------------------------------------------
// afp_debug_write()
//----------------------------------------------------------------------------
// Debug string cmd for macro.
void afp_debug_write(_In_ const dbg_level debug_level, _In_ const std::string& function, _In_ const WCHAR* format, ...)
{
	if (debug_level > current_debug_level)
	{
		return;
	}

	va_list args;
	va_start(args, format);

	std::wstring level;
    switch (debug_level)
    {
    case dbg_level_error:
		level = L"[ERROR]";
		break;
    case dbg_level_warning:
		level = L"[WARNING]";
		break;
    case dbg_level_info:
		level = L"[INFO]";
		break;
    case dbg_level_all:
		level = L"[ALL]";
		break;
    }

	const std::wstring combined = level + L"[" + string_to_wide_string(function) + L"]" + format;

	// Len + terminating character
	const auto len = _vscwprintf(combined.c_str(), args) + 1;
	const auto buffer = std::unique_ptr<WCHAR>(new WCHAR[len]);

	const auto chars_written = vswprintf_s(buffer.get(), len, combined.c_str(), args);
	if (chars_written > 0)
	{
		OutputDebugString(buffer.get());
	}

	va_end(args);
}

void dump_bitmap(const DWORD bitmap)
{
	DWORD bit = 0x01;
	WORD x;
	CHAR buffer[64];

	// ReSharper disable CppFunctionResultShouldBeUsed

	StringCbPrintfA(buffer, sizeof(buffer), "Bitmap dump:\n\n");
	OutputDebugStringA(buffer);

	StringCbPrintfA(buffer, sizeof(buffer), "0        10        20        30\n");
	OutputDebugStringA(buffer);

	StringCbPrintfA(buffer, sizeof(buffer), "12345678901234567890123456789012\n");
	OutputDebugStringA(buffer);

	RtlZeroMemory(buffer, 64);

	for (x = 1; x <= 32; x++, bit += bit)
	{
		if (bitmap & bit)
		{
			StringCbCatA(buffer, sizeof(buffer), "^");
		}
		else
		{
			StringCbCatA(buffer, sizeof(buffer), " ");
		}
	}

	StringCbCatA(buffer, sizeof(buffer), "\n");
	OutputDebugStringA(buffer);

	bit = 0x1;

	for (x = 1; x <= 32; x++, bit += bit)
	{
		StringCbPrintfA(buffer, sizeof(buffer), "%#-10x = %d\n", bit, ((bitmap & bit) ? 1 : 0));
		OutputDebugStringA(buffer);
	}

	// ReSharper restore CppFunctionResultShouldBeUsed
}


void hex_dump_buffer(const CHAR* p_buffer, const DWORD length)
{
	CHAR line[128];
	CHAR little[16];
	CHAR foo[2];
	DWORD j = 0;
	line[0] = 0;

	// ReSharper disable CppFunctionResultShouldBeUsed

	for (DWORD i = 0; i < length; i++)
	{
		CHAR part[16];

		if (!(i % 16) && j)
		{
			StringCbCatA(line, sizeof(line), "   ");

			for (DWORD k = 0; k < j; k++)
			{
				if (little[k] < -1)
				{
					StringCbCatA(line, sizeof(line), foo);
					continue;
				}
				if (std::isprint(little[k]))
				{
					foo[0] = little[k];
					foo[1] = 0;
					StringCbCatA(line, sizeof(line), foo);
				}
				else
					StringCbCatA(line, sizeof(line), ".");
			}

			StringCbCatA(line, sizeof(line), "\n");
			OutputDebugStringA(line);

			line[0] = 0;
			j = 0;
		}

		StringCbPrintfA(part, sizeof(part), "%02x ", static_cast<UCHAR>(p_buffer[i] & 0xFF));
		StringCbCatA(line, sizeof(line), part);
		little[j++] = p_buffer[i];
	}

	if (j)
	{
		for (DWORD i = j; i < 16; i++)
		{
			StringCbCatA(line, sizeof(line), "   ");
		}

		StringCbCatA(line, sizeof(line), "    ");

		for (DWORD k = 0; k < j; k++)
		{
			if (little[k] >= 0 && std::isprint(little[k]))
			{
				foo[0] = little[k];
				foo[1] = 0;
				StringCbCatA(line, sizeof(line), foo);
			}
			else
            {
                StringCbCatA(line, sizeof(line), ".");
            }
		}

		StringCbCatA(line, sizeof(line), "\n");
		OutputDebugStringA(line);
	}

	// ReSharper restore CppFunctionResultShouldBeUsed
}


void dbg_output_buffer_to_debugger(
	IN const PBYTE p_buffer,
	IN const DWORD size
)
{
	for (DWORD i = 0; i < size; i++)
	{
		CHAR c[15];
		const auto result = StringCbPrintfA(c, sizeof(c), "%-4x ", p_buffer[i]);
		if (SUCCEEDED(result) && (i + 1) % 8 == 0)
		{
			// ReSharper disable once CppFunctionResultShouldBeUsed
			StringCbCatA(c, sizeof(c), "\n");
		}

		OutputDebugStringA(c);
	}

	OutputDebugStringA("\n");
}


void dbg_free(
	IN const PBYTE ptr
)
{
	if (ptr == nullptr)
	{
		DBGWRITE(dbg_level_info, L"Attempted to free NULL block!\n");
		ASSERT(0);

		return;
	}

	HeapFree(GetProcessHeap(), 0, ptr);
}


PVOID dbg_alloc(
	IN const DWORD size,
	IN const DWORD line,
	IN const CHAR* file_name
)
{
	const auto return_buff = static_cast<PBYTE>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size));

	if (return_buff == nullptr)
	{
		WCHAR temps[1024];
		size_t numconv;

		mbstowcs_s( // NOLINT(cert-err33-c)
			&numconv,
			temps,
			sizeof(temps) / 2,
			file_name,
			strlen(file_name)
		);

		DBGWRITE(
			dbg_level_info,
			L"Mem alloc of %d bytes failed in file: %s line: %d error: %d\n",
			temps,
			line,
			size,
			GetLastError()
		);

		return nullptr;
	}

	return return_buff;
}
#endif


void report_error(CHAR* sz_error, const DWORD dw_error)
{
	WCHAR sz_error_msg[256];

	const auto result = StringCbPrintf(sz_error_msg, sizeof(sz_error_msg), TEXT("%s\n\nError Code: %ld"), sz_error,
	                                   dw_error);
	if (FAILED(result))
	{
		return;
	}

	MessageBox(
		nullptr,
		sz_error_msg,
		ERROR_DLG_TITLE,
		MB_OK | MB_ICONERROR | MB_SETFOREGROUND
	);
}
