#include "StdAfx.h"
#include <cstring>
#include "afp_net_buffer.h"

afp_net_buffer::afp_net_buffer(_In_ UINT8* afp_net_buffer, _In_ const size_t cb_buffer)
	: mBuffer(afp_net_buffer)
	  , mCurrentPos(afp_net_buffer)
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
		DBGWRITE(dbg_level_info, L"[afp:GetString]Bad string type! (%d)\n", path_type);
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
	const UINT8 string_len = GetInt8();

	memset(string, 0, cb_string);

	//
	//Check to make sure we were given a big enough buffer
	//to copy the string into.
	//
	if (string_len + sizeof(char) > cb_string)
	{
		DBGWRITE(dbg_level_info, L"[afp_net_buffer:GetPascalString]String buffer too small! (%d)\n", string_len);
		return afpParmErr;
	}

	if (string_len > 0)
	{
		//
		//Now copy the string into the supplied buffer.
		//
		GetRawData(string, string_len);

		//
		//Add a null terminator on the end of the new string.
		//
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
		RtlCopyMemory(ansi_string, GetCurrentPosPtr(), string_len);

		if (afp_mac_to_windows_string(ansi_string, cb_string, string) == nullptr)
		{
			return afpParmErr;
		}

		Advance(string_len);
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
			L"[afp_net_buffer:get_unicode_string]String buffer too small! (%d vs %d)\n", cb_string, string_len
		);

		return afpParmErr;
	}

	if (string_len > 0)
	{
		RtlCopyMemory(string, GetCurrentPosPtr(), string_len);

		Advance(string_len);
		*s_len = string_len;
	}

	return AFP_OK;
}

/*
 * GetInt8()
 *
 * Description:
 *		Extracts an INT8 from the buffer and advances
 *		the buffer pointer.
 *
 * Returns: INT8
 */
INT8 afp_net_buffer::GetInt8()
{

	return *mCurrentPos++;
}

/*
 * GetInt16()
 *
 * Description:
 *		Extracts an INT16 from the buffer and advances the
 *		buffer pointer. This function is byte-order aware
 *
 * Returns: INT16
 */
INT16 afp_net_buffer::GetInt16()
{
	INT16 result = ntohs(*reinterpret_cast<INT16*>(mCurrentPos));

	mCurrentPos += sizeof(INT16);

	return result;
}

/*
 * GetInt32()
 *
 * Description:
 *		Extracts an INT32 from the buffer and advances the
 *		buffer pointer. This function is byte-order aware
 *
 * Returns: INT32
 */
INT32 afp_net_buffer::GetInt32()
{
	INT32 result = ntohl(*reinterpret_cast<INT32*>(mCurrentPos));

	mCurrentPos += sizeof(INT32);

	return result;
}

/*
 * GetInt64()
 *
 * Description:
 *		Extracts an INT64 from the buffer and advances the
 *		buffer pointer. This function is byte-order aware
 *
 * Returns: INT64
 */
INT64 afp_net_buffer::GetInt64()
{
	INT64 result = _byteswap_uint64(*reinterpret_cast<INT64*>(mCurrentPos));

	mCurrentPos += sizeof(INT64);

	return result;
}

/*
 * GetRawData()
 *
 * Description:
 *		Extracts an raw data from the buffer and advances the
 *		buffer pointer.
 *
 * Returns: void
 */
void afp_net_buffer::GetRawData(void* buffer, size_t amount)
{
	memcpy(buffer, mCurrentPos, amount);
	mCurrentPos += amount;
}

/*
 * AddCStringAsPascal()
 *
 * Description:
 *		Converts a C style string into a pascal string and inAdds
 *		it into the afp buffer.
 *
 * Returns: AFPERROR
 */
AFPERROR afp_net_buffer::AddCStringAsPascal(char* string)
{
	if (string != nullptr)
	{
		auto cbstring = strlen(string);

		if (mCurrentPos - mBuffer + cbstring + 1 <= mBufferSize)
		{
			*mCurrentPos++ = static_cast<UINT8>(cbstring);
			memcpy(mCurrentPos, string, cbstring);

			mCurrentPos += cbstring;

			return AFP_OK;
		}
	}

	return afpParmErr;
}

/*
 * AddUniStringAsPascal()
 *
 * Description:
 *		Converts a unicode style string into a pascal string and adds
 *		it into the afp buffer.
 *
 * Returns: AFPERROR
 */
AFPERROR afp_net_buffer::AddUniStringAsPascal(unichar* string, AFPCHARMAPARRAY charMap)
{
	char ansi_string[MAX_AFP_PATH];

	if (afp_windows_to_mac_path(string, sizeof(ansi_string), charMap, ansi_string) != nullptr)
	{
		return AddCStringAsPascal(ansi_string);
	}

	DBGWRITE(dbg_level_info, L"[afp_net_buffer:AddUniStringAsPascal]Failed to convert characters!\n");

	return afpParmErr;
}

