/*
 *	afpdesktop.cpp
 *	Created 3/5/2005
 *
 *	Copyright © 2005, Michael J. Conrad. All Rights Reserved.
 *
 *	Core FP desktop database routines.
 */

#include "StdAfx.h"
#include "afp.h"
#include "afpdesktop.h"
#include "afpdirectory.h"
#include "AFPSession.h"


/*
 * FPOpenDT()
 *
 * Description:
 *		Open the desktop database for access.
 *
 * Returns: AFPERROR
 */

AFPERROR FPOpenDT(
    IN AfpBuffer& afpRequest,
       IN AfpBuffer& afpReply,
       IN LPSESSION afpSession
)
{
    uint16 afpVolumeID;
    uint16 afpDTRefNum = 0;
    AFPERROR afpError;

    //
    //First WORD is afp command and padding byte.
    //
    afpRequest.Advance(sizeof(int16));

    afpVolumeID = afpRequest.GetInt16();
    afpError = static_cast<AFPSession*>(afpSession->afpSessionObject)->SessionOpenDesk(afpVolumeID, &afpDTRefNum);

    if (AFP_SUCCESS(afpError))
    {
        afpReply.AddInt16(afpDTRefNum);
    }

    return afpError;
}


/*
 * FPCloseDT()
 *
 * Description:
 *		Closes an open desktop database.
 *
 * Returns: AFPERROR
 */

AFPERROR FPCloseDT(
    IN AfpBuffer& afpRequest,
       IN AfpBuffer& afpReply,
       IN LPSESSION afpSession
)
{
    UNREFERENCED_PARAMETER(afpReply);

    uint16 afpDTRefNum;
    AFPERROR afpError;

    afpRequest.Advance(sizeof(int16));

    afpDTRefNum = afpRequest.GetInt16();
    afpError = static_cast<AFPSession*>(afpSession->afpSessionObject)->SessionCloseDesk(afpDTRefNum);

    return afpError;
}


/*
 * FPAddIcon()
 *
 * Description:
 *		Adds an icon to the desktop database.
 *
 * Returns: AFPERROR
 */

AFPERROR FPAddIcon(
    IN AfpBuffer& afpRequest,
       IN AfpBuffer& afpReply,
       IN LPSESSION afpSession
)
{
    UNREFERENCED_PARAMETER(afpReply);

    DESKTOP_ENTRY iconEntry;
    AFPERROR afpError = AFP_OK;

    //
    //The first work contains the command and padding byte.
    //
    afpRequest.Advance(sizeof(int16));

    //
    //Get the refnum to our OPEN desktop database.
    //
    uint16 afpDTRefNum = afpRequest.GetInt16();

    //
    //Grab the file type and creator that will be associated with
    //this icon. Note that in MacOS X, they appear to be going away
    //from creators and types.
    //
    iconEntry.fileCreator = afpRequest.GetInt32();
    iconEntry.fileType = afpRequest.GetInt32();

    //
    //Next bytes containes the icon type we'll be storing. We don't
    //care what it is, its for the client to know.
    //
    iconEntry.iconType = afpRequest.GetInt8();

    //
    //A padding byte, skip it.
    //
    afpRequest.Advance(sizeof(int8));

    //
    //Get the next 2 parameters; We don't know or care what the tag is and
    //we get the size of the data we'll be storing.
    //
    iconEntry.entryTag = afpRequest.GetInt32();
    iconEntry.dataSize = afpRequest.GetInt16();
    iconEntry.entryType = ENTRY_TYPE_ICON;

    //
    //Advance in the buffer to the offset where the icon data is
    //being stored.
    //
    //afpRequest.AdvanceTo(ntohl(*(DWORD*)(afpRequest.GetBuffer())[DSI_OFFSET_DATAOFFSET]));

    if (iconEntry.dataSize > MAX_DESKTOP_DATA_SIZE)
    {
        //
        //Nothing to do here but bail out!
        //
        return (afpParmErr);
    }

    //
    //Now copy the actual icon data into our buffer to store.
    //
    afpRequest.GetRawData(iconEntry.data, iconEntry.dataSize);

    //
    //Now call the desktop routine to do the rest of the work.
    //
    OPEN_DESKTOP* pDesktop = static_cast<AFPSession*>(afpSession->afpSessionObject)->SessionFindDeskFile(afpDTRefNum);

    if (pDesktop != nullptr)
    {
        afpError = AFPAddEntry(&iconEntry, pDesktop);
    }

    return afpError;
}


/*
 * FPGetIcon()
 *
 * Description:
 *		Retrieve and icon from the database.
 *
 * Returns: AFPERROR
 */

