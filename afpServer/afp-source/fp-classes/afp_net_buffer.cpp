#include "StdAfx.h"
#include <cstring>
#include "afp_net_buffer.h"

afp_net_buffer::afp_net_buffer(_In_ char* afp_buffer, _In_ const size_t cb_buffer)
	: mBuffer(afp_buffer)
	  , mCurrentPos(afp_buffer)
	  , mBufferSize(cb_buffer)
	  , mDSIReturnValue(0)
	  , mDSIDataOffset(0)
{
}

/*
 * get_pascal_string()
 *
 * Description:
 *		Extracts a pascal style or unicode string from the afp buffer and
 *		returns it as a c-style string (null terminated). This function also
 *		advances the current buffer pointer to the end of the string
 *		and the next item in the buffer.
 *
 * Returns: None
 */
AFPERROR afp_net_buffer::get_pascal_string(
	unichar* string,
	const UINT16 cb_string,
	const bool is_path,
	const INT8 path_type,
	AFPCHARMAPARRAY char_map
)
{
	char ansi_string[MAX_AFP_PATH * 2];
	AFPERROR afp_error;
	INT16 string_len = 0;

	// Initialize the string buffer so the caller will get a clean
	// result even if no string is returned.
	memset(string, 0, cb_string);

	switch (path_type)
	{
	case kShortNames:
		afp_error = afpCallNotSupported;
		break;

	case kLongNames:
		afp_error = get_pascal_string(ansi_string, sizeof(ansi_string), &string_len);
		break;

	case kUnicodeNames:
		afp_error = get_unicode_string(ansi_string, sizeof(ansi_string), &string_len, is_path);
		break;

	default:
		DBGWRITE(dbg_level_info, L"Bad string type! (%d)\n", path_type);
		afp_error = afpParmErr;
		break;
	}

	if (AFP_SUCCESS(afp_error) && string_len > 0)
	{
		// Make sure the buffer passed is large enough to hold the unicode
		// pathname we'll get back.
		if (string_len * sizeof(unichar) < cb_string)
		{
			// If we're dealing with an AFP pathname, then we need to convert
			// the path separators to Be style ones.
			if (is_path)
			{
				if (afp_mac_to_windows_path(ansi_string, string, char_map) == nullptr)
				{
					DBGWRITE(dbg_level_info, L"Failed converting path to unicode!\n");
					afp_error = afpParmErr;
				}
			}
			else
			{
				if (afp_mac_to_windows_string(ansi_string, cb_string, string) == nullptr)
				{
					DBGWRITE(dbg_level_info, L"Failed converting string to unicode!\n");
					afp_error = afpParmErr;
				}
			}
		}
		else
		{
			DBGWRITE(dbg_level_info, L"Buffer too small for new string!\n");
			ASSERT(0);

			afp_error = afpParmErr;
		}
	}

	return afp_error;
}

/*
 * get_pascal_string()
 *
 * Description:
 *		Retrieve a pascal string from the request buffer and return
 *		it as a c-style string.
 *
 * Returns: None
 */
AFPERROR afp_net_buffer::get_pascal_string(char* string, const UINT16 cb_string, INT16* s_len)
{
	const UINT8 string_len = pull_num<INT8>();

	memset(string, 0, cb_string);

	if (string_len + sizeof(char) > cb_string)
	{
		DBGWRITE(dbg_level_info, L"String buffer too small! (%d)\n", string_len);
		return afpParmErr;
	}

	if (string_len > 0)
	{
		pull_raw_data(string, string_len);
		string[string_len] = '\0';
	}

	if (s_len != nullptr)
	{
		*s_len = string_len;
	}

	return AFP_OK;
}

/*
 * get_unicode_string()
 *
 * Description:
 *		Retrieve a normal unicode string (not AFPName) from the request buffer.
 *
 * Returns: None
 */
AFPERROR afp_net_buffer::get_unicode_string(WCHAR* string, const UINT16 cb_string, UINT16* s_len)
{
	char ansi_string[MAX_PATH];
	const INT32 string_len = *s_len;

	RtlZeroMemory(ansi_string, sizeof(ansi_string));
	RtlZeroMemory(string, cb_string);

	if (string_len + sizeof(char) > cb_string)
	{
		DBGWRITE(dbg_level_info, L"String buffer too small! (%d vs %d)\n", cb_string, string_len);
		return afpParmErr;
	}

	if (string_len > 0)
	{
		RtlCopyMemory(ansi_string, get_current_pos_ptr(), string_len);

		if (afp_mac_to_windows_string(ansi_string, cb_string, string) == nullptr)
		{
			return afpParmErr;
		}

		advance_pos_ptr(string_len);
		*s_len = static_cast<INT16>(lstrlen(string));
	}

	return AFP_OK;
}

/*
 * get_unicode_string()
 *
 * Description:
 *		Retrieve a unicode string from the request buffer and return
 *		it as a c-style string. This is a worker routine for
 *		GetString() and shouldn't be called directly.
 *
 *		This function converts an AFPName to a c-style string if isAFPName
 *		is set to TRUE. Otherwise, it just looks for a c-style string in
 *		the buffer.
 *
 * Returns: None
 */
