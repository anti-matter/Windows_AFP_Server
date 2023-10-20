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
	}

	dsi_connection::~dsi_connection()
	{
		kill_connection();
		closesocket(socket_);

		DBGWRITE(dbg_level_info, L"dsi_connection closed.\n");
	}

	void dsi_connection::connection_thread()
	{
		const auto connection_id = connection_id_;

		// Loop thread to handle received packets.
		receive();

		// We remove from tracking here, which will delete this instance, so
		// that the thread has an opportunity to terminate gracefully.
		active_connections.stop_tracking(connection_id_);

		DBGWRITE(dbg_level_info, L"Connection thread terminated (%d)\n", connection_id);
	}

	void dsi_connection::receive()
	{
		if (receive_buffer_ == nullptr)
		{
			DBGWRITE(dbg_level_error, L"Connection not properly initialized.\n");
			ASSERT(0);

			return;
		}

		while(continue_receive_)
		{
			UINT16 bytes_in_buffer = 0;

			const auto bytes_received = recv(
				socket_,
				receive_buffer_.get(),
				RECV_BUFFER_SIZE - bytes_in_buffer,
				0);

			if (bytes_received == 0)
			{
				// Connection is closing
				kill_connection();
				break;
			}
			else if (bytes_received <= 0)
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
				process_dsi_packet();

				// Check for connection closure.
				if (!continue_receive_)
				{
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
						receive_buffer_.get() + (dsi_header_size + afp_data_length),
						bytes_in_buffer);

					// If we already have a dsi header, see if we already
					// have the entire afp packet.
					if (bytes_in_buffer >= dsi_header_size)
					{
						afp_data_length = ntohl(*reinterpret_cast<ULONG*>(&receive_buffer_[dsi_offset_data_len]));
					}
				}
			} while (bytes_in_buffer >= dsi_header_size + afp_data_length);
		}
	}

	void dsi_connection::process_dsi_packet()
	{
		// Extract the DSI header parameters.
		const auto dsi_flags = *reinterpret_cast<INT8*>(&receive_buffer_[dsi_offset_flags]);
		const auto dsi_command = *reinterpret_cast<INT8*>(&receive_buffer_[dsi_offset_command]);
		const auto dsi_request_id = ntohs(*reinterpret_cast<UINT16*>(&receive_buffer_[dsi_offset_request_id]));
		const auto dsi_data_offset = ntohl(*reinterpret_cast<UINT32*>(&receive_buffer_[dsi_offset_data_offset]));
		const auto dsi_data_length = ntohl(*reinterpret_cast<UINT32*>(&receive_buffer_[dsi_offset_data_len]));

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
					//ASSERT(0);

					//kill_connection();
					//return;
				}
				client_request_id_ = dsi_request_id;

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
						send_reply(reply_buffer.get(), dsi_command, afpParmErr, 0);
					}

					return;
				}

				switch (dsi_command)
				{
				case dsi_cmd_open_session:
					dsi_open_session(std::move(reply_buffer));
					break;

				case dsi_cmd_close_session:
					dsi_close_session(std::move(reply_buffer));
					break;

				case dsi_cmd_command:
					dsi_process_command(std::move(reply_buffer), dsi_cmd_command, receive_buffer_[dsi_offset_data_start]);
					break;

				case dsi_cmd_get_status:
					dsi_send_server_status(std::move(reply_buffer));
					break;

				case dsi_cmd_write:
					break;

				default:
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

	void dsi_connection::dsi_send_server_status(std::unique_ptr<char[]> reply_buffer)
	{
		DBGWRITE(dbg_level_info, L"DSI get status command received.\n");

		afp_net_buffer req_buffer(&receive_buffer_[dsi_offset_data_start], dsi_server_request_quantum_size);
		afp_net_buffer rep_buffer(&reply_buffer[dsi_offset_data_start], dsi_server_request_quantum_size);

		const auto afp_error = FPGetSrvrInfo(req_buffer, rep_buffer, this);

		send_reply(
			reply_buffer.get(),
			dsi_cmd_get_status,
			afp_error,
			rep_buffer.get_data_length());

		// Per the AFP spec, we close the connection immediately following
		// the GetStatus call.
		kill_connection();
	}

	void dsi_connection::dsi_open_session(std::unique_ptr<char[]> reply_buffer)
	{
		DBGWRITE(dbg_level_info, L"DSI open session command received (cid: %d).\n", connection_id_);

		//afp_net_buffer request(&receive_buffer_[dsi_offset_data_start], dsi_server_request_quantum_size);

		const auto option = receive_buffer_[dsi_offset_data_start];
		const auto option_size = receive_buffer_[dsi_offset_data_start+sizeof(BYTE)];

		if (option_size == sizeof(UINT32) && option == attention_quantum)
		{
			attention_quantum_size_ = (DWORD UNALIGNED)receive_buffer_[dsi_offset_data_start + sizeof(UINT16)];
			DBGWRITE(dbg_level_info, L"Attention quantum size: %d\n", attention_quantum_size_);
		}

		DBG_DUMP_BUFFER(receive_buffer_.get(), dsi_header_size + sizeof(UINT16) + sizeof(UINT32));

		// Get a buffer wrapper for out AFP payload.
		afp_net_buffer reply(&reply_buffer[dsi_offset_data_start], dsi_server_request_quantum_size);

		reply.push_num<UINT8>(server_request_quantum);
		reply.push_num<UINT8>(sizeof(UINT32));
		reply.push_num<UINT32>(dsi_server_request_quantum_size);

		send_reply(
			reply_buffer.get(),
			dsi_cmd_open_session,
			0,
			reply.get_data_length());

		dsi_session_open_ = true;
	}

    // ReSharper disable once CppParameterMayBeConstPtrOrRef
    void dsi_connection::dsi_close_session(std::unique_ptr<char[]>& reply_buffer)
	{
		DBGWRITE(dbg_level_info, L"DSI close sesssion command received.\n");

		dsi_session_open_ = false;

		send_reply(
			reply_buffer.get(),
			dsi_cmd_close_session,
			AFP_OK,
			0);

		kill_connection();
	}

	void dsi_connection::dsi_process_command(std::unique_ptr<char[]> reply_buffer, const UINT8 dsi_cmd, const UINT8 afp_cmd)
	{
		// Get a wrapper for the AFP request payload.
		afp_net_buffer request(&receive_buffer_[dsi_offset_data_start], dsi_server_request_quantum_size);
		request.set_dsi_data_offset(ntohl(*reinterpret_cast<UINT32*>(&receive_buffer_[DSI_OFFSET_DATAOFFSET])));

		// Get a wrapper for the AFP reply payload.
		afp_net_buffer reply(&reply_buffer[dsi_offset_data_start], dsi_server_request_quantum_size);

		const auto afp_error = fp_dispatch_command(request, reply, this);
		const auto data_size = afp_cmd == afpRead || afp_cmd == afpReadExt
			? request.get_dsi_return_value()
			: request.get_data_length();

		send_reply(
			reply_buffer.get(),
			dsi_cmd,
			afp_error,
			data_size
		);

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
			client_request_id_++;
		}

		return client_request_id_;
	}

	void dsi_connection::send_reply(
		char* reply_buffer,
		const UINT8 dsi_command,
		const INT32 afp_error,
		const INT32 data_size)
	{
		// We're replying, set the correct flag and command we're replying to.
		reply_buffer[dsi_offset_flags] = dsi_reply_flag;
		reply_buffer[dsi_offset_command] = static_cast<char>(dsi_command);

		// Insert the request ID and error code into the header.
		*reinterpret_cast<UINT16*>(&reply_buffer[dsi_offset_request_id]) = htons(client_request_id_);
		*reinterpret_cast<UINT32*>(&reply_buffer[dsi_offset_error_code]) = htonl(afp_error);

		// Add the size of the AFP data we're including.
		*reinterpret_cast<UINT32*>(&reply_buffer[dsi_offset_data_len]) = htonl(data_size);

		// This is the reserved field in the header, it must be zero.
		*reinterpret_cast<UINT32*>(&reply_buffer[dsi_offset_reserved]) = 0;

		UINT32 offset = 0;
		INT32 bytes_sent;
		INT32 bytes_size = data_size + dsi_header_size;

		last_tickle_ = GetTickCount64();

		DBG_DUMP_BUFFER(reply_buffer, bytes_size);

		do
		{
			bytes_sent = send(socket_, reply_buffer, bytes_size, 0);

			if (bytes_sent < 0)
			{
				if (errno == EWOULDBLOCK)
				{
					continue;
				}

				break;
			}

			if (bytes_sent < data_size)
			{
				offset += bytes_sent;
				bytes_size -= bytes_sent;

				continue;
			}

			break;

		} while (true);

		if (bytes_sent < 0)
		{
			DBGWRITE(dbg_level_error, L"send() failed (%d)\n", WSAGetLastError());
			kill_connection();
		}
	}

	void dsi_connection::send_tickle()
	{
		send_reply(nullptr, dsi_cmd_tickle, AFP_OK, 0);
		last_tickle_ = GetTickCount64();
	}

    void dsi_connection::send_attention(UINT16 code)
    {
		UINT16 swapped_code = htons(code);
		send_reply(reinterpret_cast<char*>(&swapped_code), dsi_cmd_attention, AFP_OK, sizeof(UINT16));
    }

    bool dsi_connection::client_lost() const
	{
		if (GetTickCount64() - last_tickle_ > AFP_CLIENT_LOST)
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

		do
		{
			bytes_received = recv(socket_, receive_buffer_.get(), RECV_BUFFER_SIZE, 0);
		} while (bytes_received > 0);

		closesocket(socket_);
	}

    afp_session* dsi_connection::get_afp_session() const
	{
	    return afp_session_.get();
	}

    void dsi_connection::make_afp_session()
    {
		afp_session_ = std::make_unique<afp_session>(this);
    }

    void dsi_connection::free_afp_session()
	{
	    afp_session_ = nullptr;
	}
}
