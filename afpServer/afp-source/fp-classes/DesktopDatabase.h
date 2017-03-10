/*
*	DesktopDatabase.h
*
*	Copyright © Michael J. Conrad. All Rights Reserved.
*/

#pragma once

#include <algorithm>
#include "LArray.h"
#include "desktop.h"

extern UINT16 DesktopRefNum;

class DesktopDatabase : public LArray<DESKTOP_ENTRY>
{
public:
    DesktopDatabase() : LArray() 
    {
    }

    ~DesktopDatabase()
    {
    }

    AFPERROR AddEntry(LPDESKTOP_ENTRY entry)
    {
        std::unique_ptr<DESKTOP_ENTRY> newEntry(entry);
        AddItem(newEntry);

        return AFP_OK;
    }

    AFPERROR RemoveEntry(LPDESKTOP_ENTRY entry)
    {
        RemoveItem(entry);
        return AFP_OK;
    }

    LPDESKTOP_ENTRY FindEntry(LPDESKTOP_ENTRY searchParam, UINT16 searchIndex = 0)
    {
        UINT16 foundIndex = 0;

        std::vector<std::unique_ptr<DESKTOP_ENTRY>>::iterator it = std::find_if(mData.begin(), mData.end(), [&](const std::unique_ptr<DESKTOP_ENTRY>& d)
        {
            if (searchParam->entryType == d.get()->entryType)
            {
                switch (searchParam->entryType)
                {
                case ENTRY_TYPE_APPL:
                    if (searchParam->fileCreator == d.get()->fileCreator)
                    {
                        foundIndex++;

                        if (foundIndex >= searchIndex)
                        {
                            DBGWRITE(DbgLevelInfo, L"[desk]Found APPL entry!\n");
                            return true;
                        }
                    }
                    break;

                case ENTRY_TYPE_CMNT:
                    if ((searchParam->dirID == d.get()->dirID) && (!lstrcmp(searchParam->pathname, d.get()->pathname)))
                    {
                        foundIndex++;

                        if (foundIndex >= searchIndex)
                        {
                            DBGWRITE(DbgLevelInfo, L"[desk]Found CMNT entry!\n");
                            return true;
                        }
                    }
                    break;

                case ENTRY_TYPE_ICON:
                    if (searchParam->fileCreator == d.get()->fileCreator)
                    {
                        //
                        //This means we're in a search for FPGetIconInfo
                        //
                        if (searchIndex != 0)
                        {
                            foundIndex++;

                            if (foundIndex >= searchIndex)
                            {
                                DBGWRITE(DbgLevelInfo, L"[desk]Found ICON entry!\n");
                                return true;
                            }
                        }
                        //
                        //Nope, just a normal search for an existing icon.
                        //
                        else
                        {
                            if ((searchParam->fileType == d.get()->fileType) && (searchParam->iconType == d.get()->iconType))
                            {
                                DBGWRITE(DbgLevelInfo, L"[desk]Found ICON entry!\n");
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

        if (it != mData.end())
        {
            return it->get();
        }

        return nullptr;
    }

    /*
    * ToFile()
    *
    * Description:
    *		Saves the desktop contents to a file.
    *
    * Returns: None
    */
    bool ToFile(std::wstring path)
    {
        FILE *file;
        
        auto success = _wfopen_s(&file, path.c_str(), L"wb");
        if (success != 0)
            return false;

        auto size = mData.size();
        fwrite(&size, sizeof(size), 1, file);

        for (auto it = mData.begin(); it < mData.end(); ++it)
        {
            fwrite(it->get(), sizeof(DESKTOP_ENTRY), 1, file);
        }

        fclose(file);
        return true;
    }

    /*
    * FromFile()
    *
    * Description:
    *		Reads in the array data from a file.
    *
    * Returns: None
    */
    bool FromFile(std::wstring path)
    {
        FILE *file;

        auto success = _wfopen_s(&file, path.c_str(), L"w+b");
        if (success != 0)
            return false;

        std::vector<std::unique_ptr<DESKTOP_ENTRY>>::size_type size;
        fread(&size, sizeof(size), 1, file);

        for (std::vector<std::unique_ptr<DESKTOP_ENTRY>>::size_type i = 0; i < size; ++i)
        {
            DESKTOP_ENTRY entry;

            if (fread(&entry, sizeof(entry), 1, file) != 0)
            {
                auto ep = std::make_unique<DESKTOP_ENTRY>(entry);
                AddItem(ep);
            }
        }

        fclose(file);
        return false;
    }
};

