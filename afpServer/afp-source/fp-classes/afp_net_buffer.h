#pragma once

#include <filesystem>

#include "afp.h"
#include "afpdebug.h"
#include "afphelp.h"
#include "byte_swap.h"
#include "dsi/dsi_connection.h"

const std::wstring win_illegal_path_characters = L"\\/\"?*<>|";
constexpr wchar_t first_replacement_character = 161;
constexpr size_t max_pascal_string_length = 255;

typedef std::basic_string<unsigned char> ustring;

class afp_net_buffer // NOLINT(cppcoreguidelines-special-member-functions)
{
public:
	afp_net_buffer(_In_ char* afp_buffer, _In_ size_t cb_buffer);

	AFPERROR get_pascal_string(unichar* string, const UINT16 cb_string, const bool is_path = false, const UINT8 path_type = kLongNames,
                               AFPCHARMAPARRAY char_map = nullptr);

	std::wstring pull_path(UINT8 path_type);
	AFPERROR push_path(UINT8 path_type, const std::wstring& path);

	std::wstring pull_string(UINT8 path_type = kLongNames);
	void push_string(const std::wstring& str);

    static std::wstring convert_to_windows_path(const std::string& mac_path);
	static std::string convert_to_mac_path(const std::wstring& win_path);

	std::wstring pull_wide_string();

	template <class T>
	std::string pull_multi_byte_string();

	template <class T>
	void push_multi_byte_string(const std::string& mac_string);

	AFPERROR get_pascal_string(char* string, UINT16 cb_string, INT16* s_len = nullptr);
	AFPERROR get_unicode_string(WCHAR* string, UINT16 cb_string, UINT16* s_len);
	AFPERROR get_unicode_string(char* string, UINT16 cb_string, INT16* s_len, bool is_path, bool is_afp_name = true);

	void pull_raw_data(void* buffer, size_t amount);

	AFPERROR push_pascal_string(const char* string);
	AFPERROR push_pascal_string(const std::wstring& wide_string, AFPCHARMAPARRAY char_map = nullptr);
	AFPERROR push_unicode_string(const std::wstring& wide_string, AFPCHARMAPARRAY char_map = nullptr);

	template <class T>
	AFPERROR push_num(T value);

	template <class T>
	T pull_num();

	template <class T>
	T get_num() const;

	template <class T>
	T* get_ptr_offset();

	AFPERROR push_int8(INT8 value);
	AFPERROR push_int16(INT16 value);
	AFPERROR push_int32(INT32 value);
	AFPERROR push_int64(INT64 value);

	size_t get_data_length() const;

	void add_parameter_offset(INT16 offset, INT16 value);

	bool check_space_needed(size_t space_needed) const;
	bool not_enough_space(size_t space_needed) const;
	bool will_overflow(UINT32 value) const;

	char* get_current_pos_ptr() const;
	void advance_pos_ptr(UINT32 offset);
    void push_raw_data(const void* data, size_t size);
    void advance_if_odd();
	void advance_to(INT32 offset);
	char* get_buffer() const;
	size_t get_buffer_size() const;
	void rewind();

	void set_dsi_return_value(UINT32 value);
	UINT32 get_dsi_return_value() const;

	void set_dsi_data_offset(UINT32 value);
	UINT32 get_dsi_data_offset() const;

private:
	char* mBuffer;
	char* mCurrentPos;
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

__pragma(warning(push))
__pragma(warning(disable:4127))


template <class T>
T* afp_net_buffer::get_ptr_offset()
{
	T* offset = reinterpret_cast<T*>(get_current_pos_ptr());
	advance_pos_ptr(sizeof(T));

	return offset;
}

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
	if (not_enough_space(sizeof(T)))
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

	advance_pos_ptr(sizeof(T));

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

	advance_pos_ptr(sizeof(T));

	return result;
}

/*
 * get_num()
 *
 * Description:
 *		Retrieves a number of type T from the current position in the buffer.
 *		This method does NOT advance_pos_ptr the position pointer.
 */
template <class T>
T afp_net_buffer::get_num() const
{
	if (sizeof(T) > 1)
	{
		return byte_swap<T>(*reinterpret_cast<T*>(mCurrentPos));
	}

	return *reinterpret_cast<T*>(mCurrentPos);
}

__pragma(warning(pop))

inline size_t afp_net_buffer::get_data_length() const
{
	return mCurrentPos - mBuffer;
}

inline void afp_net_buffer::add_parameter_offset(const INT16 offset, const INT16 value)
{
	if (will_overflow(offset + value))
	{
		DBGWRITE(dbg_level_error, L"Overflow adding parameter at offset!");
		ASSERT(0);

		return;
	}

	const auto save = mCurrentPos;
	advance_to(offset);
	push_int16(value);
	mCurrentPos = save;
}

inline char* afp_net_buffer::get_current_pos_ptr() const
{
	return mCurrentPos;
}

inline void afp_net_buffer::advance_pos_ptr(const UINT32 offset)
{
	if (will_overflow(offset))
	{
		DBGWRITE(dbg_level_error, L"Overflow advancing to offset!");
		ASSERT(0);

		return;
	}

	mCurrentPos += offset;
}

inline void afp_net_buffer::push_raw_data(const void* data, const size_t size)
{
	if (will_overflow(size))
	{
		DBGWRITE(dbg_level_error, L"Overflow pushing raw data to buffer! size = %d\n", size);
		ASSERT(0);

		return;
	}

	memcpy(get_current_pos_ptr(), data, size);
	advance_pos_ptr(size);
}

inline void afp_net_buffer::advance_if_odd()
{
	if ((mCurrentPos - (mBuffer + dsi_header_size)) % 2) advance_pos_ptr(sizeof(INT8));
}

inline void afp_net_buffer::advance_to(const INT32 offset)
{
	if (will_overflow(offset))
	{
		DBGWRITE(dbg_level_error, L"Overflow advancing to offset!");
		ASSERT(0);

		return;
	}

	mCurrentPos = mBuffer + offset;
}

inline char* afp_net_buffer::get_buffer() const
{
	return mBuffer;
}

inline size_t afp_net_buffer::get_buffer_size() const
{
	return mBufferSize;
}

inline void afp_net_buffer::rewind()
{
	mCurrentPos = mBuffer;
}

inline void afp_net_buffer::set_dsi_return_value(const UINT32 value)
{
	mDSIReturnValue = value;
}

inline UINT32 afp_net_buffer::get_dsi_return_value() const
{
	return mDSIReturnValue;
}

inline void afp_net_buffer::set_dsi_data_offset(const UINT32 value)
{
	mDSIDataOffset = value;
}

inline UINT32 afp_net_buffer::get_dsi_data_offset() const
{
	return mDSIDataOffset;
}