AFPERROR afp_net_buffer::get_unicode_string(
	char* string,
	const UINT16 cb_string,
	INT16* s_len,
	const bool is_path,
	const bool is_afp_name)
{
	INT16 string_len;

	memset(string, 0, cb_string);

	// If it is an AFPName in the buffer then we look for the "hint" and
	// string length params. Otherwise, we get the length as passed in sLen.
	if (is_afp_name)
	{
		if (is_path)
		{
			pull_num<INT32>(); //hint not used.
		}

		string_len = pull_num<INT16>();
	}
	else
	{
		string_len = *s_len;
	}

	if (string_len < 0 || string_len >= MAX_AFP_PATH)
	{
		DBGWRITE(
			dbg_level_info,
			L"Bogus string length passed (%d)\n", string_len
		);

		return afpParmErr;
	}

	if ((string_len + sizeof(char)) > cb_string)
	{
		DBGWRITE(
			dbg_level_info,
			L"String buffer too small! (%d vs %d)\n", cb_string, string_len
		);

		return afpParmErr;
	}

	if (string_len > 0)
	{
		RtlCopyMemory(string, get_current_pos_ptr(), string_len);

		advance_pos_ptr(string_len);
		*s_len = string_len;
	}

	return AFP_OK;
}

/*
 * pull_raw_data()
 *
 * Description:
 *		Extracts an raw data from the buffer and advances the
 *		buffer pointer.
 *
 * Returns: void
 */
void afp_net_buffer::pull_raw_data(void* buffer, const size_t amount)
{
	if (mCurrentPos + amount > mBuffer + mBufferSize)
	{
		DBGWRITE(dbg_level_error, L"Reading past end of buffer!");
		ASSERT(0);

		return;
	}

	memcpy(buffer, mCurrentPos, amount);
	mCurrentPos += amount;
}

/*
 * push_pascal_string()
 *
 * Description:
 *		Converts a C style string into a pascal string and inAdds
 *		it into the afp buffer.
 *
 * Returns: error code.
 */
AFPERROR afp_net_buffer::push_pascal_string(const char* string)
{
	constexpr size_t max_pascal_string_length = 255;

	if (string == nullptr)
	{
		return afpParmErr;
	}

	const auto cb_string = strlen(string);
	if (cb_string > max_pascal_string_length || not_enough_space(cb_string))
	{
		return afpParmErr;
	}

	const INT8 p_len = static_cast<INT8>(cb_string);

	// Push the preceding string length byte in the reply.
	push_num(p_len);

	// Now the rest.
	push_raw_data(string, cb_string);

	return AFP_OK;
}

/*
 * push_pascal_string()
 *
 * Description:
 *		Converts a unicode style string into a pascal string and adds
 *		it into the afp buffer.
 *
 * Returns: error code.
 */
AFPERROR afp_net_buffer::push_pascal_string(unichar* string, AFPCHARMAPARRAY char_map)
{
	char ansi_string[MAX_AFP_PATH];

	if (afp_windows_to_mac_path(string, sizeof(ansi_string), char_map, ansi_string) != nullptr)
	{
		return push_pascal_string(ansi_string);
	}

	DBGWRITE(dbg_level_error, L"Failed to convert characters!\n");

	return afpParmErr;
}

/*
 * push_unicode_string()
 *
 * Description:
 *		Add a unicode string to the afp reply buffer.
 *
 * Returns: None
 */
AFPERROR afp_net_buffer::push_unicode_string(unichar* string, AFPCHARMAPARRAY char_map)
{
	const size_t cb_string = lstrlen(string);

	if (string != nullptr)
	{
		if (((mCurrentPos - mBuffer) + (cb_string * 2) + 2) <= mBufferSize)
		{
			char ansi_string[MAX_AFP_PATH];
			if (afp_windows_to_mac_path(string, sizeof(ansi_string), char_map, ansi_string) != nullptr)
			{
				const auto string_len = strlen(ansi_string);

				// If charMap is not NULL that means we are dealing with a pathname.
				// We only add the hint for paths, not any other type of string.
				if (char_map != nullptr)
				{
					// This is supposed to be the encoding hint. We need to
					// build an encoding mapping table before we can use this.
					// for now, 0 == MAC_ROMAN encoding.
					push_num<UINT32>(0);
				}

				push_num<UINT16>(static_cast<UINT16>(string_len));
				push_raw_data(ansi_string, string_len);

				return AFP_OK;
			}

			// If conversion failed, we want to write a 0 to the buffer
			// to show that there is 0 sized string.
			push_num<INT16>(0);
		}
	}

	return afpParmErr;
}

/*
 * AddIntNNN()
 *
 * Description:
 *		Wrapper to deal with old code.
 *
 * Returns: AFPERROR
 */
AFPERROR afp_net_buffer::push_int8(const INT8 value)
{
	return push_num<INT8>(value);
}

AFPERROR afp_net_buffer::push_int16(const INT16 value)
{
	return push_num<INT16>(value);
}

AFPERROR afp_net_buffer::push_int32(const INT32 value)
{
	return push_num<INT32>(value);
}

AFPERROR afp_net_buffer::push_int64(const INT64 value)
{
	return push_num<INT64>(value);
}

bool afp_net_buffer::check_space_needed(const size_t space_needed) const
{
	if (get_current_pos_ptr() - get_buffer() + space_needed > mBufferSize)
	{
		return false;
	}

	return true;
}

bool afp_net_buffer::not_enough_space(const size_t space_needed) const
{
	return !check_space_needed(space_needed);
}

bool afp_net_buffer::will_overflow(const UINT32 value) const
{
	return mBuffer + value > mBuffer + mBufferSize;
}