/*
 * AddUniString()
 *
 * Description:
 *		Add a unicode string to the afp reply buffer.
 *
 * Returns: None
 */
AFPERROR afp_net_buffer::AddUniString(unichar* string, AFPCHARMAPARRAY charMap)
{
	char ansi_string[MAX_AFP_PATH];
	size_t cbstring = lstrlen(string);

	if (string != nullptr)
	{
		if (((mCurrentPos - mBuffer) + (cbstring * 2) + 2) <= mBufferSize)
		{
			if (afp_windows_to_mac_path(string, sizeof(ansi_string), charMap, ansi_string) != nullptr)
			{
				auto stringLen = strlen(ansi_string);

				//
				//If charMap is not NULL that means we are dealing with a pathname.
				//We only add the hint for paths, not any other type of string.
				//
				if (charMap != nullptr)
				{
					//
					//This is supposed to be the encoding hint. We need to
					//build an encoding mapping table before we can use this.
					//for now, 0 == MAC_ROMAN encoding.
					//
					AddInt32(0);
				}

				AddInt16(static_cast<INT16>(stringLen));
				AddRawData(ansi_string, stringLen);

				return AFP_OK;
			}
			//
			//If conversion failed, we want to write a 0 to the buffer
			//to show that there is 0 sized string.
			//
			AddInt16(0);
		}
	}

	return afpParmErr;
}

/*
 * AddInt8()
 *
 * Description:
 *		InAdds an INT8 into the buffer and advances the
 *		buffer pointer. This function is byte-order aware
 *
 * Returns: AFPERROR
 */
AFPERROR afp_net_buffer::AddInt8(INT8 value)
{
	if (((mCurrentPos - mBuffer) + sizeof(value)) <= mBufferSize)
	{
		*mCurrentPos++ = value;

		return AFP_OK;
	}

	return afpParmErr;
}

/*
 * AddInt16()
 *
 * Description:
 *		InAdds an INT16 into the buffer and advances the
 *		buffer pointer. This function is byte-order aware
 *
 * Returns: AFPERROR
 */
AFPERROR afp_net_buffer::AddInt16(INT16 value)
{
	if (((mCurrentPos - mBuffer) + sizeof(value)) <= mBufferSize)
	{
		*reinterpret_cast<INT16*>(mCurrentPos) = htons(value);
		mCurrentPos += sizeof(INT16);

		return AFP_OK;
	}

	return afpParmErr;
}

/*
 * AddInt32()
 *
 * Description:
 *		InAdds an INT32 into the buffer and advances the
 *		buffer pointer. This function is byte-order aware
 *
 * Returns: AFPERROR
 */
AFPERROR afp_net_buffer::AddInt32(INT32 value)
{
	if (((mCurrentPos - mBuffer) + sizeof(value)) <= mBufferSize)
	{
		*reinterpret_cast<INT32*>(mCurrentPos) = htonl(value);
		mCurrentPos += sizeof(INT32);

		return AFP_OK;
	}

	return afpParmErr;
}

/*
 * AddInt64()
 *
 * Description:
 *		InAdds an INT64 into the buffer and advances the
 *		buffer pointer. This function is byte-order aware
 *
 * Returns: AFPERROR
 */
AFPERROR afp_net_buffer::AddInt64(INT64 value)
{
	if (((mCurrentPos - mBuffer) + sizeof(value)) <= mBufferSize)
	{
		*reinterpret_cast<INT64*>(mCurrentPos) = _byteswap_uint64(value);
		mCurrentPos += sizeof(INT64);

		return AFP_OK;
	}

	return afpParmErr;
}

/*
 * AddRawData()
 *
 * Description:
 *		InAdds raw data into the buffer and advances the
 *		buffer pointer.
 *
 * Returns: AFPERROR
 */
AFPERROR afp_net_buffer::AddRawData(void* data, size_t size)
{
	if (((mCurrentPos - mBuffer) + size) <= mBufferSize)
	{
		memcpy(mCurrentPos, data, size);
		mCurrentPos += size;

		return (AFP_OK);
	}

	return afpParmErr;
}

/*
 * CheckSpaceNeeded()
 *
 * Description:
 *		Check to make sure there is enough space in the buffer.
 *
 * Returns: TRUE if there is enough space left in the buffer.
 */
bool afp_net_buffer::CheckSpaceNeeded(size_t spaceNeeded) const
{
	if (((GetCurrentPosPtr() - GetBuffer()) + spaceNeeded) > mBufferSize)
	{
		return false;
	}

	return true;
}
