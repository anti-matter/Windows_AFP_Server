//===================================================================
// desktop.h
//===================================================================

#ifndef __desktop__
#define __desktop__

#include "afp.h"

typedef DWORD FILETYPE;
typedef DWORD FILECREATOR;

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

typedef struct
{
	WORD		volID;
	WORD		dtRefNum;
}OPEN_DESKTOP;

typedef struct
{
	UINT8		entryType;
	
	FILETYPE	fileType;
	FILECREATOR	fileCreator;
	DWORD		entryTag;
	DWORD		dirID;
	UINT8		iconType;
	UINT16		dataSize;
	INT8		pathType;
	
	WCHAR		pathname[MAX_AFP_NAME];
	CHAR		data[MAX_DESKTOP_DATA_SIZE];
}DESKTOP_ENTRY, *LPDESKTOP_ENTRY;

typedef struct
{
	DESKTOP_ENTRY*	compareEntry;
	WORD			searchIndex;
}DT_SEARCH_BLOB;


AFPERROR AFPInitDesktop(
	void
);

AFPERROR AFPFindEntry(
	IN				LPDESKTOP_ENTRY	searchEntry,
	IN				WORD			wSearchIndex,
	IN				OPEN_DESKTOP*	deskFile,
	OUT				LPDESKTOP_ENTRY	foundEntry
);

AFPERROR AFPAddEntry(
	IN	LPDESKTOP_ENTRY	newEntry,
	IN	OPEN_DESKTOP*	deskFile
	);

AFPERROR AFPRemoveEntry(
	IN	LPDESKTOP_ENTRY	removeEntry,
	IN	OPEN_DESKTOP*	deskFile
	);

AFPERROR AFPOpenDeskTopDB(
	IN	WORD			wVolID,
	IN	OPEN_DESKTOP*	deskFile
);

void AFPSynchronizeDesktopFile(
	IN	WORD	wVolID
);

AFPERROR AFPCloseDeskTopDB(
	IN	OPEN_DESKTOP*	deskFile
);

#endif //__desktop__
