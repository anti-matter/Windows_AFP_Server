#pragma once

#include <algorithm>
#include <vector>
#include "afp_array.h"
#include "desktop.h"

struct dt_deleter {
    void operator()(const DESKTOP_ENTRY* b) const { UNREFERENCED_PARAMETER(b); }
};

class desktop_database final : public afp_array<DESKTOP_ENTRY, dt_deleter>
{
public:
    desktop_database() = default;

    AFPERROR add_entry(const LPDESKTOP_ENTRY entry)
    {
        std::unique_ptr<DESKTOP_ENTRY, dt_deleter> new_entry(entry);
        add_item(new_entry);

        return AFP_OK;
    }

    AFPERROR remove_entry(const LPDESKTOP_ENTRY entry)
    {
        auto_lock lock(&m_mutex_);

        const auto it =
            std::find_if(m_data_.begin(), m_data_.end(), [=](const std::unique_ptr<DESKTOP_ENTRY, dt_deleter>& item)
        {
            return memcmp(item.get(), entry, sizeof(DESKTOP_ENTRY)) == 0;  // NOLINT(bugprone-suspicious-memory-comparison)
        });

        if (it != m_data_.end())
        {
            DBGWRITE(dbg_level_info, L"[AFPArray<T>::RemoveItem]Removing item from array.\n");
            m_data_.erase(it);
        }
        return AFP_OK;
    }

    LPDESKTOP_ENTRY find_entry(const LPDESKTOP_ENTRY search_param, const UINT16 search_index = 0)
    {
        auto_lock lock(&m_mutex_);

        UINT16 found_index = 0;

        const auto it = 
            std::find_if(m_data_.begin(), m_data_.end(), [&](const std::unique_ptr<DESKTOP_ENTRY, dt_deleter>& d)
        {
            if (search_param->entryType == d->entryType)
            {
                switch (search_param->entryType)
                {
                case ENTRY_TYPE_APPL:
                    if (search_param->fileCreator == d->fileCreator)
                    {
                        found_index++;

                        if (found_index >= search_index)
                        {
                            DBGWRITE(dbg_level_info, L"[desk]Found APPL entry!\n");
                            return true;
                        }
                    }
                    break;

                case ENTRY_TYPE_CMNT:
                    if ((search_param->dirID == d->dirID) && (!lstrcmp(search_param->pathname, d->pathname)))
                    {
                        found_index++;

                        if (found_index >= search_index)
                        {
                            DBGWRITE(dbg_level_info, L"[desk]Found CMNT entry!\n");
                            return true;
                        }
                    }
                    break;

                case ENTRY_TYPE_ICON:
                    if (search_param->fileCreator == d->fileCreator)
                    {
                        //
                        //This means we're in a search for FPGetIconInfo
                        //
                        if (search_index != 0)
                        {
                            found_index++;

                            if (found_index >= search_index)
                            {
                                DBGWRITE(dbg_level_info, L"[desk]Found ICON entry!\n");
                                return true;
                            }
                        }
                        //
                        //Nope, just a normal search for an existing icon.
                        //
                        else
                        {
                            if ((search_param->fileType == d->fileType) && (search_param->iconType == d->iconType))
                            {
                                DBGWRITE(dbg_level_info, L"[desk]Found ICON entry!\n");
                                return true;
                            }
                        }
                    }
                    break;

                default:
                    break;
                }
            }

            return false;
        });

        return it == m_data_.end() ? nullptr : it->get();
    }

    /*
    * to_file()
    *
    * Description:
    *		Saves the desktop contents to a file.
    *
    * Returns: true if successful.
    */
    bool to_file(const std::wstring& path)
    {
        FILE *file;
        auto_lock lock(&m_mutex_);

        const auto success = _wfopen_s(&file, path.c_str(), L"wb");
        if (success != 0 || file == nullptr)
        {
	        return false;
        }

        const auto size = m_data_.size();
        const auto written = fwrite(&size, sizeof(size), 1, file);
        if (written == sizeof(size))
        {
            for (auto it = m_data_.begin(); it < m_data_.end(); ++it)
            {
	            const auto wrote = fwrite(it->get(), sizeof(DESKTOP_ENTRY), 1, file);
                if (wrote != sizeof(DESKTOP_ENTRY))
                {
                    DBGWRITE(dbg_level_warning, L"Failed to write entire desktop entry!");
                    break;
                }
            }
        }

        return fclose(file) == 0;
    }

    /*
    * from_file()
    *
    * Description:
    *		Reads in the array data from a file.
    *
    * Returns: true if database was read successfully.
    */
    bool from_file(const std::wstring& path)
    {
        FILE *file;
        auto_lock lock(&m_mutex_);

        const auto success = _wfopen_s(&file, path.c_str(), L"w+b");
        if (success != 0 || file == nullptr)
        {
            return false;
        }

        std::vector<std::unique_ptr<DESKTOP_ENTRY, dt_deleter>>::size_type size;
        if (fread(&size, sizeof(size), 1, file) == 0)
        {
            return true;
        }

        for (std::vector<std::unique_ptr<DESKTOP_ENTRY, dt_deleter>>::size_type i = 0; i < size; ++i)
        {
            DESKTOP_ENTRY entry;

            if (fread(&entry, sizeof(entry), 1, file) != 0)
            {
                auto ep = std::unique_ptr<DESKTOP_ENTRY, dt_deleter>(new DESKTOP_ENTRY);
                memcpy(ep.get(), &entry, sizeof(DESKTOP_ENTRY));

                add_item(ep);
            }
        }

        return fclose(file) == 0;
    }
};
