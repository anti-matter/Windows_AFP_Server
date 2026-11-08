#pragma once
#include <perflib.h>
#include <atomic>

class perf_counters
{
public:
    perf_counters();
    ~perf_counters();

    [[nodiscard]] static perf_counters* current();

    // Called from every DSI connection thread while the counter thread
    // samples these - they must be atomic.
    void increment_bytes_received(const DWORD bytes_received)
    {
        bytes_received_.fetch_add(bytes_received, std::memory_order_relaxed);
    }

    void increment_bytes_sent(const DWORD bytes_sent)
    {
        bytes_sent_.fetch_add(bytes_sent, std::memory_order_relaxed);
    }

    void increment_afp_requests(const DWORD afp_requests)
    {
        afp_requests_processed_.fetch_add(afp_requests, std::memory_order_relaxed);
    }

private:
    std::atomic<DWORD> bytes_received_{0};
    std::atomic<DWORD> bytes_sent_{0};
    std::atomic<DWORD> afp_requests_processed_{0};

    // Signalled by the detached counter thread when it exits, so the
    // destructor can wait for it before CounterCleanup() tears the
    // provider down underneath it.
    std::atomic_bool counter_thread_done_{false};

    static ULONG create_counter_instance(PPERF_COUNTERSET_INSTANCE* connection_instance);
    void counter_thread();
};
