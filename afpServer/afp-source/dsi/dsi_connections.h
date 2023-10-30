#pragma once
#include <mutex>

#include "dsi_connection.h"

namespace dsi
{
	constexpr LONGLONG periodic_thread_sleep_seconds = 5;

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
		std::recursive_mutex connections_mutex_;
		std::atomic_bool connection_running_;
	};
}

