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

    if (!PowerSetRequest(power_request_, PowerRequestSystemRequired))
    {
        DBGWRITE(dbg_level_error, L"Setting power request failed (%d).\n", GetLastError());
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

	reason_context_.reset();
}
