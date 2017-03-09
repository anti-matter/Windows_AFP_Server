/*
 *	AfpBuffer.h
 *
 *	Copyright © Michael J. Conrad. All Rights Reserved.
 */

#pragma once

#include "afp.h"
#include "afphelp.h"
#include "ByteSwap.h"

class AfpBuffer
{
public:
	explicit AfpBuffer(_In_ UINT8* afpBuffer);
	AfpBuffer(_In_ UINT8* afpBuffer, _In_ const size_t cbbuffer);

	~AfpBuffer();

	AFPERROR GetString(unichar* string, UINT16 cbstring, bool isPath = false, INT8 pathType = kLongNames, AFPCHARMAPARRAY charMap = nullptr);
	AFPERROR GetPascalString(char* string, UINT16 cbstring, INT16* sLen = nullptr);
	AFPERROR GetUniString(WCHAR* string, UINT16 cbstring, UINT16* sLen);
	AFPERROR GetUnicodeString(char* string, UINT16 cbstring, INT16* sLen, bool isPath, bool isAFPName = true);

	INT8 GetInt8();
	INT16 GetInt16();
	INT32 GetInt32();
	int64 GetInt64();

	void GetRawData(void* buffer, size_t amount);

	AFPERROR AddCStringAsPascal(char* string);
	AFPERROR AddUniStringAsPascal(unichar* string, AFPCHARMAPARRAY charMap = nullptr);
	AFPERROR AddUniString(unichar* string, AFPCHARMAPARRAY charMap = nullptr);

	// Templatized functions for add/retrieving integers from the buffer. Over time,
	// move to these methods instead of fully typed functions.
	template <class T>
	AFPERROR AddInteger(T value)
	{
		if (mCurrentPos - mBuffer + sizeof(T) > mBufferSize)
			return afpParmErr;

		*reinterpret_cast<T*>(mCurrentPos) = ByteSwap<T>(value);
		Advance(sizeof(T));

		return AFP_OK;
	}

	template <class T>
	T GetInteger()
	{
		T result;

		result = ByteSwap<T>(*reinterpret_cast<T*>(mCurrentPos));
		Advance(sizeof(T));

		return result;
	}

	AFPERROR AddInt8(INT8 value);
	AFPERROR AddInt16(INT16 value);
	AFPERROR AddInt32(INT32 value);
	AFPERROR AddInt64(INT64 value);

	AFPERROR AddRawData(void* data, size_t size);

	INT32 GetDataLength() const { return static_cast<INT32>(mCurrentPos - mBuffer); }

	void AddParameterOffset(INT16 offset, INT16 value)
	{
		auto save = mCurrentPos;
		AdvanceTo(offset);
		AddInt16(value);
		mCurrentPos = save;
	}

	bool CheckSpaceNeeded(size_t spaceNeeded) const;

	UINT8* GetCurrentPosPtr() const { return (mCurrentPos); }
	void Advance(INT32 amount) { mCurrentPos += amount; }
	void AdvanceIfOdd() { if ((mCurrentPos - (mBuffer + DSI_HEADER_SIZE)) % 2) Advance(sizeof(INT8)); }
	void AdvanceTo(INT32 offset) { mCurrentPos = mBuffer + offset; }
	UINT8* GetBuffer() const { return (mBuffer); }
	size_t GetBufferSize() const { return (mBufferSize); }
	void Rewind() { mCurrentPos = mBuffer; }

	void SetDSIReturnValue(UINT32 value) { mDSIReturnValue = value; }
	UINT32 GetDSIReturnValue() const { return mDSIReturnValue; }

	void SetDSIDataOffset(UINT32 value) { mDSIDataOffset = value; }
	UINT32 GetDSIDataOffset() const { return mDSIDataOffset; }

private:

	UINT8* mBuffer;
	UINT8* mCurrentPos;
	size_t mBufferSize;

	//
	//Sometimes, like in the case of FPRead, the function result is returned
	//as part of the DSI header in the size argument. We provide this var
	//to pass that value on to the dispatch function to properly return it.
	//
	UINT32 mDSIReturnValue;

	//
	//This value stores the contents of the DSI_DATA_OFFSET parameter since
	//AFP functions cannot access that part of the buffer.
	//
	UINT32 mDSIDataOffset;
};
