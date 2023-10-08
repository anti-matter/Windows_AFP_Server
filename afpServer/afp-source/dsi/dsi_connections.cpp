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

	void dsi_connections::track_connection(dsi_connection* connection)
	{
		if (connection == nullptr)
		{
			return;
		}

		std::lock_guard<std::mutex> guard(connections_mutex_);
		connections_.push_back(connection);
	}

	void dsi_connections::stop_tracking(const dsi_connection* connection)
	{
		if (connection == nullptr)
		{
			return;
		}

		std::lock_guard<std::mutex> guard(connections_mutex_);
		const auto itr = std::remove_if(
			connections_.begin(), 
			connections_.end(), 
			[&](const dsi_connection* c) {return c == connection; });

		connections_.erase(itr, connections_.end());
	}

	void dsi_connections::periodic_thread()
	{
		std::lock_guard<std::mutex> guard(connections_mutex_);

		for (const auto connection : connections_)
		{
			// Check if we need to tickle the client.
			if (connection->tickle_required())
			{
				connection->send_tickle();
			}

			if (connection->client_lost())
			{
				connection->kill_connection();
				delete connection;
			}
		}
	}
}
