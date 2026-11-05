#include "StdAfx.h"
#include <strsafe.h>

#include "afp_open_file.h"
#include "afpxattr.h"
#include "afp_volumes.h"
#include "afp_volume.h"
#include "afp_file_index.h"

afp_open_file::afp_open_file(
    const INT16 vol_id,
    const INT32 dir_id,
    const INT16 access_mode,
    const INT8 fork,
	afp_index* entry,
	const WCHAR* win_path
)
{
	m_file_handle_ = INVALID_HANDLE_VALUE;
	m_volume_id_ = vol_id;
	m_dir_id_ = dir_id;
	m_access_mode_ = access_mode;
	m_fork_ = static_cast<INT8>(fork);
	m_entry_ = entry;
	m_file_ref_num_ = 0;
	m_should_convert_line_endings_ = false;

	volume_ = afp_volumes::current()->find(vol_id);

	ASSERT(volume_ != nullptr);

	// ReSharper disable once CppFunctionResultShouldBeUsed
	StringCbCopy(m_windows_path_, sizeof(m_windows_path_), win_path);
}


afp_open_file::~afp_open_file()
{
	if (is_fork_open())
	{
		close_file_fork();
	}
}


AFPERROR afp_open_file::afp_open_file_fork(const PWCHAR stream_name, const DWORD creation_disposition)
{
	std::filesystem::path file_path(m_windows_path_);
	UINT32 desired_access = 0;
	INT16 afp_flags = flag_index_set_item;
	DWORD dw_flags_and_attr = FILE_ATTRIBUTE_NORMAL;

	if (m_access_mode_ & kReadMode)
	{
		desired_access |= GENERIC_READ;
	}

	if (m_access_mode_ & kWriteMode)
	{
		if (volume_->is_read_only())
		{
			return afpVolLocked;
		}

	    desired_access |= GENERIC_WRITE;
	}

	switch (m_fork_)
	{
	case kDataFork:
	{
		if (file_path.extension() == res_file_extension)
		{
			DBGWRITE(dbg_level_warning, L"Attempt to open data fork of .res file.");
			return afpAccessDenied;
		}

		afp_flags |= flag_index_data_fork_open;
		const std::wstring ext = file_path.extension();

		m_should_convert_line_endings_ = m_line_conversion_.should_convert(ext);
	}
	break;

	case kRsrcFork:
		if (file_path.extension() != res_file_extension)
		{
			file_path.concat(AFP_RESOURCE_FORK_NAME);
		}

		afp_flags |= flag_index_resource_fork_open;
        break;

	case kAttrFork:
		DBGWRITE(dbg_level_info, L"Opening stream %s\n", stream_name);
		ASSERT(stream_name != nullptr);

		if (stream_name != nullptr)
		{
			file_path.concat(L":");
			file_path.concat(stream_name);

			afp_flags |= flag_index_attribute_open;
			dw_flags_and_attr = FILE_FLAG_BACKUP_SEMANTICS;
		}
		break;

	default:
		ASSERT(0);
		break;
	}

	// ReSharper restore CppFunctionResultShouldBeUsed

	DBGWRITE(dbg_level_trace, L"Opening file %s\n", file_path.c_str());

	const auto h_file = CreateFile(
		file_path.c_str(),
		desired_access,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr,
		creation_disposition,
		dw_flags_and_attr,
		nullptr
	);

	if (h_file == INVALID_HANDLE_VALUE)
	{
		const auto dw_error = GetLastError();
		DBGWRITE(dbg_level_info, L"CreateFile() failed! (%d) (%s)\n", dw_error, file_path.c_str());

		return afp_map_win_error_to_afp(dw_error, afpParmErr);
	}

	//Get the next available file ref num.
	static INT16 next_file_ref_num = 1;
	m_file_ref_num_ = next_file_ref_num++;
	m_file_handle_ = h_file;

	//Now set the appropriate flags to indicate the state of the forks.
	afp_file_index::update_entry_flags(m_entry_, afp_flags);

	return AFP_OK;
}


