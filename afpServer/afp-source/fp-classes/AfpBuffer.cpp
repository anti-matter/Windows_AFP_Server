/*
 *	AfpBuffer.cpp
 *
 *	Copyright © Michael J. Conrad. All Rights Reserved.
 */

#include "StdAfx.h"

#include <string.h>
#include <stdlib.h>
#include "AfpBuffer.h"

/*
 * AfpBuffer()
 *
 * Description:
 *		Initializer for afp buffer class.
 *
 * Returns: None
 */
AfpBuffer::AfpBuffer(_In_ UINT8* afpBuffer)
	: mBuffer(afpBuffer)
	  , mCurrentPos(afpBuffer)
	  , mBufferSize(0x7FFFFFFF)
	  , mDSIReturnValue(0)
	  , mDSIDataOffset(0)
{
}

AfpBuffer::AfpBuffer(_In_ UINT8* afpBuffer, _In_ const size_t cbbuffer)
	: mBuffer(afpBuffer)
	  , mCurrentPos(afpBuffer)
	  , mBufferSize(cbbuffer)
	  , mDSIReturnValue(0)
	  , mDSIDataOffset(0)
{
}

/*
 * ~AfpBuffer()
 *
 * Description:
 *		Destructor for afp buffer class.
 *
 * Returns: None
 */
AfpBuffer::~AfpBuffer()
{
}

/*
 * GetString()
 *
 * Description:
 *		Extracts a pascal style or unicode string from the afp buffer and
 *		returns it as a c-style string (null terminated). This function also
 *		advances the current buffer pointer to the end of the string
 *		and the next item in the buffer.
 *
 * Returns: None
 */
AFPERROR AfpBuffer::GetString(
	unichar* string,
	UINT16 cbstring,
	bool isPath,
	INT8 pathType,
	AFPCHARMAPARRAY charMap
)
{
	char ansi_string[MAX_AFP_PATH * 2];
	AFPERROR afpError;
	INT16 stringLen = 0;

	//
	//Initialize the string buffer so the caller will get a clean
	//result even if no string is returned.
	//
	memset(string, 0, cbstring);

	switch (pathType)
	{
	case kShortNames:
		afpError = afpCallNotSupported;
		break;

	case kLongNames:
		afpError = GetPascalString(ansi_string, sizeof(ansi_string), &stringLen);
		break;

	case kUnicodeNames:
		afpError = GetUnicodeString(ansi_string, sizeof(ansi_string), &stringLen, isPath);
		break;

	default:
		DBGWRITE(DbgLevelInfo, L"[afp:GetString]Bad string type! (%d)\n", pathType);
		afpError = afpParmErr;
		break;
	}

	if ((AFP_SUCCESS(afpError)) && (stringLen > 0))
	{
		//
		//Make sure the buffer passed is large enough to hold the unicode
		//pathname we'll get back.
		//
		if ((stringLen * sizeof(unichar)) < cbstring)
		{
			//
			//If we're dealing with an AFP pathname, then we need to convert
			//the path separators to Be style ones.
			//
			if (isPath)
			{
				if (AFPMacToWindowsPath(ansi_string, string, charMap) == nullptr)
				{
					DBGWRITE(DbgLevelInfo, L"[afp:GetString]Failed converting path to unicode!\n");
					afpError = afpParmErr;
				}
			}
			else
			{
				if (AFPMacToWindowsString(ansi_string, cbstring, string) == nullptr)
				{
					DBGWRITE(DbgLevelInfo, L"[afp:GetString]Failed converting string to unicode!\n");
					afpError = afpParmErr;
				}
			}
		}
		else
		{
			DBGWRITE(DbgLevelInfo, L"[afp:GetString]Buffer too small for new string!\n");
			ASSERT(0);

			afpError = afpParmErr;
		}
	}

	return afpError;
}

/*
 * GetUniString()
 *
 * Description:
 *		Retrieve a normal unicode string (not AFPName) from the request buffer.
 *
 * Returns: None
 */
AFPERROR AfpBuffer::GetUniString(WCHAR* string, UINT16 cbstring, UINT16* sLen)
{
	char ansi_string[MAX_PATH];
	INT32 stringLen = *sLen;
	AFPERROR afpError = AFP_OK;

	RtlZeroMemory(ansi_string, sizeof(ansi_string));
	RtlZeroMemory(string, cbstring);

	//
	//Check to make sure we were given a big enough buffer
	//to copy the string into.
	//
	if ((stringLen + sizeof(char)) > cbstring)
	{
		DBGWRITE(DbgLevelInfo, L"[AfpBuffer:GetUniString]String buffer too small! (%d vs %d)\n", cbstring, stringLen);
		return afpParmErr;
	}

	if (stringLen > 0)
	{
		RtlCopyMemory(ansi_string, GetCurrentPosPtr(), stringLen);

		if (AFPMacToWindowsString(ansi_string, cbstring, string) == nullptr)
		{
			return afpParmErr;
		}

		Advance(stringLen);
		*sLen = static_cast<INT16>(lstrlen(string));
	}

	return afpError;
}

