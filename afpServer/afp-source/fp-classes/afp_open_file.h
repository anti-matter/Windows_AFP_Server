#pragma once

#include "volumes.h"
#include "afphelp.h"
#include "afp_net_buffer.h"

// Struct to keep info about a particular stream. All we care
// about from the afp perspective it the size and the name.
typedef struct
{
	LARGE_INTEGER stream_size;
	WCHAR stream_name[MAX_PATH];
} afp_attr_info;

class afp_open_file  // NOLINT(cppcoreguidelines-special-member-functions)
{
public:
    afp_open_file(
		uint16 vol_id,
		uint32 dir_id,
		int16 access_mode,
		uint8 fork,
		afp_index* entry,
        const WCHAR* win_path
	);

	~afp_open_file();

	AFPERROR afp_open_file_fork(PWCHAR stream_name = nullptr, DWORD afp_creation_disposition = OPEN_ALWAYS);
	AFPERROR close_file_fork();
	AFPERROR read_fork(
		afp_net_buffer& afp_reply,
		int64 afp_offset,
		int64 afp_req_count,
		uint32* afp_actual_read
	);
	AFPERROR write_fork(
		afp_net_buffer& afp_request,
		int64 afp_offset,
		int64 afp_req_count,
		BOOL afp_from_start,
		int64* afp_last_byte_written
	) const;

	AFPERROR file_seek(
		int64 marker_distance,
		uint32 method
	) const;

	bool is_fork_open() const { return m_file_handle_ != INVALID_HANDLE_VALUE; }

	LONGLONG get_stream_length(PWCHAR stream_name = nullptr);
	AFPERROR get_stream_list(std::vector<afp_attr_info>& streams);

	HANDLE get_file_handle() const { return m_file_handle_; }
	int16 get_file_ref_num() const { return m_file_ref_num_; }
	int8 get_fork() const { return m_fork_; }
	int16 get_vol_id() const { return m_volume_id_; }
	int32 get_dir_id() const { return m_dir_id_; }
	WCHAR* get_win_path() { return m_windows_path_; }
	afp_index* get_entry() const { return m_entry_; }

private:

	HANDLE m_file_handle_;

	int16 m_file_ref_num_;
	uint16 m_volume_id_;
	uint32 m_dir_id_;
	int16 m_access_mode_;
	int8 m_fork_;

	afp_index* m_entry_;

	WCHAR m_windows_path_[MAX_PATH];
};
