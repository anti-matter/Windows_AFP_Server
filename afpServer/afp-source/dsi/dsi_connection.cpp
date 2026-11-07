#include "stdafx.h"
#include <thread>
#include "afpdebug.h"
#include "afpsrvr.h"
#include "dsi_connection.h"
#include "afpfileio.h"
#include "afpdispatch.h"
#include "afpdhxlogin.h"
#include "afp_session.h"
#include "dsi_connections.h"
#include "perf_counters.h"
#include "windows_security.h"

extern dsi::dsi_connections active_connections;

namespace dsi
{
	dsi_connection::dsi_connection(const SOCKET socket, const UINT32 connection_id)
		: socket_(socket)
		  , socket_closed_(false)
		  , owner_thread_id_(0)
		  , connection_id_(connection_id)
		  , attention_quantum_size_(0)
		  , continue_receive_(true)
		  , dsi_session_open_(false)
		  , client_request_id_(0)
		  , server_request_id_(0)
		  , last_tickle_(GetTickCount64())
		  , client_sleeping_(false)
		  , user_token_(nullptr)
		  , session_authenticated_(false)
		  , afp_version_(0)
		  , uam_type_(0)
		  , extended_logon_info_(nullptr)
		  , guest_session_(false)
	{
		DBGWRITE(dbg_level_info, L"Connection (%d) accepted\n", connection_id_);

		receive_buffer_ = std::make_unique<char[]>(RECV_BUFFER_SIZE);
		afp_session_ = std::make_unique<afp_session>(this);

		if (!adjust_privilege(const_cast<PWCHAR>(SE_IMPERSONATE_NAME), TRUE))
		{
			DBGWRITE(dbg_level_error, L"Setting impersonation privilege failed (%d)\n", GetLastError());
			ASSERT(0);
		}
	}

	dsi_connection::~dsi_connection()
	{
		// Release any resources that were still held when the connection
		// ended (e.g. a client that disconnected without logging off or
		// before completing a login).
		if (user_token_ != nullptr)
		{
			CloseHandle(user_token_);
			user_token_ = nullptr;
		}

		free_extended_logon_info();

		// Safety net; the connection thread normally closes the socket
		// before removing this instance from tracking.
		close_socket();

		DBGWRITE(dbg_level_info, L"dsi_connection (%d) closed.\n", connection_id_);
	}

    void dsi_connection::connection_thread()
	{
		// Remember the id of the thread that owns this connection; only it
		// may close the socket (see kill_connection/close_socket).
		owner_thread_id_ = GetCurrentThreadId();

#ifdef _DEBUG
        const auto connection_id = connection_id_;
#endif
		DBGWRITE(dbg_level_info, L"Connection thread started (%d)\n", connection_id_);

		while(continue_receive_)
		{
			receive();
		}

		DBGWRITE(dbg_level_info, L"Connection (%d) receive loop ended, stopping tracking\n", connection_id_);

		// Close the socket and remove us from tracking. (stop_tracking
		// closes the socket under the tracking lock, on this thread, and
		// deleting the tracking entry deletes this instance, so it must
		// be the last thing the thread does. Any kill initiated from
		// another thread only performed a shutdown() to wake us up;
		// closesocket() belongs to this thread.)
		active_connections.stop_tracking(connection_id_);

		DBGWRITE(dbg_level_info, L"Connection thread terminated (%d)\n", connection_id);
	}

