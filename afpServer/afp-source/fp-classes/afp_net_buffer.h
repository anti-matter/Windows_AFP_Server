#pragma once

#include <filesystem>

#include "afp.h"
#include "afpdebug.h"
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

	std::wstring pull_path(UINT8 path_type);
	AFPERROR push_path(UINT8 path_type, const std::wstring& path);

	std::wstring pull_string(UINT8 path_type = kLongNames);
	void push_string(const std::wstring& str, UINT8 path_type = kLongNames);
	void push_string(const std::string& str, UINT8 path_type = kLongNames);

    static std::wstring convert_to_windows_path(const std::string& mac_path);
	static std::string convert_to_mac_path(const std::wstring& win_path);

	std::wstring pull_wide_string();

	template <class T>
	std::string pull_multi_byte_string();

	template <class T>
	void push_multi_byte_string(const std::string& mac_string);

	void pull_raw_data(void* buffer, size_t amount);
	void push_raw_data(const void* data, size_t size);

	template <class T>
	AFPERROR push_num(T value);

	template <class T>
	T pull_num();

	template <class T>
	T* get_ptr_offset();

	template <class T>
	T peek_num();

    [[nodiscard]] size_t get_data_length() const;

	void set_parameter_offset_value(INT16 offset, INT16 value);

    [[nodiscard]] bool check_space_needed(size_t space_needed) const;
    [[nodiscard]] bool not_enough_space(size_t space_needed) const;
    [[nodiscard]] bool will_overflow(UINT32 value) const;

    [[nodiscard]] char* get_current_pos_ptr() const;
	[[nodiscard]] char* get_buffer() const;
	[[nodiscard]] size_t get_buffer_size() const;

    void advance_pos_ptr(UINT32 offset);
    void advance_if_odd();
	void advance_to(INT32 offset);
	void rewind();

	void set_dsi_return_value(UINT32 value);
    [[nodiscard]] UINT32 get_dsi_return_value() const;

	void set_dsi_data_offset(UINT32 value);
    [[nodiscard]] UINT32 get_dsi_data_offset() const;

private:
	char* buffer_;
	char* current_buffer_ptr_;
	size_t buffer_size_;

	//
	//Sometimes, like in the case of FPRead, the function result is returned
	//as part of the DSI header in the size argument. We provide this var
	//to pass that value on to the dispatch function to properly return it.
	//
	UINT32 dsi_return_value_;

	//
	//This value stores the contents of the DSI_DATA_OFFSET parameter since
	//AFP functions cannot access that part of the buffer.
	//
	UINT32 dsi_data_offset_;
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

// Returns the numerical value at the current ptr offset
// without advancing the ptr.
template <class T>
T afp_net_buffer::peek_num()
{
	const auto value = pull_num<T>();
	current_buffer_ptr_ -= sizeof(T);

	return value;
}

template <class T>
AFPERROR afp_net_buffer::push_num(T value)
{
	if (not_enough_space(sizeof(T)))
	{
		return afpParmErr;
	}

	if (sizeof(T) > 1)
	{
		*reinterpret_cast<T*>(current_buffer_ptr_) = byte_swap<T>(value);
	}
	else
	{
		*reinterpret_cast<T*>(current_buffer_ptr_) = value;
	}

	advance_pos_ptr(sizeof(T));

	return AFP_OK;
}

template <class T>
T afp_net_buffer::pull_num()
{
	T result;

	if (sizeof(T) > 1)
	{
		result = byte_swap<T>(*reinterpret_cast<T*>(current_buffer_ptr_));
	}
	else
	{
		result = *reinterpret_cast<T*>(current_buffer_ptr_);
	}

	advance_pos_ptr(sizeof(T));

	return result;
}

__pragma(warning(pop))

inline size_t afp_net_buffer::get_data_length() const
{
	return current_buffer_ptr_ - buffer_;
}

inline void afp_net_buffer::set_parameter_offset_value(const INT16 offset, const INT16 value)
{
	if (will_overflow(offset + value))
	{
		DBGWRITE(dbg_level_error, L"Overflow adding parameter at offset!");
		ASSERT(0);

		return;
	}

	const auto save = current_buffer_ptr_;
	advance_to(offset);
	push_num(value);
	current_buffer_ptr_ = save;
}

inline char* afp_net_buffer::get_current_pos_ptr() const
{
	return current_buffer_ptr_;
}

inline void afp_net_buffer::advance_pos_ptr(const UINT32 offset)
{
	if (will_overflow(offset))
	{
		DBGWRITE(dbg_level_error, L"Overflow advancing to offset!");
		ASSERT(0);

		return;
	}

	current_buffer_ptr_ += offset;
}

inline void afp_net_buffer::push_raw_data(const void* data, const size_t size)
{
	if (will_overflow(size))
	{
		DBGWRITE(dbg_level_error, L"Overflow pushing raw data to buffer! size = %d\n", size);
		ASSERT(0);

		return;
	}

	memcpy(current_buffer_ptr_, data, size);
	advance_pos_ptr(size);
}

inline void afp_net_buffer::advance_if_odd()
{
	if ((current_buffer_ptr_ - (buffer_ + dsi_header_size)) % 2) advance_pos_ptr(sizeof(INT8));
}

inline void afp_net_buffer::advance_to(const INT32 offset)
{
	if (will_overflow(offset))
	{
		DBGWRITE(dbg_level_error, L"Overflow advancing to offset!");
		ASSERT(0);

		return;
	}

	current_buffer_ptr_ = buffer_ + offset;
}

inline char* afp_net_buffer::get_buffer() const
{
	return buffer_;
}

inline size_t afp_net_buffer::get_buffer_size() const
{
	return buffer_size_;
}

inline void afp_net_buffer::rewind()
{
	current_buffer_ptr_ = buffer_;
}

inline void afp_net_buffer::set_dsi_return_value(const UINT32 value)
{
	dsi_return_value_ = value;
}

inline UINT32 afp_net_buffer::get_dsi_return_value() const
{
	return dsi_return_value_;
}

inline void afp_net_buffer::set_dsi_data_offset(const UINT32 value)
{
	dsi_data_offset_ = value;
}

inline UINT32 afp_net_buffer::get_dsi_data_offset() const
{
	return dsi_data_offset_;
}
