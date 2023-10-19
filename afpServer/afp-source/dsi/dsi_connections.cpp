#include "stdafx.h"
#include <thread>

#include "dsi_connections.h"
#include "afpdebug.h"
#include "afp_session.h"


dsi::dsi_connections active_connections;

namespace dsi
{
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

    dsi_connections::~dsi_connections()
    {
		std::lock_guard<std::mutex> guard(connections_mutex_);

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

	afp_session* dsi_connections::find_afp_session_with_id(const char* afp_id, const UINT32 id_size)
	{
		std::lock_guard<std::mutex> guard(connections_mutex_);

		for (const auto& connection : connections_)
		{
			const auto afp_object = connection->get_afp_session();
			if (afp_object != nullptr)
			{
				const auto afp_object_id = afp_object->get_afp_id();
				if (afp_id != nullptr && id_size == afp_object->get_afp_id_size() &&
					!memcmp(afp_id, afp_object_id, id_size))
				{
					return afp_object;
				}
			}
		}

		return nullptr;
	}

	afp_session* dsi_connections::find_afp_session_with_token(const UINT32 token)
	{
		std::lock_guard<std::mutex> guard(connections_mutex_);

		for (const auto& connection : connections_)
		{
			const auto afp_object = connection->get_afp_session();
			if (afp_object != nullptr)
			{
				if (afp_object->get_token() == token)
				{
					return afp_object;
				}
			}
		}

		return nullptr;
	}

    void dsi_connections::broadcast_attention(const UINT16 code)
    {
		std::lock_guard<std::mutex> guard(connections_mutex_);

		for (const auto& connection : connections_)
		{
			connection->send_attention(code);
		}
    }

    UINT16 dsi_connections::count(const bool authenticated_only)
    {
		std::lock_guard<std::mutex> guard(connections_mutex_);

		UINT16 result = 0;

		for (const auto& connection : connections_)
		{
		    if (authenticated_only && connection->get_is_session_authenticated())
		    {
				result++;
				continue;
		    }

			result++;
		}

		return result;
    }
}
