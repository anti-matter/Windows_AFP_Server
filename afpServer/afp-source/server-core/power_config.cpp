#include "stdafx.h"
#include "power_config.h"

#include "afpdebug.h"

power_config::power_config(): power_request_(nullptr)
{
    reason_context_ = std::make_unique<REASON_CONTEXT>();
    reason_context_->Version = POWER_REQUEST_CONTEXT_VERSION;
    reason_context_->Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
    reason_context_->Reason.SimpleReasonString = const_cast<LPWSTR>(simple_reason_.c_str());
}

power_config::~power_config()
{
    power_restore();
}

void power_config::power_system_required()
{
    if (power_request_ != nullptr)
    {
        DBGWRITE(dbg_level_trace, L"Power config already set.\n");
        return;
    }

    power_request_ = PowerCreateRequest(reason_context_.get());

    if (power_request_ == nullptr)
    {
        DBGWRITE(dbg_level_error, L"PowerCreateRequest() failed (%d).\n", GetLastError());
        return;
    }

    if (!PowerSetRequest(power_request_, PowerRequestSystemRequired))
    {
        DBGWRITE(dbg_level_error, L"Setting power request failed (%d).\n", GetLastError());

        // Close the handle so a later attempt (next client connection)
        // can create a fresh request; keeping it here would make every
        // subsequent call believe the request is already active.
        CloseHandle(power_request_);
        power_request_ = nullptr;
        return;
    }

    DBGWRITE(dbg_level_trace, L"Power configuration set successfully.\n");
}

void power_config::power_restore()
{
    if (power_request_ != nullptr)
    {
        PowerClearRequest(power_request_, PowerRequestSystemRequired);
        CloseHandle(power_request_);

        power_request_ = nullptr;

        DBGWRITE(dbg_level_trace, L"Power configuration restored.\n");
    }

    // Note: reason_context_ is deliberately kept alive - it is needed to
    // create a new request if a client connects again later. Resetting it
    // here made every subsequent power_system_required() call fail with a
    // NULL reason context, permanently losing the sleep-block after the
    // first client disconnect.
}