AFPERROR FPGetIcon(
    IN AfpBuffer& afpRequest,
       IN AfpBuffer& afpReply,
       IN LPSESSION afpSession
)
{
    DESKTOP_ENTRY searchEntry;
    DESKTOP_ENTRY iconEntry;

    afpRequest.Advance(sizeof(int16));

    uint16 afpDTRefNum = afpRequest.GetInt16();

    searchEntry.fileCreator = afpRequest.GetInt32();
    searchEntry.fileType = afpRequest.GetInt32();
    searchEntry.iconType = afpRequest.GetInt8();

    afpRequest.Advance(sizeof(int8));

    uint16 afpLength = afpRequest.GetInt16();
    searchEntry.entryType = ENTRY_TYPE_ICON;

    auto desktop = static_cast<AFPSession*>(afpSession->afpSessionObject)->SessionFindDeskFile(afpDTRefNum);

    if (desktop == nullptr)
    {
        return afpParmErr;
    }

    auto afpError = AFPFindEntry(&searchEntry, DONT_USE_INDEX, desktop, &iconEntry);

    if (AFP_SUCCESS(afpError))
    {
        //
        //We found the bitmap and the caller actually has memory
        //reserved for the bimap.
        //

        //
        //As per the spec, even if the client didn't reserve enough
        //space, we still copy what we can.
        //
        auto afpCopySize = min(afpLength, iconEntry.dataSize);
        afpReply.AddRawData(iconEntry.data, afpCopySize);
    }

    return afpError;
}


/*
 * FPGetIconInfo()
 *
 * Description:
 *		Retrieve about an icon in the database.
 *
 * Returns: AFPERROR
 */

AFPERROR FPGetIconInfo(
    IN AfpBuffer& afpRequest,
       IN AfpBuffer& afpReply,
       IN LPSESSION afpSession
)
{
    DESKTOP_ENTRY searchEntry;
    DESKTOP_ENTRY foundEntry;

    afpRequest.Advance(sizeof(int16));
    uint16 afpDTRefNum = afpRequest.GetInt16();

    searchEntry.fileCreator = afpRequest.GetInt32();
    uint16 afpIconIndex = afpRequest.GetInt16();
    searchEntry.entryType = ENTRY_TYPE_ICON;

    auto desktop = static_cast<AFPSession*>(afpSession->afpSessionObject)->SessionFindDeskFile(afpDTRefNum);

    if (desktop == nullptr)
    {
        return afpParmErr;
    }

    auto afpError = AFPFindEntry(&searchEntry, afpIconIndex, desktop, &foundEntry);

    if (AFP_SUCCESS(afpError))
    {
        afpReply.AddInt32(foundEntry.entryTag);
        afpReply.AddRawData(&foundEntry.fileType, sizeof(FILETYPE));
        afpReply.AddInt8(foundEntry.iconType);
        afpReply.AddInt8(0);
        afpReply.AddInt16(static_cast<int16>(foundEntry.dataSize));
    }

    return afpError;
}


/*
 * FPAddComment()
 *
 * Description:
 *		Add a comment for a file or directory to the database.
 *
 * Returns: AFPERROR
 */

AFPERROR FPAddComment(
    IN AfpBuffer& afpRequest,
       IN AfpBuffer& afpReply,
       IN LPSESSION afpSession
)
{
    UNREFERENCED_PARAMETER(afpReply);

    DESKTOP_ENTRY newEntry;
    p_afp_index afpEntry = nullptr;

    DBGWRITE(dbg_level_all, L"[afp:FPAddComment]Enter\n");

    afpRequest.Advance(sizeof(int16));

    uint16 afpDTRefNum = afpRequest.GetInt16();
    newEntry.dirID = afpRequest.GetInt32();
    newEntry.pathType = afpRequest.GetInt8();

    auto afpError = afpRequest.GetString(newEntry.pathname, sizeof(newEntry.pathname), true, newEntry.pathType, nullptr);

    if (!AFP_SUCCESS(afpError))
    {
        return (afpParmErr);
    }

    afpRequest.AdvanceIfOdd();
    afpRequest.GetPascalString(newEntry.data, sizeof(newEntry.data));

    newEntry.entryType = ENTRY_TYPE_CMNT;
    newEntry.dataSize = strlen(newEntry.data);

    auto desktop = static_cast<AFPSession*>(afpSession->afpSessionObject)->SessionFindDeskFile(afpDTRefNum);

    if (desktop == nullptr)
    {
        return afpParmErr;
    }

    //
    //Make sure the object that we're adding the comment for
    //exists.
    //
    afpError = AFPIndexFindEntryWithVolIDAndPath(
        desktop->volID,
        newEntry.dirID,
        newEntry.pathname,
        &afpEntry);

    if ((!AFP_SUCCESS(afpError)) ||
        (GetFileAttributes(afpEntry->sz_path) == AFP_MAX_DWORD))
    {
        //
        //We couldn't find the requested object.
        //
        DBGWRITE(dbg_level_info, L"[afp:FPAddComment]Path not found!\n");
        afpError = afpObjectNotFound;
    }
    else
    {
        afpError = AFPAddEntry(&newEntry, desktop);
    }

    return afpError;
}


