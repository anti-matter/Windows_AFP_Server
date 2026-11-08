#include "StdAfx.h"
#include "afpdebug.h"

#include "afphelp.h"


#if defined(DBG) || defined(_DEBUG)

//----------------------------------------------------------------------------
// Persistent debug log
//----------------------------------------------------------------------------
// Debug builds always mirror every DBGWRITE to a log file so there is a
// durable, on-disk record of server events (connections, logons, protocol
// errors) without needing WinDbg/DebugView attached. The file is
// afpServer_debug.log next to the executable; if that directory is not
// writable it falls back to %TEMP%. Each line is prefixed with a
// millisecond tick and the thread id so the multi-threaded request flows
// can be interleaved correctly. Writes are serialized on a critical
// section and unbuffered (WriteFile), so the log survives a crash up to
// the last completed call.

namespace
{
	class debug_log final
	{
	public:
		debug_log()
		{
			InitializeCriticalSection(&cs_);

			WCHAR path[MAX_PATH]{};
			if (GetModuleFileNameW(nullptr, path, MAX_PATH) > 0)
			{
				WCHAR* const last_slash = wcsrchr(path, L'\\');
				if (last_slash != nullptr)
				{
					last_slash[1] = L'\0';
					StringCchCatW(path, MAX_PATH, L"afpServer_debug.log");
					handle_ = open_log_file(path);
				}
			}

			if (handle_ == INVALID_HANDLE_VALUE && GetTempPathW(MAX_PATH, path) > 0)
			{
				StringCchCatW(path, MAX_PATH, L"afpServer_debug.log");
				handle_ = open_log_file(path);
			}

			if (handle_ != INVALID_HANDLE_VALUE)
			{
				SYSTEMTIME sys_time{};
				GetLocalTime(&sys_time);
				CHAR banner[160];
				StringCchPrintfA(banner, ARRAYSIZE(banner),
				                 "\r\n==== afpServer debug log started %04u-%02u-%02u %02u:%02u:%02u (tick base %llu) ====\r\n",
				                 sys_time.wYear, sys_time.wMonth, sys_time.wDay,
				                 sys_time.wHour, sys_time.wMinute, sys_time.wSecond,
				                 static_cast<unsigned long long>(GetTickCount64()));
				write_raw(banner, static_cast<DWORD>(strlen(banner)));
			}
		}

		~debug_log()
		{
			if (handle_ != INVALID_HANDLE_VALUE)
			{
				CloseHandle(handle_);
			}
			DeleteCriticalSection(&cs_);
		}

		debug_log(const debug_log&) = delete;
		debug_log& operator=(const debug_log&) = delete;

		void write(const WCHAR* text)
		{
			if (handle_ == INVALID_HANDLE_VALUE)
			{
				return;
			}

			// Convert the wide message to UTF-8.
			const int utf8_len = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
			if (utf8_len <= 1)
			{
				return;
			}
			std::string utf8(static_cast<size_t>(utf8_len) - 1, '\0');
			WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8.data(), utf8_len, nullptr, nullptr);

			CHAR header[64];
			StringCchPrintfA(header, ARRAYSIZE(header), "%10llu T%-6u ",
			                 static_cast<unsigned long long>(GetTickCount64()),
			                 static_cast<unsigned int>(GetCurrentThreadId()));

			EnterCriticalSection(&cs_);
			DWORD written = 0;
			WriteFile(handle_, header, static_cast<DWORD>(strlen(header)), &written, nullptr);
			WriteFile(handle_, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
			LeaveCriticalSection(&cs_);
		}

	private:
		static HANDLE open_log_file(const WCHAR* path)
		{
			return CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
			                   nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		}

		void write_raw(const char* text, const DWORD length)
		{
			EnterCriticalSection(&cs_);
			DWORD written = 0;
			WriteFile(handle_, text, length, &written, nullptr);
			LeaveCriticalSection(&cs_);
		}

		HANDLE handle_ = INVALID_HANDLE_VALUE;
		CRITICAL_SECTION cs_;
	};

	// C++11 guarantees thread-safe one-time initialization of this static.
	debug_log& get_debug_log()
	{
		static debug_log log_file;
		return log_file;
	}
}


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
		level = L"[DUMP__IN]";
        break;
    case dbg_level_dump_out:
		level = L"[DUMP_OUT]";
        break;
    }

	std::wstring combined;

	if (debug_level != dbg_level_dump_in && debug_level != dbg_level_dump_out)
	{
		//__func__ is plain ASCII.
		combined = level + L"[" + string_to_wide_string(function, CP_UTF8) + L"]" + format;
	}
	else
	{
		// Dumping buffer.
		combined = level + L" " + format;
	}

	// Len + terminating character
	const auto len = _vscwprintf(combined.c_str(), args) + 1;
	if (len <= 0)
	{
		//_vscwprintf failed (encoding error in one of the string args).
		va_end(args);
		return;
	}

	//Array form is required: scalar delete of new[] is undefined.
	const auto buffer = std::unique_ptr<WCHAR[]>(new WCHAR[len]);

	const auto chars_written = vswprintf_s(buffer.get(), len, combined.c_str(), args);
	if (chars_written > 0)
	{
		OutputDebugString(buffer.get());

		// Mirror to the persistent debug log file (debug builds: always on).
		std::wstring line(buffer.get());
		while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n'))
		{
			line.pop_back();
		}
		line += L"\r\n";
		get_debug_log().write(line.c_str());
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
				line.push_back(std::isprint(static_cast<unsigned char>(readable[k])) ? readable[k] : '.');
			}

			DBGWRITE(level, L"%s\n", string_to_wide_string(line, CP_ACP).c_str());

			line.erase();
			readable.erase();

			j = 0;
	    }

		//Treat the byte as unsigned: masking here (rather than zeroing
		//negative values) keeps high-bit bytes visible in the dump.
		const unsigned char ch = static_cast<unsigned char>(buffer[i]);

        if (std::string part = char_to_hex(static_cast<char>(ch)); part.length() == 1)
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
			line.push_back(std::isprint(static_cast<unsigned char>(readable[k])) ? readable[k] : '.');
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