	void dsi_connection::receive()
	{
		UINT16 bytes_in_buffer = 0;

		memset(receive_buffer_.get(), 0, RECV_BUFFER_SIZE);

	    do
		{
			const auto bytes_received = recv(
				socket_,
				&receive_buffer_[bytes_in_buffer],
				RECV_BUFFER_SIZE - bytes_in_buffer,
				0);

			if (bytes_received == 0)
			{
				// A zero return from recv() always means the peer has
				// closed its side of the connection, even if the client
				// told us it was going to sleep.
				DBGWRITE(dbg_level_info, L"0 bytes received, killing connection..\n");
				kill_connection();
				break;
			}

            if (bytes_received < 0)
            {
                DBGWRITE(dbg_level_error, L"Error from recv(): %d)\n", WSAGetLastError());
				kill_connection();
				break;
            }

			perf_counters::current()->increment_bytes_received(bytes_received);

            bytes_in_buffer += static_cast<UINT16>(bytes_received);

			if (bytes_in_buffer < dsi_header_size)
			{
				// We didn't get a full DSI header, go get more bits.
				continue; 
			}

			auto afp_data_length = ntohl(*reinterpret_cast<ULONG*>(&receive_buffer_[dsi_offset_data_len]));

			if (afp_data_length > RECV_BUFFER_SIZE - dsi_header_size)
			{
				// The client declared a payload that can never fit in our
				// receive buffer. Treat it as a malformed request.
				DBGWRITE(dbg_level_error, L"Invalid DSI data length in request header: %d\n", afp_data_length);
				kill_connection();
				break;
			}

			if (bytes_in_buffer < dsi_header_size + afp_data_length)
			{
				//We have a full DSI header, but we don't have all the afp payload.
				continue;
			}

			do
			{
				DBG_DUMP_BUFFER(receive_buffer_.get(), bytes_in_buffer, dbg_level_dump_in);

				// We have the entire DSI header and AFP payload.
				process_dsi_packet(receive_buffer_.get());

				// Check for connection closure.
				if (!continue_receive_)
				{
					bytes_in_buffer = 0;
					break;
				}

				// Process any overflow bits in the receive buffer.
				if (bytes_in_buffer > dsi_header_size + afp_data_length)
				{
					// Move the bits to the top of the receive buffer.
					bytes_in_buffer -= static_cast<UINT16>(dsi_header_size + afp_data_length);

					DBGWRITE(dbg_level_trace, L"Processing overflow bytes!!! (%d)\n", bytes_in_buffer);

					// Source and destination ranges can overlap here, so
					// this must be a memmove(), not a memcpy().
					memmove(
						receive_buffer_.get(),
						&receive_buffer_[dsi_header_size + afp_data_length],
						bytes_in_buffer);

					// If we already have a dsi header, see if we already
					// have the entire afp packet.
					if (bytes_in_buffer >= dsi_header_size)
					{
						afp_data_length = ntohl(*reinterpret_cast<ULONG*>(&receive_buffer_[dsi_offset_data_len]));

						if (afp_data_length > RECV_BUFFER_SIZE - dsi_header_size)
						{
							DBGWRITE(dbg_level_error, L"Invalid DSI data length in request header: %d\n", afp_data_length);
							kill_connection();
							bytes_in_buffer = 0;
							break;
						}
					}
				}
				else
				{
					bytes_in_buffer = 0;
				}
			} while (bytes_in_buffer >= dsi_header_size + afp_data_length);

            // If bytes_in_buffer is >0, then we have a partial read from
            // the next request.
		} while (continue_receive_ && bytes_in_buffer > 0);
	}

