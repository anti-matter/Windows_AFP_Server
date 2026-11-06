#pragma once

#include "afp_net_buffer.h"
#include "line-conversion/line_conversion.h"

class afp_volume;
struct afp_index;

// Struct to keep info about a particular stream. All we care
// about from the afp perspective is the size and the name.
typedef struct
{
	LARGE_INTEGER stream_size;
	WCHAR stream_name[MAX_PATH];
} afp_attr_info;

class afp_open_file  // NOLINT(cppcoreguidelines-special-member-functions)
{
public:
    afp_open_file(
		INT16 vol_id,
		INT32 dir_id,
		INT16 access_mode,
		INT8 fork,
		std::shared_ptr<afp_index> entry,
        const WCHAR* win_path
	);

	~afp_open_file();

	AFPERROR afp_open_file_fork(PWCHAR stream_name = nullptr, DWORD creation_disposition = OPEN_ALWAYS);
	AFPERROR close_file_fork();
	AFPERROR read_fork(
		afp_net_buffer& afp_reply,
		INT64 afp_offset,
		INT64 afp_req_count,
		DWORD* afp_actual_read
	);
	AFPERROR write_fork(
		afp_net_buffer& afp_request,
		INT64 afp_offset,
		INT64 afp_req_count,
		BOOL afp_from_start,
		INT64* afp_last_byte_written
	) const;

	AFPERROR file_seek(
		INT64 marker_distance,
		UINT32 method
	) const;

	AFPERROR set_fork_length(INT64 length) const;

    [[nodiscard]] bool is_fork_open() const { return m_file_handle_ != INVALID_HANDLE_VALUE; }

	LONGLONG get_stream_length(PWCHAR stream_name = nullptr) const;
	AFPERROR get_stream_list(std::vector<afp_attr_info>& streams) const;

    [[nodiscard]] HANDLE get_file_handle() const { return m_file_handle_; }
    [[nodiscard]] INT16 get_file_ref_num() const { return m_file_ref_num_; }
    [[nodiscard]] INT8 get_fork() const { return m_fork_; }
    [[nodiscard]] INT16 get_vol_id() const { return m_volume_id_; }
    [[nodiscard]] INT32 get_dir_id() const { return m_dir_id_; }
	[[nodiscard]] WCHAR* get_win_path() { return m_windows_path_; }
	// The open file holds an owning reference to its index entry, so
	// these stay valid for the lifetime of the open file even if a
	// concurrent delete unlinks the entry (the entry becomes an inert
	// "orphan" until this file closes).
	[[nodiscard]] afp_index* get_entry() const { return m_entry_.get(); }
	[[nodiscard]] const std::shared_ptr<afp_index>& get_entry_ref() const { return m_entry_; }
    [[nodiscard]] const std::shared_ptr<afp_volume>& get_volume() const { return volume_; }
    [[nodiscard]] bool is_volume_read_only() const;

private:

	HANDLE m_file_handle_;

	INT16 m_file_ref_num_;
	INT16 m_volume_id_;
	INT32 m_dir_id_;
	INT16 m_access_mode_;
	INT8 m_fork_;

	std::shared_ptr<afp_volume> volume_;
	std::shared_ptr<afp_index> m_entry_;

	WCHAR m_windows_path_[MAX_PATH];

	std::vector<std::wstring> m_line_convert_extensions_;

	line_conversion m_line_conversion_;
	bool m_should_convert_line_endings_;
};
