#include "stdafx.h"
#include "afpdebug.h"
#include "afpsrvr.h"
#include "dsi_connection.h"
#include "afpfileio.h"
#include "afpdispatch.h"
#include "afpdhxlogin.h"
#include "afp_session.h"
#include "dsi_connections.h"

extern dsi::dsi_connections active_connections;

namespace dsi
{
	dsi_connection::dsi_connection(const SOCKET socket, const UINT32 connection_id)
		: socket_(socket)
		  , connection_id_(connection_id)
		  , attention_quantum_size_(0)
		  , continue_receive_(true)
		  , dsi_session_open_(false)
		  , client_request_id_(0)
		  , server_request_id_(0)
		  , last_tickle_(0)
		  , client_sleeping_(false)
		  , user_token_(nullptr)
		  , session_authenticated_(false)
		  , afp_version_(0)
		  , uam_type_(0)
		  , extended_logon_info_(nullptr)
	{
		DBGWRITE(dbg_level_info, L"Connection (%d) accepted\n", connection_id_);

		receive_buffer_ = std::make_unique<char[]>(RECV_BUFFER_SIZE);
		afp_session_ = std::make_unique<afp_session>(this);
	}

	dsi_connection::~dsi_connection()
	{
		DBGWRITE(dbg_level_info, L"dsi_connection closed.\n");
	}

    void dsi_connection::connection_thread()
	{
#ifdef _DEBUG
        const auto connection_id = connection_id_;
#endif

		while(continue_receive_)
		{
			receive();
		}

		// We remove from tracking here, which will delete this instance, so
		// that the thread has an opportunity to terminate gracefully.
		active_connections.stop_tracking(connection_id_);

		DBGWRITE(dbg_level_info, L"Connection thread terminated (%d)\n", connection_id);
	}