	void dsi_connection::process_dsi_packet(char* receive_buffer)
	{
		// Extract the DSI header parameters.
		afp_net_buffer header(receive_buffer, dsi_header_size);
		const auto dsi_flags = header.pull_num<INT8>();
		const auto dsi_command = header.pull_num<INT8>();
		const auto dsi_request_id = header.pull_num<UINT16>();
		header.advance_pos_ptr(sizeof(UINT32)); // Data offset field; not needed here.
		const auto dsi_data_length = header.pull_num<UINT32>();

		last_tickle_ = GetTickCount64();

		if (dsi_flags == dsi_request_flag && dsi_command != dsi_cmd_tickle)
		{
			DBGWRITE(dbg_level_info, L"Connection (%d) received DSI request: cmd=%u, id=%u, data_len=%u\n",
			         connection_id_, dsi_command, dsi_request_id, dsi_data_length);
		}

		switch(dsi_flags)
		{
		case dsi_request_flag:
			{
				const auto expected_request_id = adjust_client_request_id(dsi_request_id);
				if (expected_request_id != dsi_request_id)
				{
					DBGWRITE(
						dbg_level_error,
						L"DSI request id does not match expected value: %d expected, %d received.\n",
						expected_request_id,
						dsi_request_id);

					kill_connection();
					return;
				}

				const auto client_request_id = dsi_request_id;

				// AFP commands (carried by DSI Command/Write) must have at
				// least one byte of payload: the AFP command code itself.
				if ((dsi_command == dsi_cmd_command || dsi_command == dsi_cmd_write) &&
					dsi_data_length < sizeof(UINT8))
				{
					DBGWRITE(dbg_level_error, L"DSI command %d received with empty AFP payload.\n", dsi_command);
					kill_connection();
					return;
				}

				std::unique_ptr<char[]> reply_buffer;

				if (dsi_command != dsi_cmd_tickle)
				{
					reply_buffer = std::make_unique<char[]>(SEND_BUFFER_SIZE);
					if (reply_buffer == nullptr)
					{
						ASSERT(0);
						return;
					}
				}

				// Make sure that dsi_open_session() has been called before processing any other AFP commands.
				if (!dsi_session_open_ &&
					(dsi_command != dsi_cmd_open_session && dsi_command != dsi_cmd_get_status))
				{
					// If we are in a weird situation, and we received a tickle here,
					// we must consider that we don't have a return buffer to send an
					// error to the caller.
					if (dsi_command != dsi_cmd_tickle)
					{
						send_reply(reply_buffer.get(), dsi_command, client_request_id, afpParmErr, 0);
						ASSERT(0);
					}

					return;
				}

				switch (dsi_command)
				{
				case dsi_cmd_open_session:
					dsi_open_session(receive_buffer, reply_buffer.get(), client_request_id);
					break;

				case dsi_cmd_close_session:
					dsi_close_session(reply_buffer.get(), client_request_id);
					break;

				case dsi_cmd_command:
					dsi_process_command(receive_buffer, reply_buffer.get(), dsi_cmd_command, client_request_id, receive_buffer[dsi_offset_data_start]);
					break;

				case dsi_cmd_get_status:
					dsi_send_server_status(receive_buffer, reply_buffer.get(), client_request_id);
					break;

				case dsi_cmd_tickle:
					break;

				case dsi_cmd_write:
					dsi_process_command(receive_buffer, reply_buffer.get(), dsi_cmd_write, client_request_id, receive_buffer[dsi_offset_data_start]);
					break;

				default:
					ASSERT(0);
					break;
				}

				perf_counters::current()->increment_afp_requests(1);
			}

			if (dsi_command != dsi_cmd_tickle)
			{
				DBGWRITE(dbg_level_info, L"Connection (%d) reply sent: DSI cmd=%u, id=%u\n",
				         connection_id_, dsi_command, dsi_request_id);
			}
			break;

		case dsi_reply_flag:
			{
				// do nothing
			}
			break;

		default:
			DBGWRITE(dbg_level_error, L"Bad flag in DSI header: %d\n", dsi_flags);
			ASSERT(0);

			kill_connection();
		}
	}

	void dsi_connection::dsi_send_server_status(char* receive_buffer, char* reply_buffer, UINT16 client_request_id)
	{
		DBGWRITE(dbg_level_info, L"DSI get status command received.\n");

		afp_net_buffer req_buffer(&receive_buffer[dsi_offset_data_start], dsi_server_request_quantum_size);
		afp_net_buffer rep_buffer(&reply_buffer[dsi_offset_data_start], dsi_server_request_quantum_size);

		const auto afp_error = FPGetSrvrInfo(req_buffer, rep_buffer, this);

		send_reply(
			reply_buffer,
			dsi_cmd_get_status,
			client_request_id,
			afp_error,
			static_cast<INT32>(rep_buffer.get_data_length()));

		// Per the AFP spec, we close the connection immediately following
		// the GetStatus call.
		kill_connection();
	}

	void dsi_connection::dsi_open_session(char* receive_buffer, char* reply_buffer, UINT16 client_request_id)
	{
		DBGWRITE(dbg_level_info, L"DSI open session command received (cid: %d).\n", connection_id_);

		afp_net_buffer request(&receive_buffer[dsi_offset_data_start], dsi_server_request_quantum_size);
		const auto option = request.pull_num<INT8>();
		const auto option_size = request.pull_num<INT8>();

		if (option_size == sizeof(UINT32) && option == attention_quantum)
		{
			attention_quantum_size_ = request.pull_num<UINT32>();
			DBGWRITE(dbg_level_info, L"Attention quantum size: %d\n", attention_quantum_size_);
		}

		// Get a buffer wrapper for our AFP payload.
		afp_net_buffer reply(&reply_buffer[dsi_offset_data_start], dsi_server_request_quantum_size);
		reply.push_num<UINT8>(server_request_quantum);
		reply.push_num<UINT8>(sizeof(UINT32));
		reply.push_num<UINT32>(dsi_server_request_quantum_size);

		//Include the replay cache size option
		//reply.push_num<UINT8>(server_replay_cache_size);
		//reply.push_num<UINT8>(sizeof(INT32));
		//reply.push_num<UINT32>(replay_cache_size);

		send_reply(
			reply_buffer,
			dsi_cmd_open_session,
			client_request_id,
			AFP_OK,
			static_cast<INT32>(reply.get_data_length()));

		dsi_session_open_ = true;
	}

