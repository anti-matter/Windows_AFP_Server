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

		std::thread periodic(&periodic_thread, this);
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

	void dsi_connections::stop_tracking(dsi_connection* connection)
	{
		if (connection == nullptr)
		{
			return;
		}

		std::lock_guard<std::mutex> guard(connections_mutex_);

		const auto it = std::find(
			connections_.begin(),
			connections_.end(),
			[connection](const dsi_connection* test)
			{
				return test && test == connection;
			});

		if (it != connections_.end())
		{
			connections_.erase(it);
		}
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
