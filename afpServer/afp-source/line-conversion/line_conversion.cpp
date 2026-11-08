#include "stdafx.h"
#include "line_conversion.h"
#include "afpdebug.h"
#include "afp_registry.h"


line_conversion::line_conversion()
	: m_status_(afperr_registry_no_extensions_for_conversion)
{
	afp_registry afp_reg;
	m_status_ = afp_reg.get_extensions_for_line_conversion(m_line_convert_extensions_);

	//Normalize the configured list: trim surrounding whitespace and make
	//sure each entry carries a leading dot, so entries like " .h" or
	//"txt" still match the output of std::filesystem::path::extension().
	if (m_status_ == NO_ERROR)
	{
		for (auto& ext : m_line_convert_extensions_)
		{
			const auto first = ext.find_first_not_of(L" \t");
			if (first == std::wstring::npos)
			{
				ext.clear();
				continue;
			}

			const auto last = ext.find_last_not_of(L" \t");
			ext = ext.substr(first, last - first + 1);

			if (!ext.empty() && ext.front() != L'.')
			{
				ext.insert(ext.begin(), L'.');
			}
		}
	}
}

/// <summary>
/// See if we should convert line endings for a given file extension.
/// The extension is compared case-insensitively: Windows file systems
/// are, so "README.TXT" must match a ".txt" entry.
/// </summary>
bool line_conversion::should_convert(const std::wstring& extension) const
{
	if (m_status_ == afperr_registry_no_extensions_for_conversion)
	{
		return false;
	}

	for (const auto& ext : m_line_convert_extensions_)  // NOLINT(readability-use-anyofallof)
	{
		if (!ext.empty() && _wcsicmp(ext.c_str(), extension.c_str()) == 0)
		{
			DBGWRITE(dbg_level_info, L"Line conversion enabled for extension: %s\n", ext.c_str());
			return true;
		}
	}

	return false;
}

/// <summary>
/// In-place, one-for-one conversion of line endings over the data at the
/// buffer's current position (CR on the wire <-> LF on disk). The byte
/// count does not change, so the buffer wrapper's bounds and position are
/// deliberately left untouched; the caller advances the position.
/// </summary>
void line_conversion::convert_buffer(afp_net_buffer& buffer, const DWORD length, const line_ending convert_to) const
{
	if (m_status_ == afperr_registry_no_extensions_for_conversion)
	{
		return;
	}

	convert_buffer(buffer.get_current_pos_ptr(), length, convert_to);
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
