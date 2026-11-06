#pragma once
#include <atomic>
#include <mutex>
#include <winsock2.h>

enum dsi_option_fields
{
	server_request_quantum = 0x00,
	attention_quantum = 0x01,
	server_replay_cache_size = 0x02
};

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

enum dsi_attention_flags
{
    dsi_attention_server_shutdown = 0x8000,
	dsi_attention_user_disconnect = 0x9000,
	dsi_attention_server_message = 0x2000,
	dsi_attention_server_notify = 0x3001,
	dsi_attention_time_mask = 0x0FFF
};

constexpr UINT16 dsi_max_request_id = 65535;
constexpr UINT32 dsi_server_request_quantum_size = MAXSHORT - dsi_header_size;
constexpr UINT32 replay_cache_size = 32;

#define RECV_BUFFER_SIZE (MAXWORD)
#define SEND_BUFFER_SIZE (MAXWORD)

constexpr UINT32 tickle_time = (1000 * 15);
constexpr UINT32 client_lost_idle = (1000 * 120);
constexpr UINT32 client_lost_sleeping = (1000 * 60 * 60 * 24);

class afp_session;

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
		void process_dsi_packet(char* receive_buffer);

		void dsi_send_server_status(char* receive_buffer, char* reply_buffer, UINT16 client_request_id);
		void dsi_open_session(char* receive_buffer, char* reply_buffer, UINT16 client_request_id);
		void dsi_close_session(char* reply_buffer, UINT16 client_request_id);
		void dsi_process_command(char* receive_buffer, char* reply_buffer, UINT8 dsi_cmd, UINT16 client_request_id, UINT8 afp_cmd);

		UINT16 adjust_client_request_id(UINT16 id_from_client);
		UINT16 adjust_server_request_id();

        void send_(const char* buffer, INT32 send_size);
		void send_reply(char* reply_buffer, UINT8 dsi_command, UINT16 client_request_id, INT32 afp_error, INT32 afp_data_size);
		void send_request(char* buffer, UINT8 dsi_command, INT32 afp_data_size);
		void send_tickle();
		void send_attention(UINT16 code);

		bool client_lost() const;
		void kill_connection();

		// Accessors
		bool tickle_required() const { return GetTickCount64() - last_tickle_ > tickle_time; }
		void set_sleep_state(const bool sleeping) { client_sleeping_ = sleeping;  }
		bool get_is_client_sleeping() const { return client_sleeping_; }

		UINT32 get_connection_id() const { return connection_id_; }

		HANDLE get_user_token() const { return user_token_; }
		void set_user_token(const HANDLE token)
		{
			// We own the previous token handle; close it before replacing
			// so re-logon does not leak the old handle.
			if (token == user_token_)
			{
				return;
			}

			if (user_token_ != nullptr)
			{
				CloseHandle(user_token_);
			}

			user_token_ = token;
		}
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

		void set_extended_logon_info(void* data) { extended_logon_info_ = data; }
		void* get_extended_logon_info() const { return extended_logon_info_; }
		// The blob is allocated with new (see afpuser.cpp), so it must be
		// released with operator delete, not free().
		void free_extended_logon_info() { operator delete(extended_logon_info_); extended_logon_info_ = nullptr; }

		afp_session* get_afp_session() const;

		// dsi_connections closes the socket (on the connection's own
		// thread, from stop_tracking) while holding the tracking lock so
		// that no other thread can be sending on the socket when it is
		// closed.
		friend class dsi_connections;

    private:
		// Performs an orderly shutdown of the socket and closes it.
		// Must only be called from the connection's own thread, because
		// closesocket() on a socket that another thread is blocked in
		// recv() on is not safe.
		void close_socket();

		SOCKET socket_;
		bool socket_closed_;
		std::atomic<DWORD> owner_thread_id_;
		UINT32 connection_id_;
		UINT32 attention_quantum_size_;
		std::atomic_bool continue_receive_;
		bool dsi_session_open_;

		std::unique_ptr<char[]> receive_buffer_;
		std::unique_ptr<afp_session> afp_session_;

		UINT16 client_request_id_;
		UINT16 server_request_id_;

		ULONGLONG last_tickle_;

		// This is for the new FPZzzz task which tells the server
		// when the client is going to sleep.
		bool client_sleeping_;

		HANDLE user_token_;
		bool session_authenticated_;

		UINT8 afp_version_;

		INT8 uam_type_;
		void* extended_logon_info_;

		std::wstring user_name_;
		std::wstring domain_name_;

		std::mutex send_mutex_;
	};
}
