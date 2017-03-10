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
#include "volumes.h"
#include "fpinfo.h"
#include "fdparms.h"


/*
 * AFPInitDesktop()
 *
 * Description:
 *		Initialize the desktop database access functions.
 *
 * Returns: AFP error code.
 */

AFPERROR AFPInitDesktop()
{
    return AFP_OK;
}


/*
 * AFPFindEntry()
 *
 * Description:
 *		Find an entry in the database associated with the supplied criteria.
 *
 * Returns: AFP error code.
 */

AFPERROR AFPFindEntry(
    IN LPDESKTOP_ENTRY searchEntry,
       IN WORD wSearchIndex,
       IN OPEN_DESKTOP* deskFile,
       OUT LPDESKTOP_ENTRY foundEntry
)
{
    AFP_VOLUME_INFO* volumeInfo = nullptr;

    if (!AFPGetVolumeInfoBlock(nullptr, deskFile->volID, &volumeInfo))
    {
        //
        //We failed to find a block corresponding to the
        //supplied volume ID.
        //
        DBGWRITE(DbgLevelInfo, L"[desk]Bad volume ID in AFPFindEntry()\n");
        return afpParmErr;
    }

    auto entry = volumeInfo->desktop->FindEntry(searchEntry, wSearchIndex);

    if (entry != nullptr)
    {
        DBGWRITE(DbgLevelInfo, L"[desk:AFPFindEntry]Found entry for file = %s\n", entry->pathname);

        // Make a copy instead of passing original pointer.
        memcpy_s(foundEntry, sizeof(DESKTOP_ENTRY), entry, sizeof(DESKTOP_ENTRY));

        return AFP_OK;
    }

    #ifdef _DEBUG
    auto const itemType = searchEntry->entryType == ENTRY_TYPE_ICON ? L"ICON" : L"APPLorCMNT";
    DBGWRITE(DbgLevelInfo, L"[desk]Failed to find item in desktop DB! (type = %s)\n", itemType);
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
    IN LPDESKTOP_ENTRY newEntry,
       IN OPEN_DESKTOP* deskFile
)
{
    AFP_VOLUME_INFO* volumeInfo = nullptr;

    if (!AFPGetVolumeInfoBlock(nullptr, deskFile->volID, &volumeInfo))
    {
        //
        //We failed to find a block corresponding to the
        //supplied volume ID.
        //
        DBGWRITE(DbgLevelInfo, L"[desk]Bad volume ID in AFPAddEntry()\n");
        return afpParmErr;
    }

    //
    //Search to see if the data we want to add already exists.
    //
    auto existingEntry = volumeInfo->desktop->FindEntry(newEntry);

    //
    //If we find an entry in the data base that matches the new entry, we
    //replace the existing data with the new.
    //
    if (existingEntry != nullptr)
    {
        //
        //For icon entries, we only replace existing entries if the icon
        //bitmap is the same size.
        //
        if (newEntry->entryType == ENTRY_TYPE_ICON)
        {
            if (newEntry->dataSize != existingEntry->dataSize)
            {
                return afpIconTypeError;
            }
        }

        DBGWRITE(DbgLevelInfo, L"[desk]Removing and replacing icon...\n");

        volumeInfo->desktop->RemoveEntry(existingEntry);
    }

    volumeInfo->desktop->AddEntry(newEntry);

    DBGWRITE(DbgLevelInfo, L"[desk]Added new item to desktop database!\n");

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
    IN LPDESKTOP_ENTRY removeEntry,
       IN OPEN_DESKTOP* deskFile
)
{
    ASSERT(removeEntry != nullptr);
    ASSERT(deskFile != nullptr);

    AFP_VOLUME_INFO* volumeInfo = nullptr;

    if (!AFPGetVolumeInfoBlock(nullptr, deskFile->volID, &volumeInfo))
    {
        //
        //We failed to find a block corresponding to the
        //supplied volume ID.
        //
        DBGWRITE(DbgLevelInfo, L"[desk]Bad volume ID in AFPAddEntry()\n");
        return afpParmErr;
    }

    DBGWRITE(DbgLevelInfo, L"[desk]Removing entry from DT array for file %s\n", removeEntry->pathname);

    return volumeInfo->desktop->RemoveEntry(removeEntry);;
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
    IN WORD wVolID,
       IN OPEN_DESKTOP* deskFile
)
{
    AFP_VOLUME_INFO* volumeInfo;
    TCHAR szPath[MAX_PATH];
    AFPERROR afpError = AFP_OK;

    if (!AFPGetVolumeInfoBlock(nullptr, wVolID, &volumeInfo))
    {
        //
        //We failed to find a block corresponding to the
        //supplied volume ID.
        //
        DBGWRITE(DbgLevelInfo, L"[VOL]Bad volume ID in AFPOpenDeskTopDB()\n");
        return afpParmErr;
    }

    //
    //Build the path to our desktop file for this volume.
    //
    StringCbPrintf(szPath, sizeof(szPath), TEXT("%s\\%s"), volumeInfo->szPath, DESKTOP_FILE_NAME);

    auto desktopFileExists = (GetFileAttributes(szPath) == MAXDWORD) ? false : true;

    AQUIRE_NOTIFY_LOCK_VOLINFO(volumeInfo);

    deskFile->volID = wVolID;
    deskFile->dtRefNum = wVolID;

    if (volumeInfo->desktop == nullptr)
    {
        afpError = afpMiscErr;

        volumeInfo->desktop = new DesktopDatabase();

        if (desktopFileExists)
        {
            volumeInfo->desktop->FromFile(szPath);
        }
        else
        {
            //
            //If we create a new file above, then we also need to add the
            //desktop file to the index.
            //
            auto pEntry = AFPIndexAddEntry(
                nullptr,
                wVolID,
                kRootDirID,
                DESKTOP_FILE_NAME,
                szPath,
                false,
                nullptr
            );

            if (pEntry != nullptr)
            {
                AFPCheckForAndAddFinderInfoStream(pEntry, nullptr, true);
                AFPCheckForAndCreateFinderRsrcStream(pEntry);
            }
            else
            {
                ASSERT(0);
            }
        }
    }

    RELEASE_NOTIFY_LOCK_VOLINFO(volumeInfo);

    return afpError;
}


/*
 * AFPSynchronizeDesktopFile()
 *
 * Description:
 *		Synchronize the desktop file on the disk to the one in memory.
 *		This is a fairly expensive operation, you don't want to do it
 *		too frequentyly. However, you don't want to not do it too often
 *		as data loss may result if the server crashes and the lastest data
 *		hasn't been synched up yet.
 *
 * Returns: none
 */

void AFPSynchronizeDesktopFile(
    IN WORD wVolID
)
{
    AFP_VOLUME_INFO* volumeInfo = nullptr;
    WCHAR szPath[MAX_PATH];

    if (!AFPGetVolumeInfoBlock(nullptr, wVolID, &volumeInfo))
    {
        DBGWRITE(DbgLevelInfo, L"[desk:AFPSynchronizeDesktopFile]Bad volume ID in AFPSynchronizeDesktopFile()\n");
        return;
    }

    StringCbPrintf(szPath, sizeof(szPath), TEXT("%s\\%s"), volumeInfo->szPath, DESKTOP_FILE_NAME);

    volumeInfo->desktop->ToFile(szPath);
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
    IN OPEN_DESKTOP* deskFile
)
{
    AFPSynchronizeDesktopFile(deskFile->volID);
    return NO_ERROR;
}
