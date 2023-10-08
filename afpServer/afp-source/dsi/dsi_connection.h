#pragma once
#include <winsock2.h>

#include "afp_session.h"

// This is the largest request that the server can receive
// from the client. It does not include the DSI header size.
#define SRVR_REQUEST_QUANTUM_SIZE	(MAXSHORT-dsi_header_size)

// Offsets for the DSI header fields
enum
{
	dsi_offset_flags = 0,
	dsi_offset_command = 1,
	dsi_offset_request_id = 2,
	dsi_offset_error_code = 4,
	dsi_offset_data_offset = 4,
	dsi_offset_data_len = 8,
	dsi_offset_reserved = 12,
	dsi_offset_data_start = 16,
	dsi_header_size = 16,

	overflow_buffer_size = 4096
};

#define RECV_BUFFER_SIZE		(MAXWORD) 
#define SEND_BUFFER_SIZE		(MAXWORD)

namespace dsi
{
	// This class manages the network connection and data flow between
	// the server and client. Here we process the incoming requests
	// and hand off the afp packet to the afp engine for action.
	class dsi_connection
	{
	public:
		dsi_connection(SOCKET socket, UINT32 connection_id);
		~dsi_connection();

		void connection_thread();
		void receive();
		UINT16 expected_request_id(UINT16 id_from_client);

		void send_reply(char* reply_buffer, UINT8 dsi_command, INT32 afp_error, INT32 data_size);
		void send_tickle();

		bool client_lost() const;
		void kill_connection();

		// Accessors
		bool tickle_required() const { return GetTickCount64() - last_tickle_ > AFP_TICKLE_TIME; }
		void set_sleep_state(const bool sleeping) { client_sleeping_ = sleeping;  }

	private:
		SOCKET socket_;
		UINT32 connection_id_;
		std::unique_ptr<INT8[]> receive_buffer_;
		bool continue_receive_;

		afp_session* afp_session_;

		UINT16 client_request_id_;
		UINT16 server_request_id_;

		ULONGLONG last_tickle_;
		bool client_sleeping_;
	};
}
