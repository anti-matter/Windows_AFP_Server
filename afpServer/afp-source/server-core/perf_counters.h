#pragma once
#include <perflib.h>

class perf_counters
{
public:
    perf_counters();
    ~perf_counters();

    [[nodiscard]] static perf_counters* current();

    void increment_bytes_received(const DWORD bytes_received)
    {
        bytes_received_ += bytes_received;
    }

    void increment_bytes_sent(const DWORD bytes_sent)
    {
        bytes_sent_ += bytes_sent;
    }

    void increment_afp_requests(const DWORD afp_requests)
    {
        afp_requests_processed_ += afp_requests;
    }

private:
    DWORD bytes_received_;
    DWORD bytes_sent_;
    DWORD afp_requests_processed_;

    static ULONG create_counter_instance(PPERF_COUNTERSET_INSTANCE* connection_instance);
    void counter_thread();
};
