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
    case dbg_level_trace:
		level = L"[TRACE]";
		break;
    case dbg_level_dump_in:
		level = L"[DUMP_IN]";
        break;
    case dbg_level_dump_out:
		level = L"[DUMP_OUT]";
        break;
    }

	std::wstring combined;

	if (debug_level != dbg_level_dump_in && debug_level != dbg_level_dump_out)
	{
		combined = level + L"[" + string_to_wide_string(function, kUnicodeNames) + L"]" + format;
	}
	else
	{
		// Dumping buffer.
		combined = level + L" " + format;
	}

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

std::string char_to_hex(const char n)
{
    constexpr char hex_chars[16] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
	std::string result;

	result.push_back(hex_chars[(n & 0xF0) >> 4]);
	result.push_back(hex_chars[(n & 0x0F) >> 0]);

	return result;
}

void hex_dump(const char* buffer, const size_t length, const dbg_level level)
{
	if (level > current_debug_level)
	{
		return;
	}

	std::string line;
	std::string readable;
    DWORD j = 0;

    for (DWORD i = 0; i < length; i++)
	{
	    if (i % 16 == 0 && j > 0)
	    {
			line.append("    ");

			for (DWORD k = 0; k < j; k++)
			{
				line.push_back(std::isprint(readable[k]) ? readable[k] : '.');
			}

			DBGWRITE(level, L"%s\n", string_to_wide_string(line, CP_ACP).c_str());

			line.erase();
			readable.erase();

			j = 0;
	    }

		char ch = buffer[i];
		if (ch < 0)
		{
			ch = 0;
		}

        if (std::string part = char_to_hex(ch); part.length() == 1)
		{
			line.push_back('0');
			line.append(part);
			line.push_back(' ');
		}
		else
		{
			line.append(part);
			line.push_back(' ');
		}

		readable.push_back(ch);
		j++;
	}

	if (j != 0)
	{
	    for (DWORD i = j; i <= 15; i++)
	    {
			line.append("   ");
	    }

		line.append("    ");

		for (DWORD k = 0; k < j; k++)
		{
			line.push_back(std::isprint(readable[k]) ? readable[k] : '.');
		}

		DBGWRITE(level, L"%s\n", string_to_wide_string(line, CP_ACP).c_str());
	}
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
