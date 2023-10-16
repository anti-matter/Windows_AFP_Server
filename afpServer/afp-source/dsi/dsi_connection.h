#pragma once
#include <winsock2.h>

#include "afpdhxlogin.h"
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
	class dsi_connection  // NOLINT(cppcoreguidelines-special-member-functions)
	{
	public:
		dsi_connection(SOCKET socket, UINT32 connection_id);
		~dsi_connection();

		void connection_thread();
		void receive();
		void process_dsi_packet();

		void dsi_send_server_status(std::unique_ptr<char[]> reply_buffer);
		void dsi_open_session(std::unique_ptr<char[]> reply_buffer);
		void dsi_close_session(std::unique_ptr<char[]> reply_buffer);
		void dsi_process_command(std::unique_ptr<char[]> reply_buffer, UINT8 dsi_cmd, UINT8 afp_cmd);

		UINT16 adjust_client_request_id(UINT16 id_from_client);

		void send_reply(std::unique_ptr<char[]> reply_buffer, UINT8 dsi_command, INT32 afp_error, UINT32 data_size);
		void send_tickle();

		bool client_lost() const;
		void kill_connection();

		// Accessors
		bool tickle_required() const { return GetTickCount64() - last_tickle_ > AFP_TICKLE_TIME; }
		void set_sleep_state(const bool sleeping) { client_sleeping_ = sleeping;  }
		bool get_is_client_sleeping() const { return client_sleeping_; }

		UINT32 get_connection_id() const { return connection_id_; }

		HANDLE get_user_token() const { return user_token_; }
		void set_user_token(const HANDLE token) { user_token_ = token; }
		void set_session_authenticated(const bool authenticated) { session_authenticated_ = authenticated; }
		bool get_is_session_authenticated() const { return session_authenticated_; }

		void set_afp_version(const UINT8 version) { afp_version_ = version; }
		UINT8 get_afp_version() const { return afp_version_; }

		// Logon data accessors.
		void set_uam_type(const INT8 version) { uam_type_ = version; }
		INT8 get_uam_type() const { return uam_type_; }
		void set_user_name(const std::wstring& user_name) { user_name_ = user_name; }
		std::wstring get_user_name() const { return user_name_; }
		void set_domain_name(const std::wstring& domain_name) { domain_name_ = domain_name; }
		std::wstring get_domain_name() const { return domain_name_; }

		void set_extended_logon_info(std::unique_ptr<DHXInfo>& data) { extended_logon_info_ = std::move(data); }
		DHXInfo* get_extended_logon_info() const { return extended_logon_info_.get(); }
		void free_extended_logon_info() { extended_logon_info_ = nullptr; }

		afp_session* get_afp_session() const { return afp_session_.get(); }
		void make_afp_session() { afp_session_ = std::make_unique<afp_session>(this); }
		void free_afp_session() { afp_session_ = nullptr; }

	private:
		SOCKET socket_;
		UINT32 connection_id_;
		UINT32 attention_quantum_size_;
		std::unique_ptr<char[]> receive_buffer_;
		bool continue_receive_;
		bool dsi_session_open_;

		std::unique_ptr<afp_session> afp_session_;

		UINT16 client_request_id_;
		UINT16 server_request_id_;

		ULONGLONG last_tickle_;

		// This is for the new FPZzzz command which tells the server
		// when the client is going to sleep.
		bool client_sleeping_;

		HANDLE user_token_;
		bool session_authenticated_;

		UINT8 afp_version_;

		INT8 uam_type_;
		std::unique_ptr<DHXInfo> extended_logon_info_;

		std::wstring user_name_;
		std::wstring domain_name_;
	};
}
