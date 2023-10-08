#include "stdafx.h"
#include "afpdebug.h"
#include "dsi_connection.h"

#include "dsi_connections.h"

namespace dsi
{
	extern dsi_connections active_connections;

	dsi_connection::dsi_connection(const SOCKET socket, UINT32 connection_id)
		: socket_(socket)
		, connection_id_(connection_id)
		, continue_receive_(true)
		, client_request_id_(0)
		, server_request_id_(0)
	{
		DBGWRITE(dbg_level_info, L"Connection (%d) accepted", connection_id_);

		receive_buffer_ = std::make_unique<INT8[]>(RECV_BUFFER_SIZE);
		afp_session_ = new afp_session(nullptr);

		active_connections.track_connection(this);
	}

	dsi_connection::~dsi_connection()
	{
		active_connections.stop_tracking(this);
		kill_connection();
	}

	void dsi_connection::connection_thread()
	{
		receive();
		DBGWRITE(dbg_level_info, L"Connection thread terminated (%x)", socket_);
	}

	void dsi_connection::receive()
	{
		if (receive_buffer_ == nullptr || afp_session_ == nullptr)
		{
			DBGWRITE(dbg_level_error, L"Connection not properly initialized.");
			ASSERT(0);

			return;
		}

		while(continue_receive_)
		{
			
		}


	}

	UINT16 dsi_connection::expected_request_id(const UINT16 id_from_client)
	{
		if (client_request_id_ == DSI_MAX_REQUEST_ID) {
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
		reply_buffer[DSI_OFFSET_FLAGS] = DSI_REPLY_FLAG;
		reply_buffer[DSI_OFFSET_COMMAND] = static_cast<char>(dsi_command);

		// Insert the request ID and error code into the header.
		*reinterpret_cast<UINT16*>(&reply_buffer[DSI_OFFSET_REQUESTID]) = htons(client_request_id_);
		*reinterpret_cast<UINT32*>(&reply_buffer[DSI_OFFSET_ERRORCODE]) = htonl(afp_error);

		// Add the size of the AFP data we're including.
		*reinterpret_cast<UINT32*>(&reply_buffer[DSI_OFFSET_DATALEN]) = htonl(data_size);

		// This is the reserved field in the header, it must be zero.
		*reinterpret_cast<UINT32*>(&reply_buffer[DSI_OFFSET_RESERVED]) = 0;

		auto send_buffer = std::make_unique<UINT8>(SEND_BUFFER_SIZE);

		UINT32 offset = 0;
		INT32 bytes_sent = 0;
		INT32 bytes_size = data_size;

		last_tickle_ = GetTickCount64();

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
			DBGWRITE(dbg_level_error, L"send() failed (%d)", WSAGetLastError());
			kill_connection();
		}
	}

	void dsi_connection::send_tickle()
	{
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

		continue_receive_ = false;

		shutdown(socket_, SD_BOTH);
		closesocket(socket_);
	}
}