    void dsi_connection::dsi_close_session(char* reply_buffer, UINT16 client_request_id)
	{
		DBGWRITE(dbg_level_info, L"DSI close session command received.\n");

		dsi_session_open_ = false;

		send_reply(
			reply_buffer,
			dsi_cmd_close_session,
			client_request_id,
			AFP_OK,
			0);

		kill_connection();
	}

	void dsi_connection::dsi_process_command(
		char* receive_buffer, 
		char* reply_buffer, 
		const UINT8 dsi_cmd, 
		const UINT16 client_request_id, 
		const UINT8 afp_cmd)
	{
		// Get a wrapper for the AFP request payload.
		afp_net_buffer request(
			&receive_buffer[dsi_offset_data_start],
			ntohl(*reinterpret_cast<UINT32*>(&receive_buffer[dsi_offset_data_len])));
		request.set_dsi_data_offset(ntohl(*reinterpret_cast<UINT32*>(&receive_buffer[dsi_offset_data_offset])));

		// Get a wrapper for the AFP reply payload.
		afp_net_buffer reply(&reply_buffer[dsi_offset_data_start], SEND_BUFFER_SIZE);

        DBGWRITE(dbg_level_info, L"Connection (%d) dispatching AFP command %u\n", connection_id_, afp_cmd);
        const auto afp_error = fp_dispatch_command(request, reply, this);
		DBGWRITE(dbg_level_info, L"Connection (%d) AFP command %u returned %d\n",
		         connection_id_, afp_cmd, static_cast<int>(afp_error));
		const auto data_size = afp_cmd == afpRead || afp_cmd == afpReadExt
			? reply.get_dsi_return_value()
			: reply.get_data_length();

#ifdef _DEBUG
		if (AFP_FAILED(afp_error) && afp_error != afpEofError && afp_error != afpObjectNotFound && afp_error != afpItemNotFound)
		{
			DBGWRITE(dbg_level_error, L"AFP error: %d, for FP cmd: %d\n", afp_error, afp_cmd);
		}
#endif

		send_reply(
			reply_buffer,
			dsi_cmd,
			client_request_id,
			afp_error,
			static_cast<INT32>(data_size));

		if (!session_authenticated_ && afp_error != afpAuthContinue)
		{
			DBGWRITE(dbg_level_info, L"Authentication gone, killing DSI session\n");
			kill_connection();
		}
	}

	UINT16 dsi_connection::adjust_client_request_id(const UINT16 id_from_client)
	{
		if (client_request_id_ == dsi_max_request_id) {
			client_request_id_ = 0;
		}

		if (client_request_id_ == 0) {
			client_request_id_ = id_from_client;
		}
		else {
			++client_request_id_;
		}

		return client_request_id_;
	}

    UINT16 dsi_connection::adjust_server_request_id()
    {
		if (server_request_id_ == dsi_max_request_id)
		{
			server_request_id_ = 0;
		}
		else
		{
			++server_request_id_;
		}

        return server_request_id_;
    }

    void dsi_connection::send_(const char* buffer, const INT32 send_size)
    {
		// Can only send_ one at a time.
		std::lock_guard<std::mutex> guard(send_mutex_);

		INT32 total_sent = 0;

		while (total_sent < send_size)
		{
			const auto bytes_sent = send(socket_, &buffer[total_sent], send_size - total_sent, 0);

			if (bytes_sent == SOCKET_ERROR)
			{
				if (WSAGetLastError() == WSAEWOULDBLOCK)
				{
					// Should not happen on a blocking socket, but back off
					// and retry rather than spinning or dropping data.
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
					continue;
				}

				DBGWRITE(dbg_level_error, L"send() failed (%d)\n", WSAGetLastError());
				break;
			}

			total_sent += bytes_sent;
		}

		DBGWRITE(dbg_level_trace, L"Bytes sent: %d\n", total_sent);
		DBG_DUMP_BUFFER(buffer, send_size, dbg_level_dump_out);

		perf_counters::current()->increment_bytes_sent(total_sent);

		if (total_sent < send_size)
		{
			kill_connection();
		}
    }

