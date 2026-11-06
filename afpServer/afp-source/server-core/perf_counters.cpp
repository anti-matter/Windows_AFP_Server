#include "stdafx.h"
#include "perf_counters.h"
#include <afpCounters.h>
#include <atomic>
#include <thread>

#include "afpdebug.h"
#include "dsi/dsi_connections.h"

extern std::atomic_bool server_running;
extern dsi::dsi_connections active_connections;

perf_counters counters;

perf_counters::perf_counters()
    : bytes_received_(0)
    , bytes_sent_(0)
    , afp_requests_processed_(0)
{
    const auto status = CounterInitialize(nullptr, nullptr, nullptr, nullptr);
    if (status != ERROR_SUCCESS)
    {
        DBGWRITE(dbg_level_error, L"CounterInitialize() failed: %d\n", status);
        return;
    }

    std::thread t(&perf_counters::counter_thread, this);
    t.detach();
}

perf_counters::~perf_counters()
{
    CounterCleanup();
}

perf_counters* perf_counters::current()
{
    return &counters;
}

ULONG perf_counters::create_counter_instance(PPERF_COUNTERSET_INSTANCE* connection_instance)
{
    const auto instance = PerfCreateInstance(AFP_SERVER_PROVIDER, &AFP_SERVERGuid, L"Afp Server Details", 0);
    if (instance == nullptr)
    {
        return GetLastError();
    }

    *connection_instance = instance;

    DBGWRITE(dbg_level_info, L"Performance counters initialized.\n");

    return ERROR_SUCCESS;
}

void perf_counters::counter_thread()
{
    PPERF_COUNTERSET_INSTANCE connection_instance;

    auto status = create_counter_instance(&connection_instance);
    if (status != ERROR_SUCCESS)
    {
        CounterCleanup();
        return;
    }

    while (server_running)
    {
        // Update the number of DSI connections active for this server.
        status = PerfSetULongCounterValue(
            AFP_SERVER_PROVIDER,
            connection_instance,
            AFP_SERVER_CONNECTIONS,
            active_connections.count(true)
        );

        if (status != ERROR_SUCCESS)
        {
            DBGWRITE(dbg_level_warning,
                L"PerfSetULongCounterValue(id_connections) returned failure: %d\n", status);
            break;
        }

        status = PerfSetULongCounterValue(
            AFP_SERVER_PROVIDER,
            connection_instance,
            AFP_SERVER_BYTES_RECEIVED_PER_SECOND,
            bytes_received_
        );

        if (status != ERROR_SUCCESS)
        {
            DBGWRITE(dbg_level_warning,
                L"PerfSetULongCounterValue(id_byte_received_per_second) returned failure: %d\n", status);
            break;
        }

        bytes_received_ = 0;

        status = PerfSetULongCounterValue(
            AFP_SERVER_PROVIDER,
            connection_instance,
            AFP_SERVER_BYTES_SENT_PER_SECOND,
            bytes_sent_
        );

        if (status != ERROR_SUCCESS)
        {
            DBGWRITE(dbg_level_warning,
                L"PerfSetULongCounterValue(ID_BYTESSENTPERSEC) returned failure: %d\n", status);
            break;
        }

        bytes_sent_ = 0;

        status = PerfSetULongCounterValue(
            AFP_SERVER_PROVIDER,
            connection_instance,
            AFP_SERVER_REQUESTS_PER_SECOND,
            afp_requests_processed_
        );

        if (status != ERROR_SUCCESS)
        {
            DBGWRITE(dbg_level_warning,
                L"PerfSetULongCounterValue(ID_AFPREQUESTSPROCESSED) returned failure: %d\n", status);
            break;
        }

        afp_requests_processed_ = 0;

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    PerfDeleteInstance(AFP_SERVER_PROVIDER, connection_instance);
}
