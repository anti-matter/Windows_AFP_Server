#pragma once

class power_config  // NOLINT(cppcoreguidelines-special-member-functions)
{
public:
    power_config();
    ~power_config();

    void power_system_required();
    void power_restore();

private:
    HANDLE power_request_;
    std::unique_ptr<REASON_CONTEXT> reason_context_;

    const std::wstring simple_reason_ = L"AFP Server has connections.";
};

