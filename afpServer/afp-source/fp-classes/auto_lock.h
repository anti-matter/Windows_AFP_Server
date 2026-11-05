#pragma once

class auto_lock  // NOLINT(cppcoreguidelines-special-member-functions)
{
public:
    explicit auto_lock(CRITICAL_SECTION* to_lock);
    ~auto_lock();

    void lock() const;
    void unlock() const;

private:
    CRITICAL_SECTION* cs_;
};