	void dsi_connection::send_reply(
		char* reply_buffer,
		const UINT8 dsi_command,
		const UINT16 client_request_id,
		const INT32 afp_error,
		const INT32 afp_data_size)
	{
		afp_net_buffer header(reply_buffer, dsi_header_size + afp_data_size);
		header.push_num<UINT8>(dsi_reply_flag);
		header.push_num<UINT8>(dsi_command);
		header.push_num<UINT16>(client_request_id);
		header.push_num<INT32>(afp_error);
		header.push_num<UINT32>(afp_data_size);
		header.push_num<UINT32>(0);

		last_tickle_ = GetTickCount64();

		send_(reply_buffer, afp_data_size + dsi_header_size);
	}

    void dsi_connection::send_request(char* buffer, const UINT8 dsi_command, const INT32 afp_data_size)
    {
		const auto data_size = afp_data_size + dsi_header_size;

		afp_net_buffer header(buffer, data_size);
		header.push_num<UINT8>(dsi_request_flag);
		header.push_num<UINT8>(dsi_command);
		header.push_num(server_request_id_);
		header.push_num<UINT32>(0);
		header.push_num(afp_data_size);
		header.push_num<UINT32>(0);

		DBGWRITE(dbg_level_trace, L"Sending request, DSI command: %d, request id: %d\n", dsi_command, server_request_id_);

		adjust_server_request_id();

		send_(buffer, data_size);
    }

    void dsi_connection::send_tickle()
	{
        char reply_buffer[dsi_header_size] = {};

		DBGWRITE(dbg_level_trace, L"Sending tickle to connection (%d)\n", connection_id_);

		send_request(reply_buffer, dsi_cmd_tickle, 0);
		last_tickle_ = GetTickCount64();
	}

    void dsi_connection::send_attention(const UINT16 code)
    {
		char reply_buffer[dsi_header_size + sizeof(UINT16)] = {};

		afp_net_buffer reply(&reply_buffer[dsi_offset_data_start], sizeof(UINT16));
		reply.push_num(code);

		DBGWRITE(dbg_level_info, L"Sending attention: %x\n", code);

		send_request(reply_buffer, dsi_cmd_attention, sizeof(UINT16));
    }

    bool dsi_connection::client_lost() const
	{
		const auto elapsed_time = GetTickCount64() - last_tickle_;

		if ((client_sleeping_ && elapsed_time > client_lost_sleeping) ||
			(!client_sleeping_ && elapsed_time > client_lost_idle))
		{
			return true;
		}

		return false;
	}

	void dsi_connection::kill_connection()
	{
		// Make sure the kill sequence runs only once, even if this is
		// called from more than one thread (the connection thread and the
		// periodic monitoring thread can both get here).
		bool expected = true;
		if (!continue_receive_.compare_exchange_strong(expected, false))
		{
			return;
		}

		DBGWRITE(dbg_level_info, L"Killing connection (%d)\n", connection_id_);

		// shutdown() is safe to call while another thread is blocked in
		// recv() on this socket; it wakes that thread up. The socket
		// itself is closed by the owning connection thread (see
		// connection_thread), because closesocket() with a pending recv()
		// is not safe and the handle could be reused.
		if (GetCurrentThreadId() == owner_thread_id_)
		{
			// We are the connection thread; only a send-side shutdown is
			// needed so the client sees an orderly close. The socket is
			// drained and closed at the end of connection_thread().
			shutdown(socket_, SD_SEND);
		}
		else
		{
			// Another thread (e.g. the periodic monitor or server
			// shutdown) is killing us. Shut down both directions to wake
			// up any recv() blocked on the owning thread.
			shutdown(socket_, SD_BOTH);
		}
	}

	void dsi_connection::close_socket()
	{
		if (socket_closed_)
		{
			return;
		}

		socket_closed_ = true;
		DBGWRITE(dbg_level_info, L"Connection (%d) closing socket\n", connection_id_);

		shutdown(socket_, SD_SEND);

		// Read any remaining bits from the client so that it sees an
		// orderly closure. Use a short receive timeout and a bounded
		// iteration count so a client that keeps the connection open (or
		// floods us with data) cannot hold us here.
		DWORD timeout_ms = 1000;
		setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO,
			reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));

		int bytes_received;
		int drain_iterations = 0;
		do
		{
			bytes_received = recv(socket_, receive_buffer_.get(), RECV_BUFFER_SIZE, 0);
		} while (bytes_received > 0 && ++drain_iterations < 16);

		closesocket(socket_);
		socket_ = INVALID_SOCKET;
		DBGWRITE(dbg_level_info, L"Connection (%d) socket closed (drained %d iterations)\n",
		         connection_id_, drain_iterations);
	}

    afp_session* dsi_connection::get_afp_session() const
	{
	    return afp_session_.get();
	}
}