AFPERROR afp_open_file::close_file_fork()
{
	INT16 afp_flags = 0;

	//First we need to update the flags in the afp_index as to the state of
	//this file fork. Note that AFPIndexUpdateEntryFlags() takes care of
	//actually clearing the bits in the index entry.

	switch (m_fork_)
	{
	case kDataFork:
		afp_flags |= flag_index_data_fork_open;
		break;

	case kRsrcFork:
		afp_flags |= flag_index_resource_fork_open;
		break;

	case kAttrFork:
		afp_flags |= flag_index_attribute_open;
		break;

	default:
		ASSERT(0);
		break;
	}

	DBGWRITE(dbg_level_trace, L"Closing file %s, fork = %d\n", m_windows_path_, m_fork_);

	//Now set the appropriate flags to indicate the state of the forks.
	afp_file_index::update_entry_flags(m_entry_, afp_flags);

	//Now, perform the actual close operation on the file.
	if (m_file_handle_ != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_file_handle_);
		m_file_handle_ = INVALID_HANDLE_VALUE;
	}
	else
	{
		DBGWRITE(dbg_level_trace, L"Invalid file handle for %s\n", m_windows_path_);
	}

	return AFP_OK;
}


AFPERROR afp_open_file::read_fork(
	afp_net_buffer& afp_reply,
	const INT64 afp_offset,
	INT64 afp_req_count,
	DWORD* afp_actual_read
)
{
	OVERLAPPED overlapped;
	LARGE_INTEGER offset;

	//Make sure we have a valid file handle before we do anything.
	if (m_file_handle_ == INVALID_HANDLE_VALUE)
	{
		ASSERT(0);
		return afpParmErr;
	}

	DBGWRITE(dbg_level_trace, L"Reading %lld bytes from file %s from fork %d\n", afp_req_count, m_windows_path_, m_fork_);

	//We have to make sure our buffer is big enough for
	//the read operation.
	if (afp_reply.get_buffer_size() < static_cast<size_t>(afp_req_count))
	{
		DBGWRITE(
			dbg_level_warning,
			L"Buffer too small! (needed=%d, have=%d)\n",
			afp_req_count,
			afp_reply.get_buffer_size()
		);

	    //Our buffer is too small, adjust the amount to read.
		afp_req_count = afp_reply.get_buffer_size();
	}

	//Get the 64bit offset into a LI structure.
	offset.QuadPart = afp_offset;

	//Set up our offsets into the file to read.
	overlapped.Offset = offset.LowPart;
	overlapped.OffsetHigh = offset.HighPart;
	overlapped.hEvent = nullptr;

	//Call the Windows API to do all the work for us.
	const auto success = ReadFile(
		m_file_handle_,
		afp_reply.get_current_pos_ptr(),
		static_cast<UINT32>(afp_req_count),
		afp_actual_read,
		&overlapped
	);

	AFPERROR afp_error = AFP_OK;

	if (!success)
	{
		afp_error = afp_map_win_error_to_afp(GetLastError(), afpParmErr);
		if (afp_error != afpEofError)
        {
            DBGWRITE(dbg_level_warning, L"ReadFile() failed! (%d)\n", GetLastError());
        }
	}
	else if (afp_req_count > 0 && *afp_actual_read == 0)
	{
		afp_error = afpEofError;
	}
	else if (*afp_actual_read > 0)
	{
		if (m_should_convert_line_endings_)
		{
			m_line_conversion_.convert_buffer(afp_reply, *afp_actual_read, line_conversion_cr);
		}

		afp_reply.advance_pos_ptr(static_cast<INT32>(*afp_actual_read));
	}

	return afp_error;
}