	void dsi_connection::receive()
	{
		UINT16 bytes_in_buffer = 0;

	    do
		{
			const auto bytes_received = recv(
				socket_,
				&receive_buffer_[bytes_in_buffer],
				RECV_BUFFER_SIZE - bytes_in_buffer,
				0);

			if (bytes_received == 0)
			{
				DBGWRITE(dbg_level_info, L"0 bytes received, killing connection..\n");
				kill_connection();
				break;
			}

            if (bytes_received < 0)
            {
                DBGWRITE(dbg_level_error, L"Error from recv(): %d)\n", WSAGetLastError());
                continue;
            }

            bytes_in_buffer += static_cast<UINT16>(bytes_received);

			if (bytes_in_buffer < dsi_header_size)
			{
				// We didn't get a full DSI header, go get more bits.
				continue; 
			}

			ULONG afp_data_length = ntohl(*reinterpret_cast<ULONG*>(&receive_buffer_[dsi_offset_data_len]));

			if (bytes_in_buffer < dsi_header_size + afp_data_length)
			{
				//We have a full DSI header, but we don't have all the afp payload.
				continue;
			}

			do
			{
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

					DBGWRITE(dbg_level_info, L"Processing overflow bytes!!! (%d)\n", bytes_in_buffer);

					memcpy(
						receive_buffer_.get(),
						&receive_buffer_[dsi_header_size + afp_data_length],
						bytes_in_buffer);

					// If we already have a dsi header, see if we already
					// have the entire afp packet.
					if (bytes_in_buffer >= dsi_header_size)
					{
						afp_data_length = ntohl(*reinterpret_cast<ULONG*>(&receive_buffer_[dsi_offset_data_len]));
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
		//const auto dsi_data_offset = header.pull_num<UINT32>();
		//const auto dsi_data_length = header.pull_num<UINT32>();

		last_tickle_ = GetTickCount64();

		switch(dsi_flags)
		{
		case dsi_request_flag:
			{
				const auto expected_request_id = adjust_client_request_id(dsi_request_id);
				if (expected_request_id != dsi_request_id)
				{
					DBGWRITE(
						dbg_level_error,
						L"DSI request id does not match expected value: %d expected, %d recevied.\n",
						expected_request_id,
						dsi_request_id);

					kill_connection();
					return;
				}

				const auto client_request_id = dsi_request_id;

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
					// If we are in a weird situation and we received a tickle here,
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

		// Get a buffer wrapper for out AFP payload.
		afp_net_buffer reply(&reply_buffer[dsi_offset_data_start], dsi_server_request_quantum_size);
		reply.push_num<UINT8>(server_request_quantum);
		reply.push_num<UINT8>(sizeof(UINT32));
		reply.push_num<UINT32>(dsi_server_request_quantum_size);

		//Include the replay cache size option
		//reply.push_num<UINT8>(server_replay_cache_size);
		//reply.push_num<UINT8>(sizeof(int32));
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
		DBGWRITE(dbg_level_info, L"DSI close sesssion command received.\n");

		dsi_session_open_ = false;

		send_reply(
			reply_buffer,
			dsi_cmd_close_session,
			client_request_id,
			AFP_OK,
			0);

		kill_connection();
	}

	void dsi_connection::dsi_process_command(char* receive_buffer, char* reply_buffer, UINT8 dsi_cmd, UINT16 client_request_id, UINT8 afp_cmd)
	{
		// Get a wrapper for the AFP request payload.
		afp_net_buffer request(&receive_buffer[dsi_offset_data_start], dsi_server_request_quantum_size);
		request.set_dsi_data_offset(ntohl(*reinterpret_cast<UINT32*>(&receive_buffer[dsi_offset_data_offset])));

		// Get a wrapper for the AFP reply payload.
		afp_net_buffer reply(&reply_buffer[dsi_offset_data_start], dsi_server_request_quantum_size);

        DBGWRITE(dbg_level_info, L"Processing AFP cmd: %d\n", afp_cmd);

        const auto afp_error = fp_dispatch_command(request, reply, this);
		const auto data_size = afp_cmd == afpRead || afp_cmd == afpReadExt
			? reply.get_dsi_return_value()
			: reply.get_data_length();

		if (AFP_FAILED(afp_error))
		{
			DBGWRITE(dbg_level_error, L"AFP error: %d\n", afp_error);
		}

		send_reply(
			reply_buffer,
			dsi_cmd,
			client_request_id,
			afp_error,
			static_cast<INT32>(data_size));

		if (!session_authenticated_ && afp_error != afpAuthContinue)
		{
			DBGWRITE(dbg_level_info, L"Authencation gone, killing DSI session\n");
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

    UINT16 dsi_connection::get_next_request_id()
    {
        server_request_id_ = server_request_id_ == dsi_max_request_id
                                 ? 0
                                 : server_request_id_++;

        return server_request_id_;
    }

    void dsi_connection::send_(const char* buffer, const INT32 send_size)
    {
		// Can only send_ one at a time.
		std::lock_guard<std::mutex> guard(send_mutex_);

		UINT32 offset = 0;
		INT32 bytes_sent;
		INT32 bytes_size = send_size;

		do
		{
			bytes_sent = send(socket_, &buffer[offset], bytes_size, 0);

			if (bytes_sent < 0)
			{
				if (errno == EWOULDBLOCK)
				{
					continue;
				}

				break;
			}

			if (bytes_sent < send_size)
			{
				offset += bytes_sent;
				bytes_size -= bytes_sent;

				continue;
			}

			break;

		} while (true);

		DBGWRITE(dbg_level_info, L"Bytes sent: %d\n", bytes_sent);

		if (bytes_sent < 0)
		{
			DBGWRITE(dbg_level_error, L"send() failed (%d)\n", WSAGetLastError());
			kill_connection();
		}
    }

	void dsi_connection::send_reply(
		char* reply_buffer,
		const UINT8 dsi_command,
		const UINT16 client_request_id,
		const INT32 afp_error,
		const INT32 data_size)
	{
		afp_net_buffer header(reply_buffer, dsi_header_size + data_size);
		header.push_num<UINT8>(dsi_reply_flag);
		header.push_num<UINT8>(dsi_command);
		header.push_num<UINT16>(client_request_id);
		header.push_num<INT32>(afp_error);
		header.push_num<UINT32>(data_size);
		header.push_num<UINT32>(0);

		last_tickle_ = GetTickCount64();

		send_(reply_buffer, data_size + dsi_header_size);
	}

    void dsi_connection::send_request(char* buffer, const UINT8 dsi_command, const INT32 afp_data_size)
    {
		const auto data_size = afp_data_size + dsi_header_size;

		afp_net_buffer header(buffer, data_size);
		header.push_num<UINT8>(dsi_request_flag);
		header.push_num<UINT8>(dsi_command);
		header.push_num(get_next_request_id());
		header.push_num<UINT32>(0);
		header.push_num(afp_data_size);
		header.push_num<UINT32>(0);

		DBGWRITE(dbg_level_info, L"Sending request, DSI command: %d\n", dsi_command);

		send_(buffer, data_size);
    }

    void dsi_connection::send_tickle()
	{
        char reply_buffer[dsi_header_size] = {};

		DBGWRITE(dbg_level_info, L"Sending tickle to connection (%d)\n", connection_id_);

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
		if (GetTickCount64() - last_tickle_ > client_lost_idle)
		{
			if (!client_sleeping_)
			{
				return true;
			}
		}

		return false;
	}

	void dsi_connection::kill_connection()
	{
		if (!continue_receive_)
		{
			return;
		}

		DBGWRITE(dbg_level_info, L"Killing connection (%d)\n", connection_id_);

		continue_receive_ = false;

		shutdown(socket_, SD_SEND);

		// Call recv() to ensure socket is shutdown before closing.
		int bytes_received;
        const auto receive_buffer = std::make_unique<char[]>(RECV_BUFFER_SIZE);

		do
		{
			bytes_received = recv(socket_, receive_buffer.get(), RECV_BUFFER_SIZE, 0);
		} while (bytes_received > 0);

		closesocket(socket_);
	}

    afp_session* dsi_connection::get_afp_session() const
	{
	    return afp_session_.get();
	}
}
