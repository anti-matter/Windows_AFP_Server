#pragma once
#include <mutex>

#include "dsi_connection.h"

namespace dsi
{
	#define AFP_TICKLE_TIME				(1000*15)
	#define AFP_CLIENT_LOST				(1000*120)
	#define AFP_CLIENT_LOST_SLEEPING	(1000*60*60*24)
	#define RECV_POSTED					1
	#define SEND_POSTED					2
	#define ARBI_POSTED					3
	#define POST_SEND					1
	#define POST_RECV					2
	#define NO_ACTION					0
	#define SEND_BUF_SIZE				MAXWORD
	#define OVER_BUF_SIZE				4096
	#define	RECV_BUF_SIZE				MAXWORD
	#define DATA_BUF_SIZE				(RECV_BUF_SIZE+DSI_HEADER_SIZE)
	#define SCAVENGE_THREAD_SLEEP		5000 //scavenger scavenges every 5 seconds

	// Class that manages all dsi connections that are open.
	class dsi_connections  // NOLINT(cppcoreguidelines-special-member-functions)
	{
	public:
		dsi_connections();
		~dsi_connections();

		void track_connection(std::unique_ptr<dsi_connection>& connection);
		void stop_tracking(UINT32 connection_id);

		// Server is shutting down.
		void close_all();

		void periodic_thread();

		afp_session* find_afp_session_with_id(const char* afp_id, const UINT32 id_size);
		afp_session* find_afp_session_with_token(const UINT32 token);

		void broadcast_attention(UINT16 code);
		UINT16 count(bool authenticated_only = false);

	private:
		std::vector<std::unique_ptr<dsi_connection>> connections_;
		std::mutex connections_mutex_;
	};
}

