/*
*	DesktopDatabase.h
*
*	Copyright © Michael J. Conrad. All Rights Reserved.
*/

#include "stdafx.h"
#include "DesktopDatabase.h"
#include <algorithm>

UINT16 DesktopRefNum = 1;

AFPERROR DesktopDatabase::AddEntry(LPDESKTOP_ENTRY entry)
{
    std::unique_ptr<DESKTOP_ENTRY> newEntry(entry);
    AddItem(newEntry);

    return AFP_OK;
}

AFPERROR DesktopDatabase::RemoveEntry(LPDESKTOP_ENTRY entry)
{
    RemoveItem(entry);
    return AFP_OK;
}

LPDESKTOP_ENTRY DesktopDatabase::FindEntry(LPDESKTOP_ENTRY searchParam, UINT16 searchIndex)
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
