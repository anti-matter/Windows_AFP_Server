#include "stdafx.h"
#include <thread>

#include "dsi_connections.h"


namespace dsi
{
	dsi_connections active_connections;
	bool connections_initialized = false;

	dsi_connections::dsi_connections()
	{
		if (connections_initialized)
		{
			ASSERT(0);
			return;
		}

		connections_initialized = true;

		std::thread periodic(&dsi_connections::periodic_thread, this);
		periodic.detach();
	}

	void dsi_connections::track_connection(std::unique_ptr<dsi_connection>& connection)
	{
		std::lock_guard<std::mutex> guard(connections_mutex_);
		connections_.push_back(std::move(connection));
	}

	void dsi_connections::stop_tracking(UINT32 connection_id)
	{
		std::lock_guard<std::mutex> guard(connections_mutex_);
		const auto itr = std::remove_if(
			connections_.begin(),
			connections_.end(),
			[&](const std::unique_ptr<dsi_connection>& c) {return c->get_connection_id() == connection_id;});

		if (itr != connections_.end())
		{
			connections_.erase(itr, connections_.end());
		}
	}

	void dsi_connections::periodic_thread()
	{
		std::lock_guard<std::mutex> guard(connections_mutex_);

		//for (const auto connection : connections_)
		//{
		//	// Check if we need to tickle the client.
		//	if (connection->tickle_required())
		//	{
		//		connection->send_tickle();
		//	}

		//	if (connection->client_lost())
		//	{
		//		connection->kill_connection();
		//		stop_tracking(connection->get_connection_id());
		//	}
		//}
	}
}
