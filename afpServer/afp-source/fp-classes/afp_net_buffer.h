#pragma once

#include "afp.h"
#include "afphelp.h"
#include "byte_swap.h"

class afp_net_buffer // NOLINT(cppcoreguidelines-special-member-functions)
{
public:
	afp_net_buffer(UINT8* afp_net_buffer, size_t cb_buffer);

	AFPERROR get_pascal_string(unichar* string, UINT16 cb_string, bool is_path = false, INT8 path_type = kLongNames,
	                   AFPCHARMAPARRAY char_map = nullptr);
	AFPERROR get_pascal_string(char* string, UINT16 cb_string, INT16* s_len = nullptr);
	AFPERROR get_unicode_string(WCHAR* string, UINT16 cb_string, UINT16* s_len);
	AFPERROR get_unicode_string(char* string, UINT16 cb_string, INT16* s_len, bool is_path, bool is_afp_name = true);

	INT8 GetInt8();
	INT16 GetInt16();
	INT32 GetInt32();
	int64 GetInt64();

	void GetRawData(void* buffer, size_t amount);

	AFPERROR AddCStringAsPascal(char* string);
	AFPERROR AddUniStringAsPascal(unichar* string, AFPCHARMAPARRAY charMap = nullptr);
	AFPERROR AddUniString(unichar* string, AFPCHARMAPARRAY charMap = nullptr);

	template <class T>
	AFPERROR push_num(T value);

	template <class T>
	T pull_num();

	template <class T>
	T get_num() const;

	AFPERROR AddInt8(INT8 value);
	AFPERROR AddInt16(INT16 value);
	AFPERROR AddInt32(INT32 value);
	AFPERROR AddInt64(INT64 value);

	AFPERROR AddRawData(void* data, size_t size);

	INT32 GetDataLength() const;

	void AddParameterOffset(INT16 offset, INT16 value);

	bool CheckSpaceNeeded(size_t spaceNeeded) const;

	UINT8* GetCurrentPosPtr() const;
	void Advance(INT32 amount);
	void AdvanceIfOdd();
	void AdvanceTo(INT32 offset);
	UINT8* GetBuffer() const;
	size_t GetBufferSize() const;
	void Rewind();

	void SetDSIReturnValue(UINT32 value);
	UINT32 GetDSIReturnValue() const;

	void SetDSIDataOffset(UINT32 value);
	UINT32 GetDSIDataOffset() const;

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

/*
 * push_num()
 *
 * Description:
 *		Extracts a number of type T from the buffer and advances
 *		the position pointer by sizeof(T).
 */
template <class T>
AFPERROR afp_net_buffer::push_num(T value)
{
	if (mCurrentPos - mBuffer + sizeof(T) > mBufferSize)
	{
		return afpParmErr;
	}

	if (sizeof(T) > 1)
	{
		*reinterpret_cast<T*>(mCurrentPos) = byte_swap<T>(value);
	}
	else
	{
		*reinterpret_cast<T*>(mCurrentPos) = value;
	}

	Advance(sizeof(T));

	return AFP_OK;
}

/*
 * pull_num()
 *
 * Description:
 *		Pulls a number of type T from the buffer and advances the
 *		position pointer by sizeof(T).
 */
template <class T>
T afp_net_buffer::pull_num()
{
	T result;

	if (sizeof(T) > 1)
	{
		result = byte_swap<T>(*reinterpret_cast<T*>(mCurrentPos));
	}
	else
	{
		result = *reinterpret_cast<T*>(mCurrentPos);
	}

	Advance(sizeof(T));

	return result;
}

/*
 * get_num()
 *
 * Description:
 *		Retrieves a number of type T from the current position in the buffer.
 *		This method does NOT advance the position pointer.
 */
template <class T>
T afp_net_buffer::get_num() const
{
	T result;

	if (sizeof(T) > 1)
	{
		return byte_swap<T>(*reinterpret_cast<T*>(mCurrentPos));
	}

	return *reinterpret_cast<T*>(mCurrentPos);
}

inline INT32 afp_net_buffer::GetDataLength() const
{
	return mCurrentPos - mBuffer;
}

inline void afp_net_buffer::AddParameterOffset(INT16 offset, INT16 value)
{
	const auto save = mCurrentPos;
	AdvanceTo(offset);
	AddInt16(value);
	mCurrentPos = save;
}

inline UINT8* afp_net_buffer::GetCurrentPosPtr() const
{
	return (mCurrentPos);
}

inline void afp_net_buffer::Advance(INT32 amount)
{
	mCurrentPos += amount;
}

inline void afp_net_buffer::AdvanceIfOdd()
{
	if ((mCurrentPos - (mBuffer + DSI_HEADER_SIZE)) % 2) Advance(sizeof(INT8));
}

inline void afp_net_buffer::AdvanceTo(INT32 offset)
{
	mCurrentPos = mBuffer + offset;
}

inline UINT8* afp_net_buffer::GetBuffer() const
{
	return (mBuffer);
}

inline size_t afp_net_buffer::GetBufferSize() const
{
	return (mBufferSize);
}

inline void afp_net_buffer::Rewind()
{
	mCurrentPos = mBuffer;
}

inline void afp_net_buffer::SetDSIReturnValue(UINT32 value)
{
	mDSIReturnValue = value;
}

inline UINT32 afp_net_buffer::GetDSIReturnValue() const
{
	return mDSIReturnValue;
}

inline void afp_net_buffer::SetDSIDataOffset(UINT32 value)
{
	mDSIDataOffset = value;
}

inline UINT32 afp_net_buffer::GetDSIDataOffset() const
{
	return mDSIDataOffset;
}
