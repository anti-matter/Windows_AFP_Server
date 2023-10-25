/*
 * desktop.cpp
 *
 * Copyright (c) Michael J. Conrad.
 *
 * Core AFP desktop database routines live here. This is by no
 * means the best way to implement this.
 */

#include "StdAfx.h"
#include "afpdebug.h"
#include "desktop.h"
#include "afp_volumes.h"
#include "volumes.h"

extern std::unique_ptr<afp_volumes> g_volumes;


/*
 * AFPFindEntry()
 *
 * Description:
 *		Find an entry in the database associated with the supplied criteria.
 *
 * Returns: AFP error code.
 */

AFPERROR AFPFindEntry(
    IN const LPDESKTOP_ENTRY search_entry,
    IN const WORD search_index,
    IN const OPEN_DESKTOP* desk_file,
    OUT const LPDESKTOP_ENTRY found_entry
)
{
    const auto volume_info = g_volumes->find(desk_file->volID);
    if (volume_info == nullptr)
    {
        // We failed to find a block corresponding to the
        // supplied volume ID.
        DBGWRITE(dbg_level_info, L"Bad volume ID in AFPFindEntry()\n");
        return afpParmErr;
    }

    const auto entry = volume_info->get_desktop()->find_entry(search_entry, search_index);
    if (entry != nullptr)
    {
        //DBGWRITE(dbg_level_info, L"[desk:AFPFindEntry]Found entry for file = %s\n", entry->pathname);

        // Make a copy instead of passing original pointer.
        memcpy_s(found_entry, sizeof(DESKTOP_ENTRY), entry, sizeof(DESKTOP_ENTRY));

        return AFP_OK;
    }

#ifdef _DEBUG
    const auto itemType = search_entry->entryType == ENTRY_TYPE_ICON ? L"ICON" : L"APPLorCMNT";
    DBGWRITE(dbg_level_info, L"Failed to find item in desktop DB! (type = %s)\n", itemType);
#endif

    return afpItemNotFound;
}


/*
 * AFPAddEntry()
 *
 * Description:
 *		Add a new entry to the desktop database.
 *
 * Returns: AFP error code.
 */

AFPERROR AFPAddEntry(
    IN const LPDESKTOP_ENTRY new_entry,
    IN const OPEN_DESKTOP* desk_file
)
{
    const auto volume_info = g_volumes->find(desk_file->volID);
    if (volume_info == nullptr)
    {
        // We failed to find a block corresponding to the
        // supplied volume ID.
        DBGWRITE(dbg_level_info, L"Bad volume ID in AFPAddEntry()\n");
        return afpParmErr;
    }

    // Search to see if the data we want to add already exists.
    const auto desktop_db = volume_info->get_desktop();
    const auto existingEntry = desktop_db->find_entry(new_entry);

    // If we find an entry in the data base that matches the new entry, we
    // replace the existing data with the new.
    if (existingEntry != nullptr)
    {
        //
        //For icon entries, we only replace existing entries if the icon
        //bitmap is the same size.
        //
        if (new_entry->entryType == ENTRY_TYPE_ICON)
        {
            if (new_entry->dataSize != existingEntry->dataSize)
            {
                return afpIconTypeError;
            }
        }

        DBGWRITE(dbg_level_info, L"Removing and replacing icon...\n");

       desktop_db->remove_entry(existingEntry);
    }

    desktop_db->add_entry(new_entry);

    DBGWRITE(dbg_level_info, L"Added new item to desktop database!\n");

    return AFP_OK;
}


/*
 * AFPRemoveEntry()
 *
 * Description:
 *		Remove an entry from the desktop database.
 *
 * Returns: AFP error code.
 */

AFPERROR AFPRemoveEntry(
    IN const LPDESKTOP_ENTRY remove_entry,
    IN const OPEN_DESKTOP* desk_file
)
{
    ASSERT(remove_entry != nullptr);
    ASSERT(desk_file != nullptr);

    const auto volume_info = g_volumes->find(desk_file->volID);
    if (volume_info == nullptr)
    {
        // We failed to find a block corresponding to the
        // supplied volume ID.
        DBGWRITE(dbg_level_info, L"[desk]Bad volume ID in AFPAddEntry()\n");
        return afpParmErr;
    }

    DBGWRITE(dbg_level_info, L"[desk]Removing entry from DT array for file %s\n", remove_entry->pathname);

    return volume_info->get_desktop()->remove_entry(remove_entry);
}


/*
 * AFPOpenDeskTopDB()
 *
 * Description:
 *		Opens the desktop database.
 *
 * Returns: AFP error code.
 */

AFPERROR AFPOpenDeskTopDB(
    IN const WORD volume_id,
    IN OPEN_DESKTOP* desk_file
)
{
    const auto volume_info = g_volumes->find(volume_id);
    if (volume_info == nullptr)
    {
        // We failed to find a block corresponding to the
        // supplied volume ID.
        DBGWRITE(dbg_level_info, L"Bad volume ID in AFPOpenDeskTopDB()\n");
        return afpParmErr;
    }

    const auto desktop_path = volume_info->get_share_path() / DESKTOP_FILE_NAME;
    const auto desktop_file_exists = GetFileAttributes(desktop_path.c_str()) == MAXDWORD ? false : true;
    ASSERT(desktop_file_exists);

    desk_file->volID = volume_id;
    desk_file->dtRefNum = volume_id;

    return AFP_OK;
}


/*
 * AFPSynchronizeDesktopFile()
 *
 * Description:
 *		Synchronize the desktop file on the disk to the one in memory.
 *		This is a fairly expensive operation, you don't want to do it
 *		too frequently. However, you don't want to not do it too often
 *		as data loss may result if the server crashes and the latest data
 *		hasn't been synched up yet.
 *
 * Returns: none
 */

void AFPSynchronizeDesktopFile(
    IN const WORD volume_id
)
{
    const auto volume_info = g_volumes->find(volume_id);
    if (volume_info == nullptr)
    {
        DBGWRITE(dbg_level_info, L"Bad volume ID.\n");
        return;
    }

    const auto desktop_path = volume_info->get_share_path() / DESKTOP_FILE_NAME;
    volume_info->get_desktop()->to_file(desktop_path);
}


/*
 * AFPCloseDeskTopDB()
 *
 * Description:
 *		Closes the desktop database.
 *
 * Returns: AFP error code.
 */

AFPERROR AFPCloseDeskTopDB(
    IN const OPEN_DESKTOP* desk_file
)
{
    AFPSynchronizeDesktopFile(desk_file->volID);
    return NO_ERROR;
}