/*
 * GetUnicodeString()
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
AFPERROR AfpBuffer::GetUnicodeString(char* string, UINT16 cbstring, INT16* sLen, bool isPath, bool isAFPName)
{
	INT16 stringLen;

	memset(string, 0, cbstring);

	//
	//If it is an AFPName in the buffer then we look for the "hint" and
	//string length params. Otherwise, we get the length as passed in sLen.
	//
	if (isAFPName)
	{
		if (isPath)
		{
			GetInt32(); //hint not used.
		}

		stringLen = GetInt16();
	}
	else
	{
		stringLen = *sLen;
	}

	//
	//Perform some basic checks to make sure we didn't get a bogus string
	//length passed to us. Note that we allow zero length strings!!
	//
	if ((stringLen < 0) || (stringLen >= MAX_AFP_PATH))
	{
		DBGWRITE(
			DbgLevelInfo,
			L"[AfpBuffer:GetUnicodeString]Bogus string length passed (%d)\n", stringLen
		);

		return afpParmErr;
	}

	//
	//Check to make sure we were given a big enough buffer
	//to copy the string into.
	//
	if ((stringLen + sizeof(char)) > cbstring)
	{
		DBGWRITE(
			DbgLevelInfo,
			L"[AfpBuffer:GetUnicodeString]String buffer too small! (%d vs %d)\n", cbstring, stringLen
		);

		return afpParmErr;
	}

	if (stringLen > 0)
	{
		RtlCopyMemory(string, GetCurrentPosPtr(), stringLen);

		Advance(stringLen);
		*sLen = stringLen;
	}

	return AFP_OK;
}

/*
 * GetPascalString()
 *
 * Description:
 *		Retrieve a pascal string from the request buffer and return
 *		it as a c-style string. This is a worker routine for
 *		GetString() and shouldn't be called directly.
 *
 * Returns: None
 */
AFPERROR AfpBuffer::GetPascalString(char* string, UINT16 cbstring, INT16* sLen)
{
	UINT8 stringLen = GetInt8();

	memset(string, 0, cbstring);

	//
	//Check to make sure we were given a big enough buffer
	//to copy the string into.
	//
	if (stringLen + sizeof(char) > cbstring)
	{
		DBGWRITE(DbgLevelInfo, L"[AfpBuffer:GetPascalString]String buffer too small! (%d)\n", stringLen);
		return afpParmErr;
	}

	if (stringLen > 0)
	{
		//
		//Now copy the string into the supplied buffer.
		//
		GetRawData(string, stringLen);

		//
		//Add a null terminator on the end of the new string.
		//
		string[stringLen] = '\0';
	}

	if (sLen != nullptr)
	{
		*sLen = stringLen;
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
INT8 AfpBuffer::GetInt8()
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
INT16 AfpBuffer::GetInt16()
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
INT32 AfpBuffer::GetInt32()
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
INT64 AfpBuffer::GetInt64()
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
void AfpBuffer::GetRawData(void* buffer, size_t amount)
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
AFPERROR AfpBuffer::AddCStringAsPascal(char* string)
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
AFPERROR AfpBuffer::AddUniStringAsPascal(unichar* string, AFPCHARMAPARRAY charMap)
{
	char ansi_string[MAX_AFP_PATH];

	if (AFPWindowsToMacPath(string, sizeof(ansi_string), charMap, ansi_string) != nullptr)
	{
		return AddCStringAsPascal(ansi_string);
	}

	DBGWRITE(DbgLevelInfo, L"[AfpBuffer:AddUniStringAsPascal]Failed to convert characters!\n");

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
AFPERROR AfpBuffer::AddUniString(unichar* string, AFPCHARMAPARRAY charMap)
{
	char ansi_string[MAX_AFP_PATH];
	size_t cbstring = lstrlen(string);

	if (string != nullptr)
	{
		if (((mCurrentPos - mBuffer) + (cbstring * 2) + 2) <= mBufferSize)
		{
			if (AFPWindowsToMacPath(string, sizeof(ansi_string), charMap, ansi_string) != nullptr)
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
			else
			{
				//
				//If conversion failed, we want to write a 0 to the buffer
				//to show that there is 0 sized string.
				//
				AddInt16(0);
			}
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
AFPERROR AfpBuffer::AddInt8(INT8 value)
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
AFPERROR AfpBuffer::AddInt16(INT16 value)
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
AFPERROR AfpBuffer::AddInt32(INT32 value)
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
AFPERROR AfpBuffer::AddInt64(INT64 value)
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
AFPERROR AfpBuffer::AddRawData(void* data, size_t size)
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
bool AfpBuffer::CheckSpaceNeeded(size_t spaceNeeded) const
{
	if (((GetCurrentPosPtr() - GetBuffer()) + spaceNeeded) > mBufferSize)
	{
		return false;
	}

	return true;
}
