#pragma once
#include <winsock2.h>

#include "afp_session.h"

// This is the largest request that the server can receive
// from the client. It does not include the DSI header size.
#define SRVR_REQUEST_QUANTUM_SIZE	(MAXSHORT-dsi_header_size)

// DSIOpen option fields
enum dsi_option_fields
{
	server_request_quantum = 0x00,
	attention_quantum = 0x01
};

// Offsets for the DSI header fields
enum dsi_header_offsets
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

enum dsi_net_flags
{
	dsi_request_flag = 0x00,
	dsi_reply_flag = 0x01
};

enum dsi_commands
{
	dsi_cmd_close_session = 1,
	dsi_cmd_command,
	dsi_cmd_get_status,
	dsi_cmd_open_session,
	dsi_cmd_tickle,
	dsi_cmd_write,
	dsi_cmd_attention = 8
};

constexpr UINT16 dsi_max_request_id = 65535;
constexpr UINT32 dsi_server_request_quantum_size = MAXSHORT - dsi_header_size;

#define RECV_BUFFER_SIZE		(MAXSHORT) 
#define SEND_BUFFER_SIZE		(MAXSHORT)

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
		void process_dsi_packet();

		void dsi_send_status(std::unique_ptr<char[]> reply_buffer);
		void dsi_open_session(std::unique_ptr<char[]> reply_buffer);
		void dsi_close_session(std::unique_ptr<char[]> reply_buffer);

		UINT16 adjust_client_request_id(UINT16 id_from_client);

		void send_reply(std::unique_ptr<char[]> reply_buffer, UINT8 dsi_command, INT32 afp_error, INT32 data_size);
		void send_tickle();

		bool client_lost() const;
		void kill_connection();

		// Accessors
		bool tickle_required() const { return GetTickCount64() - last_tickle_ > AFP_TICKLE_TIME; }
		void set_sleep_state(const bool sleeping) { client_sleeping_ = sleeping;  }

	private:
		SOCKET socket_;
		UINT32 connection_id_;
		UINT32 attention_quantum_size_;
		std::unique_ptr<char[]> receive_buffer_;
		bool continue_receive_;
		bool dsi_session_open_;

		afp_session* afp_session_;

		UINT16 client_request_id_;
		UINT16 server_request_id_;

		ULONGLONG last_tickle_;
		bool client_sleeping_;
	};
}
