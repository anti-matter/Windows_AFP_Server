//===================================================================
// desktop.h
//===================================================================

#ifndef __desktop__
#define __desktop__

#include "afp.h"

using FILETYPE = DWORD;
using FILECREATOR = DWORD;

#define MAX_DESKTOP_DATA_SIZE	1024
#define MAX_APPL_PATHNAME		MAX_AFP_STRING_LEN
#define MAX_OPEN_DESKTOPS		32
#define MAX_COMMENT_SIZE		199
#define DESKTOP_FILE_NAME		TEXT("afp_desktop.db")
#define DONT_USE_INDEX			0

#define ENTRY_TYPE_EMPTY		0
#define ENTRY_TYPE_ICON			1
#define ENTRY_TYPE_APPL			2
#define ENTRY_TYPE_CMNT			3
#define ENTRY_TYPE_RESERVED		4

using OPEN_DESKTOP = struct
{
    WORD volID;
    WORD dtRefNum;
};

typedef struct
{
    UINT8 entryType;

    FILETYPE fileType;
    FILECREATOR fileCreator;
    DWORD entryTag;
    DWORD dirID;
    UINT8 iconType;
    UINT16 dataSize;
    INT8 pathType;

    WCHAR pathname[MAX_AFP_NAME];
    CHAR data[MAX_DESKTOP_DATA_SIZE];
} DESKTOP_ENTRY, *LPDESKTOP_ENTRY;

using DT_SEARCH_BLOB = struct
{
    DESKTOP_ENTRY* compareEntry;
    WORD searchIndex;
};

AFPERROR AFPFindEntry(
    IN LPDESKTOP_ENTRY search_entry,
    IN WORD search_index,
    IN const OPEN_DESKTOP* desk_file,
    OUT LPDESKTOP_ENTRY found_entry
);

AFPERROR AFPAddEntry(
    IN LPDESKTOP_ENTRY new_entry,
    IN const OPEN_DESKTOP* desk_file
);

AFPERROR AFPRemoveEntry(
    IN LPDESKTOP_ENTRY remove_entry,
    IN const OPEN_DESKTOP* desk_file
);

AFPERROR AFPOpenDeskTopDB(
    IN WORD volume_id,
    IN OPEN_DESKTOP* desk_file
);

void AFPSynchronizeDesktopFile(
    IN WORD volume_id
);

AFPERROR AFPCloseDeskTopDB(
    IN const OPEN_DESKTOP* desk_file
);

#endif //__desktop__
