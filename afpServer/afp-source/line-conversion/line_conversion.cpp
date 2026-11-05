#include "stdafx.h"
#include "line_conversion.h"
#include "afpdebug.h"
#include "afp_registry.h"


line_conversion::line_conversion()
	: m_status_(afperr_registry_no_extensions_for_conversion)
{
	afp_registry afp_reg;
	m_status_ = afp_reg.get_extensions_for_line_conversion(m_line_convert_extensions_);
}

/// <summary>
/// See if we should convert line endings for a given file extension.
/// </summary>
bool line_conversion::should_convert(const std::wstring& extension) const
{
	if (m_status_ == afperr_registry_no_extensions_for_conversion)
	{
		DBGWRITE(dbg_level_info, L"Line conversion not enabled, status: %d\n", m_status_);
		return false;
	}

	for (const auto& ext : m_line_convert_extensions_)  // NOLINT(readability-use-anyofallof)
	{
		if (ext == extension)
		{
			DBGWRITE(dbg_level_info, L"Line conversion enabled for extension: %s\n", ext.c_str());
			return true;
		}
	}

	return false;
}

/// <summary>
/// Simple in-place conversion of line endings in a buffer. Assumes we're converting
/// from CR to LF, and vice versa.
///
/// Windows uses CRLF for line endings, Unix uses LF, and classic Mac OS used CR.
/// </summary>
void line_conversion::convert_buffer(afp_net_buffer& buffer, const DWORD length, const line_ending convert_to) const
{
	if (m_status_ == afperr_registry_no_extensions_for_conversion)
	{
		DBGWRITE(dbg_level_info, L"Line conversion not enabled, status: %d\n", m_status_);
		return;
	}

	convert_buffer(buffer.get_current_pos_ptr(), length, convert_to);
	buffer.set_buffer_size(length);
}

void line_conversion::convert_buffer(char* buffer, const DWORD length, const line_ending convert_to)
{
	const auto p = buffer;
    const auto len = length;

	if (p == nullptr || len == 0)
	{
		DBGWRITE(dbg_level_error, L"Invalid buffer or length for line conversion!\n");
		return;
	}

    for (UINT32 i = 0; i < len; i++)
	{
		if (convert_to == line_conversion_cr)
		{
            if (p[i] == line_conversion_lf)
            {
                p[i] = line_conversion_cr;
            }
        }
		else if (convert_to == line_conversion_lf) // Writing to file, convert CR to LF
		{
			if (p[i] == line_conversion_cr)
			{
				p[i] = line_conversion_lf;
			}
		}
	}
}

void line_conversion::get_converted_size(const std::wstring& file_path, DWORD* file_size, const line_ending convert_to)
{
	const auto length = *file_size;
    const auto buffer = std::make_unique<char[]>(length);

	const auto h_file = CreateFile(
		file_path.c_str(),
		GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr
	);

	DWORD actual_read = 0;

	const auto success = ReadFile(
		h_file,
		buffer.get(),
		length,
		&actual_read,
		nullptr
	);

	if (success)
    {
        convert_buffer(buffer.get(), actual_read, convert_to);
		*file_size = actual_read;
	}

	CloseHandle(h_file);
}