AFPERROR afp_open_file::write_fork(
	afp_net_buffer& afp_request,
	const INT64 afp_offset,
	const INT64 afp_req_count,
	const BOOL afp_from_start,
	INT64* afp_last_byte_written
) const
{
	// Make sure we have a valid file handle before we do anything.
	if (m_file_handle_ == INVALID_HANDLE_VALUE)
	{
		ASSERT(0);
		return afpParmErr;
	}

	// Get the ptr to the actual block of data we want to write out.
	afp_request.move_to(static_cast<INT32>(afp_request.get_dsi_data_offset()));

	// Set the file position to the desired offset before writing.
	auto afp_error = file_seek(afp_offset, afp_from_start ? FILE_BEGIN : FILE_END);

	if (AFP_SUCCESS(afp_error))
	{
		DWORD afp_actual = 0;
		const auto req_count = static_cast<DWORD>(afp_req_count);

		if (m_should_convert_line_endings_)
		{
			m_line_conversion_.convert_buffer(afp_request, req_count, line_conversion_lf);
		}

		DBGWRITE(dbg_level_trace, L"WRITE: Writing (%I64d) bytes from offset (%I64d)\n", afp_req_count, afp_offset);

		const auto success = WriteFile(
			m_file_handle_,
			afp_request.get_current_pos_ptr(),
			req_count,
			&afp_actual,
			nullptr
		);

		if (!success)
		{
			afp_error = afp_map_win_error_to_afp(GetLastError(), afpParmErr);
			DBGWRITE(dbg_level_info, L"WriteFile() failed! (%d)\n", GetLastError());
		}
		else
		{
			ASSERT(std::cmp_less_equal(afp_actual, afp_req_count));

			const auto file_pointer = SetFilePointer(m_file_handle_, 0, nullptr, FILE_CURRENT);
			if (file_pointer == INVALID_SET_FILE_POINTER)
			{
				// Call failed for some reason.
				DBGWRITE(dbg_level_error, L"WRITE: Failed to obtain current file position after write!\n");
				return afpParmErr;
			}

			*afp_last_byte_written = file_pointer;

			DBGWRITE(dbg_level_info,
			         L"WRITE: Actual written: %d, Last Byte: %d\n",
			         afp_actual,
			         *afp_last_byte_written
			);
		}
	}

	return afp_error;
}


AFPERROR afp_open_file::file_seek(
	const INT64 marker_distance,
	const UINT32 method
) const
{
	LARGE_INTEGER distance;
	AFPERROR afp_error = AFP_OK;

	distance.QuadPart = marker_distance;
	distance.LowPart = SetFilePointer(m_file_handle_, static_cast<LONG>(distance.LowPart), &distance.HighPart, method);

	if (distance.LowPart == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR)
	{
		DBGWRITE(dbg_level_info, L"SetFilePointer() failed %d\n", GetLastError());
		afp_error = afpParmErr;
	}

	return afp_error;
}

AFPERROR afp_open_file::set_fork_length(const INT64 length) const
{
	auto error = file_seek(length, FILE_BEGIN);
	if (AFP_FAILED(error))
	{
		DBGWRITE(dbg_level_error, L"Seeking fork length to (%d) failed with error (%d)\n", length, error);
		return error;
	}

	if (!SetEndOfFile(m_file_handle_))
	{
		// Failed to set end of file...
		const auto win_error = GetLastError();

		DBGWRITE(dbg_level_error, L"Setting end of file failed with (%d)\n", win_error);
		return afp_map_win_error_to_afp(win_error, afpParmErr);
	}

	DBGWRITE(dbg_level_trace, L"Fork length set to (%d)\n", length);

	return AFP_OK;
}


/*
 * get_stream_length()
 *
 * Description:
 *		Returns the length in bytes of the currently open stream.
 *
 * Returns: size or BAD_LENGTH_64 if failed
 */

LONGLONG afp_open_file::get_stream_length(const PWCHAR stream_name) const
{
	if (stream_name != nullptr)
	{
		// Call the extended attribute helper functions for the dirty work.
		return GetStreamLen(m_windows_path_, stream_name);
	}

	// If no stream name, get the length of the main stream/file

	if (m_file_handle_ != INVALID_HANDLE_VALUE)
	{
		LARGE_INTEGER file_size;

		if (GetFileSizeEx(m_file_handle_, &file_size))
		{
			return file_size.QuadPart;
		}

		DBGWRITE(dbg_level_info, L"GetFileSizeEx() failed (%d)\n", GetLastError());
	}

	return BAD_LENGTH_64;
}


/*
 * get_stream_list()
 *
 * Description:
 *		Populate an afp_array with the names and header info of all the streams
 *		for the file.
 *
 *		NOTE: Make sure the afp_array is initialized to contain element sizes
 *		equal to sizeof(afp_attr_info).
 *
 * Returns: afpError code
 */

AFPERROR afp_open_file::get_stream_list(std::vector<afp_attr_info>& streams) const
{
	// Call the extended attribute helper functions for the dirty work.
	return GetStreams(m_windows_path_, streams);
}

bool afp_open_file::is_volume_read_only() const
{
    return volume_->is_read_only();
}
