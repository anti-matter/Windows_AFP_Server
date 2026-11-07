#include "stdafx.h"
#include <thread>
#include <algorithm>

#include "dsi_connections.h"

#include <afpEvents.h>

#include "afpdebug.h"
#include "afp_session.h"
#include "power_config.h"


dsi::dsi_connections active_connections;
power_config power_manage;

namespace dsi
{
	bool connections_initialized = false;

	dsi_connections::dsi_connections() : connection_running_(true)
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

    dsi_connections::~dsi_connections() = default;

    void dsi_connections::track_connection(std::unique_ptr<dsi_connection>& connection)
	{
		DBGWRITE(dbg_level_info, L"Tracking connection (%u), waiting on tracking lock\n", connection->get_connection_id());
		std::lock_guard guard(connections_mutex_);
		connections_.push_back(std::move(connection));
		DBGWRITE(dbg_level_info, L"Tracking connection done, %u active\n", static_cast<unsigned int>(connections_.size()));

		// Tell the power manager that we need system power since we have
		// active connections to our server.
		power_manage.power_system_required();
	}

	void dsi_connections::stop_tracking(const UINT32 connection_id)
	{
		DBGWRITE(dbg_level_info, L"Stop tracking connection (%u), waiting on tracking lock\n", connection_id);
		std::lock_guard guard(connections_mutex_);
		const auto itr = std::ranges::find_if(connections_, [&](const std::unique_ptr<dsi_connection>& c)
        {
            return c->get_connection_id() == connection_id;
        });

		if (itr != connections_.end())
		{
			DBGWRITE(dbg_level_info, L"Stopping tracking of connection: %d\n", itr->get()->get_connection_id());

			// Close the socket under the tracking lock, on the connection's
			// own thread (this is only called from connection_thread), so no
			// other thread can be inside send() on it when we close it.
			(*itr)->close_socket();

			connections_.erase(itr);

			// See if we need to restore system power config.
			if (active_connections.count() == 0)
			{
				power_manage.power_restore();
			}

			return;
		}

		DBGWRITE(dbg_level_error, L"Cannot stop connection %d, not tracking.\n", connection_id);
	}

    void dsi_connections::close_all()
    {
		connection_running_ = false;

		// The server is shutting down. First thing to do is send_
        // an attention message to all connected clients informing
        // them of the closure.
		broadcast_attention(dsi_attention_server_shutdown);

		// Only signal the connections to stop. Each connection thread wakes
		// up from its blocked recv(), closes its own socket and removes
		// itself from tracking. Erasing the entries here would destroy the
		// connection objects while their threads are still running.
		std::lock_guard guard(connections_mutex_);

		for (const auto& connection : connections_)
		{
			connection->kill_connection();
		}
	}

	    void dsi_connections::periodic_thread()
	{
		while (connection_running_)
		{
			{
				// The connection list is shared with the accept thread and
				// the connection threads, so it must be walked under the
				// lock. (Scope the lock to the walk only, so the sleep
				// below does not block tracking/shutdown operations.)
				DBGWRITE(dbg_level_trace, L"Periodic thread: waiting on tracking lock\n");
				std::lock_guard guard(connections_mutex_);
				DBGWRITE(dbg_level_trace, L"Periodic thread: lock held, %u connections\n",
				         static_cast<unsigned int>(connections_.size()));

				for (const auto& connection : connections_)
				{
					// Check if we need to tickle the client.
					if (connection->tickle_required() && !connection->get_is_client_sleeping())
					{
						DBGWRITE(dbg_level_trace, L"Periodic thread: sending tickle to connection (%u)\n", connection->get_connection_id());
						connection->send_tickle();
					}

					// If connection is lost, we terminate the connection.
					if (connection->client_lost())
					{
						if (connection->get_is_client_sleeping())
						{
							DBGWRITE(dbg_level_info, L"Client %s is sleeping past expiration, killing connection.\n", connection->get_user_name().c_str());
						}
						else
						{
							DBGWRITE(dbg_level_info, L"Client %s timed out, killing connection.\n", connection->get_user_name().c_str());
						}

						EventWriteAFPServerEvt_ClientTimeout(connection->get_user_name().c_str());

						// Do not remove the connection from tracking here:
						// erasing from the vector while iterating it would
						// invalidate the current iterator, and deleting the
						// object while its connection thread is still running
						// would be a use-after-free. The kill just shuts the
						// socket down to wake the connection thread; that
						// thread closes its socket and stops tracking itself.
						connection->kill_connection();
					}
				}
			}

			std::this_thread::sleep_for(std::chrono::seconds(periodic_thread_sleep_seconds));
		}
	}

	afp_session* dsi_connections::find_afp_session_with_id(const char* afp_id, const UINT32 id_size)
	{
		std::lock_guard guard(connections_mutex_);

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
		std::lock_guard guard(connections_mutex_);

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
		std::lock_guard guard(connections_mutex_);

		for (const auto& connection : connections_)
		{
			connection->send_attention(code);
		}
    }

    UINT16 dsi_connections::count(const bool authenticated_only)
    {
		std::lock_guard guard(connections_mutex_);

		UINT16 result = 0;

		for (const auto& connection : connections_)
		{
			// Only count authenticated sessions when the caller asked
			// for authenticated_only; count everything otherwise.
			if (authenticated_only && !connection->get_is_session_authenticated())
			{
				continue;
			}

			result++;
		}

		return result;
    }
}
