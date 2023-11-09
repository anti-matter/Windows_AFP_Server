#pragma once

#include <fstream>
#include <iterator>
#include <vector>
#include <windows.h>
#include "auto_lock.h"
#include "afpdebug.h"

template <class T, class D = std::default_delete<T>> class afp_array  // NOLINT(cppcoreguidelines-special-member-functions)
{
public:

    /*
    * AFPArray()
    *
    * Description:
    *		Initializer for the AFPArray class.
    *
    * Returns: None
    */
    afp_array()
    {
        InitializeCriticalSection(&m_mutex_);
    }

    /*
    * ~AFPArray()
    *
    * Description:
    *		Destructor, deallocate the memory used for the buffer.
    *
    * Returns: None
    */
    virtual ~afp_array()
    {
        DeleteCriticalSection(&m_mutex_);
    }

    /*
    * AddItem()
    *
    * Description:
    *		Add a new item to the array to track.
    *
    * Returns: None
    */
    void add_item(std::unique_ptr<T,D>& data)
    {
        auto_lock lock(&m_mutex_);
        m_data_.push_back(std::move(data));
    }

    /*
    * RemoveItem()
    *
    * Description:
    *		Remove an item from the array.
    *
    * Returns: None
    */
    void remove_item(const T* data)
    {
        auto_lock lock(&m_mutex_);

        typename std::vector<std::unique_ptr<T,D>>::iterator it =
            std::find_if(m_data_.begin(), m_data_.end(), [=](const std::unique_ptr<T>& item)
        {
            return memcmp(item.get(), data, sizeof(T)) == 0;
        });

        if (it != m_data_.end())
        {
            DBGWRITE(dbg_level_info, L"[AFPArray<T>::RemoveItem]Removing item from array.\n");
            m_data_.erase(it);
        }
    }

    /*
    * GetItem()
    *
    * Description:
    *		Returns the item (T*) located at the position.
    *
    * Returns: T*
    */
    T* get_item(UINT32 index)
    {
        std::unique_ptr<T, D> &up = m_data_.at(index);
        return up.get();
    }

	UINT32 num_items() const { return m_data_.size(); }
    static UINT32 item_size() { return sizeof(T); }

    // So you can iterate through the vector array.
    typename std::vector<std::unique_ptr<T>>::iterator begin() { return m_data_.begin(); }
    typename std::vector<std::unique_ptr<T>>::iterator end() { return m_data_.end(); }

    CRITICAL_SECTION* mutex() { return &m_mutex_; }

protected:

    std::vector<std::unique_ptr<T,D>> m_data_;
	CRITICAL_SECTION m_mutex_;
};
