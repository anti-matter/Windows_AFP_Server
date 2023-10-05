/*
 * AFPCriticalSection.h
 * Created 03/28/09
 *
 * Copyright (c) 2009, Michael J. Conrad. All Rights Reserved.
 */

#pragma once

#include <cassert>


class AFPCriticalSection
{
public:
	explicit AFPCriticalSection(
        IN const PCRITICAL_SECTION	critical_section = nullptr,
        IN const bool lock_immediately = false
        )
    {
        m_is_locked_ = false;

        if (critical_section != nullptr)
        {
            m_critical_section_ = critical_section;
            m_owns_structure_ = false;
        }
        else
        {
            m_critical_section_ = static_cast<PCRITICAL_SECTION>(malloc(sizeof(CRITICAL_SECTION)));
            if (m_critical_section_ != nullptr)
            {
                m_owns_structure_ = true;
                InitializeCriticalSection(m_critical_section_);
            }
        }

        if (lock_immediately)
        {
	        AFPCriticalSection::lock();
        }
    }

    virtual ~AFPCriticalSection()
    {
        if (m_is_locked_) {
	        AFPCriticalSection::unlock();
        }

        if (m_owns_structure_ && m_critical_section_ != nullptr)
        {
            DeleteCriticalSection( m_critical_section_ );
            delete [] m_critical_section_;
        }
    }

    virtual void lock()
    {
        assert(m_critical_section_ != nullptr);

        if (m_critical_section_ != nullptr) {

            EnterCriticalSection( m_critical_section_ );
            m_is_locked_ = true;
        }
    }

    virtual void unlock()
    {
        assert(m_critical_section_ != nullptr);

        if ((m_is_locked_) && (m_critical_section_ != nullptr)) {

            LeaveCriticalSection( m_critical_section_ );
            m_is_locked_ = false;
        }
    }

private:
    PCRITICAL_SECTION	m_critical_section_;
    bool				m_is_locked_;
    bool				m_owns_structure_;
};