/*
 * FPGetComment()
 *
 * Description:
 *		Get a comment for a file or directory from the database.
 *
 * Returns: AFPERROR
 */

AFPERROR FPGetComment(
    IN AfpBuffer& afpRequest,
       IN AfpBuffer& afpReply,
       IN LPSESSION afpSession
)
{
    DESKTOP_ENTRY searchEntry;
    DESKTOP_ENTRY foundEntry;
    p_afp_index afpEntry = nullptr;

    DBGWRITE(dbg_level_all, L"[afp:FPGetComment]Enter\n");

    afpRequest.Advance(sizeof(int16));

    uint16 afpDTRefNum = afpRequest.GetInt16();
    searchEntry.dirID = afpRequest.GetInt32();
    searchEntry.pathType = afpRequest.GetInt8();
    searchEntry.entryType = ENTRY_TYPE_CMNT;

    auto afpError = afpRequest.GetString(searchEntry.pathname, sizeof(searchEntry.pathname), true, searchEntry.pathType, nullptr);

    if (!AFP_SUCCESS(afpError))
    {
        return (afpParmErr);
    }

    //
    //Make sure the object that we're getting the comment for
    //exists.
    //
    auto desktop = static_cast<AFPSession*>(afpSession->afpSessionObject)->SessionFindDeskFile(afpDTRefNum);

    if (desktop == nullptr)
    {
        return afpParmErr;
    }

    afpError = AFPIndexFindEntryWithVolIDAndPath(
        desktop->volID,
        searchEntry.dirID,
        searchEntry.pathname,
        &afpEntry);

    if ((!AFP_SUCCESS(afpError)) ||
        (GetFileAttributes(afpEntry->sz_path) == AFP_MAX_DWORD))
    {
        //
        //We couldn't find the requested object.
        //
        DBGWRITE(dbg_level_info, L"[afp:FPGetComment]Path not found!\n");
        afpError = afpObjectNotFound;
    }
    else
    {
        afpError = AFPFindEntry(&searchEntry, DONT_USE_INDEX, desktop, &foundEntry);

        if (AFP_SUCCESS(afpError))
        {
            afpReply.AddCStringAsPascal(foundEntry.data);
        }
    }

    return afpError;
}


/*
 * FPRemoveComment()
 *
 * Description:
 *		Remove a comment for a file or directory from the database.
 *
 * Returns: AFPERROR
 */

AFPERROR FPRemoveComment(
    IN AfpBuffer& afpRequest,
       IN AfpBuffer& afpReply,
       IN LPSESSION afpSession
)
{
    UNREFERENCED_PARAMETER(afpReply);

    DESKTOP_ENTRY searchEntry;

    afpRequest.Advance(sizeof(int16));

    uint16 afpDTRefNum = afpRequest.GetInt16();
    searchEntry.dirID = afpRequest.GetInt32();
    searchEntry.pathType = afpRequest.GetInt8();
    searchEntry.entryType = ENTRY_TYPE_CMNT;

    auto afpError = afpRequest.GetString(searchEntry.pathname, sizeof(searchEntry.pathname), true, searchEntry.pathType, nullptr);

    if (!AFP_SUCCESS(afpError))
    {
        return afpParmErr;
    }

    auto pDesktop = static_cast<AFPSession*>(afpSession->afpSessionObject)->SessionFindDeskFile(afpDTRefNum);

    if (pDesktop == nullptr)
    {
        return afpParmErr;
    }

    afpError = AFPRemoveEntry(&searchEntry, pDesktop);

    return afpError;
}


/*
 * FPGetAPPL()
 *
 * Description:
 *		Get an APPL entry associated with a filetype from the database.
 *
 * Returns: AFPERROR
 */

