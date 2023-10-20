#include "stdafx.h"
#include <thread>
#include <winsock2.h>

#include "dsi_init.h"
#include "dsi_connection.h"
#include "afpdebug.h"
#include "dsi_connections.h"
#include "volume_array.h"

bool server_running = true;
SOCKET listen_socket;
extern dsi::dsi_connections active_connections;
extern volume_array g_volume_array;

namespace dsi
{
	void dsi_init()
	{
		WSADATA wsa_data;

		const auto result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
		if (result != 0) {
			DBGWRITE(dbg_level_info, L"WSAStartup failed with error: %d\n", result);
			ASSERT(0);

			return;
		}

		std::thread listen_thread(dsi_listen, nullptr);
		listen_thread.detach();

		std::thread attention_thread(dsi_attention_thread);
		attention_thread.detach();
	}

	void dsi_listen(void* /*data*/)
	{
		do
		{
			std::this_thread::sleep_for(std::chrono::seconds(1));

			DBGWRITE(dbg_level_info, L"Inializing listening socket.");

			listen_socket = socket(AF_INET, SOCK_STREAM, 0);
			if (listen_socket == INVALID_SOCKET)
			{
				DBGWRITE(dbg_level_info, L"Failed to create socket\n");
				continue;
			}

			sockaddr_in internet_address;
			internet_address.sin_family = AF_INET;
			internet_address.sin_addr.s_addr = INADDR_ANY;
			internet_address.sin_port = htons(afp_port);

			auto error = bind(listen_socket, reinterpret_cast<PSOCKADDR>(&internet_address), sizeof(internet_address));
			if (error != ERROR_SUCCESS)
			{
				DBGWRITE(dbg_level_error, L"Failed to bind accept socket (%d)", WSAGetLastError());

				closesocket(listen_socket);
				continue;
			}

			error = listen(listen_socket, SOMAXCONN);
			if (error == SOCKET_ERROR)
			{
				DBGWRITE(dbg_level_error, L"listen() failed (%d)", WSAGetLastError());

				closesocket(listen_socket);
				continue;
			}

			DBGWRITE(dbg_level_info, L"Ready to accept AFP connections...\n");

			UINT32 connection_id = 1;

			while(server_running)
			{
				const auto new_sock = accept(listen_socket, nullptr, nullptr);
				if (new_sock == INVALID_SOCKET)
				{
					error = WSAGetLastError();

					if (error == WSAEINTR)
					{
						// This error means that we have closed the socket
						// and are attempting to shut the thread down.

						DBGWRITE(dbg_level_info, L"Accept thread shutting down...\n");
						break;
					}

					// An error occurred and the acceptance failed. Continue on
					// to the next request.
					DBGWRITE(dbg_level_warning, L"Accept socket failed! (%d)\n", error);
					continue;
				}

				DBGWRITE(dbg_level_info, L"Connection id (%d) accepted.\n", connection_id);

				// Create the connection object that will service the socket
				// and kick it off on its own thread.
				auto connection = std::make_unique<dsi_connection>(new_sock, connection_id++);

				std::thread connection_thread(&dsi_connection::connection_thread, connection.get());
				connection_thread.detach();

				// Henceforth the connection manager owns the pointer.
				active_connections.track_connection(connection);
			}

			closesocket(listen_socket);
			break;

		} while (true);
	}

	void dsi_shutdown()
	{
		// The server is shutting down. First thing to do is send
		// an attention message to all connected clients informing
		// them of the closure.
		active_connections.broadcast_attention(dsi_attention_server_shutdown);

		// Signal listening thread to stop looping.
		server_running = false;

		closesocket(listen_socket);
	}

	void dsi_attention_thread()
	{
	    while (server_running)
	    {
			const auto dirty_volumes = g_volume_array.find_dirty_volumes();
			if (!dirty_volumes.empty())
			{
                for (const WORD dirty_volume : dirty_volumes)
                {
                    const auto volume_info = g_volume_array.find_by_id(dirty_volume);
					if (volume_info != nullptr)
					{
						volume_info->fDirty = false;
						active_connections.broadcast_attention(dsi_attention_server_notify);
					}
                }
			}

			std::this_thread::sleep_for(std::chrono::seconds(1));
	    }
	}
}