AFPERROR FPGetAPPL(
    IN AfpBuffer& afpRequest,
       IN AfpBuffer& afpReply,
       IN LPSESSION afpSession
)
{
    WIN32_FILE_ATTRIBUTE_DATA winAttributes;
    DESKTOP_ENTRY searchEntry, foundEntry;
    p_afp_index afpEntry = nullptr;
    uint16 afpVolumeID = 0;

    afpRequest.Advance(sizeof(int16));
    uint16 afpDTRefNum = afpRequest.GetInt16();

    afpRequest.GetRawData(&searchEntry.fileCreator, sizeof(FILECREATOR));

    uint16 afpIndex = afpRequest.GetInt16();
    uint16 afpBitmap = afpRequest.GetInt16();

    searchEntry.entryType = ENTRY_TYPE_APPL;

    auto desktop = static_cast<AFPSession*>(afpSession->afpSessionObject)->SessionFindDeskFile(afpDTRefNum);
    if (desktop == nullptr)
    {
        return afpParmErr;
    }

    auto afpError = AFPFindEntry(
        &searchEntry,
        afpIndex,
        desktop,
        &foundEntry
    );

    if (AFP_SUCCESS(afpError))
    {
        afpError = AFPIndexFindEntryWithVolIDAndPath(
            desktop->volID,
            foundEntry.dirID,
            foundEntry.pathname,
            &afpEntry
        );

        if (AFP_SUCCESS(afpError))
        {
            //
            //Collect attribute data on the file or folder.
            //
            if (!GetFileAttributesEx(afpEntry->sz_path, GetFileExInfoStandard, &winAttributes))
            {
                return afpObjectNotFound;
            }

            afpReply.AddInt16(afpBitmap);
            afpReply.AddInt32(foundEntry.entryTag);

            afpError = AFPGetObjectParms(
                afpReply,
                afpBitmap,
                afpVolumeID,
                foundEntry.dirID,
                afpSession,
                afpEntry,
                winAttributes
            );
        }
    }

    return afpError;
}


/*
 * FPAddAPPL()
 *
 * Description:
 *		Add an APPL entry associated with a filetype to the database.
 *
 * Returns: AFPERROR
 */

AFPERROR FPAddAPPL(
    IN AfpBuffer& afpRequest,
       IN AfpBuffer& afpReply,
       IN LPSESSION afpSession
)
{
    UNREFERENCED_PARAMETER(afpReply);

    DESKTOP_ENTRY newEntry;

    afpRequest.Advance(sizeof(int16));

    uint16 afpDTRefNum = afpRequest.GetInt16();

    newEntry.dirID = afpRequest.GetInt32();
    newEntry.fileCreator = afpRequest.GetInt32();
    newEntry.entryTag = afpRequest.GetInt32();
    newEntry.pathType = afpRequest.GetInt8();
    newEntry.entryType = ENTRY_TYPE_APPL;
    newEntry.dataSize = 0;

    auto afpError = afpRequest.GetString(newEntry.pathname, sizeof(newEntry.pathname), true, newEntry.pathType, nullptr);
    if (!AFP_SUCCESS(afpError))
    {
        return afpParmErr;
    }

    auto desktop = static_cast<AFPSession*>(afpSession->afpSessionObject)->SessionFindDeskFile(afpDTRefNum);
    if (desktop == nullptr)
    {
        return afpParmErr;
    }

    afpError = AFPAddEntry(&newEntry, desktop);

    return afpError;
}


/*
 * FPRemoveAPPL()
 *
 * Description:
 *		Remove an APPL entry associated with a filetype to the database.
 *
 * Returns: AFPERROR
 */

AFPERROR FPRemoveAPPL(
    IN AfpBuffer& afpRequest,
       IN AfpBuffer& afpReply,
       IN LPSESSION afpSession
)
{
    UNREFERENCED_PARAMETER(afpReply);

    DESKTOP_ENTRY searchEntry;

    afpRequest.Advance(sizeof(int16));

    uint16 afpDTRefNum = afpRequest.GetInt16();
    searchEntry.dirID = afpRequest.GetInt32();
    searchEntry.fileCreator = afpRequest.GetInt32();
    searchEntry.pathType = afpRequest.GetInt8();
    searchEntry.entryType = ENTRY_TYPE_APPL;

    auto afpError = afpRequest.GetString(searchEntry.pathname, sizeof(searchEntry.pathname), true, searchEntry.pathType, nullptr);
    if (!AFP_SUCCESS(afpError))
    {
        return afpParmErr;
    }

    //
    //Althoug the spec says to, we don't check to make sure that
    //the actual application really exists on the volume. Why? Because
    //when you move or delete a file, the client always calls this
    //AFTER it deletes or moves it.
    //

    auto desktop = static_cast<AFPSession*>(afpSession->afpSessionObject)->SessionFindDeskFile(afpDTRefNum);
    if (desktop == nullptr)
    {
        return afpParmErr;
    }

    afpError = AFPRemoveEntry(&searchEntry, desktop);

    return afpError;
}
