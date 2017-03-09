/*
 *	addshare.cpp
 *
 *	Copyright © 2005, Michael J. Conrad. All Rights Reserved.
 *
 *	UI routines for adding a new share to the server.
 */

#include "stdafx.h"
#include "addshare.h"
#include "volumes.h"

#include <shlobj.h>
#include <stdlib.h>
#include <stdio.h>


/*
 *	AddShare()
 *
 *	Description
 *		Root routine for allowing the user to add a new share. The routines put
 *		up a choose folder dialog that the user uses to choose a diretory to share.
 *
 *	Returns
 *		None
 */
 
void AddShare(HWND hwndFrontWindow)
{
	TCHAR	szPath[MAX_PATH];
	TCHAR	szSharename[MAX_PATH];

	//
	//ChooseFolder returns FALSE if the user selected cancel or there was
	//another failure.
	//
	if (ChooseFolder(hwndFrontWindow, szPath, szSharename))
	{
		//
		//Make sure the first character in the path is a drive letter and that the
		//path length is greater than 3 ("C:\"). We cannot share remote folders.
		//
		if ((szPath[0] != '\\') && (lstrlen(szPath) > 3))
		{
			TCHAR	szTemp[512];
			
			//
			//The AFP routine that will add the new share to the registry
			//and begin sharing it to users.
			//
			if (SUCCEEDED(AFPAddNewVolumeFromUI(szSharename, szPath, NULL)))
			{
				StringCbPrintf(
					szTemp,
					sizeof(szTemp),
					TEXT("The following directory has been successfully shared to Macintosh clients (via AFP):\n\n %s"),
					szPath
					);
			}
			else
			{
				StringCbPrintf(
					szTemp, sizeof(szTemp),
					TEXT("The directory was not shared, please make sure you are only sharing folders on the local disk.")
					);
			}
			
			MessageBox(
				hwndFrontWindow,
				szTemp,
				TEXT("Sharing Notification"),
				MB_OK
				);
		}
		else
		{
			MessageBox(
				hwndFrontWindow,
				TEXT("Only local directories (folders) may be shared."),
				TEXT("Sharing Error"),
				MB_OK
				);
		}
	}
}


/*
 *	ChooseFolder()
 *
 *	Description
 *		Put up a dialog and allow the user to choose a folder.
 *
 *	Returns
 *		Boolean - TRUE if successful, FALSE otherwise.
 */
 
int ChooseFolder(HWND hwndFrontWindow, PTCHAR pFolderPath, PTCHAR pShareName)
{
	LPITEMIDLIST	lpii	= NULL;
	int				result	= FALSE;
	BROWSEINFO		bi;

	bi.hwndOwner		= hwndFrontWindow;
	bi.pidlRoot			= NULL;
	bi.pszDisplayName	= pShareName;
	bi.lpszTitle		= TEXT("Choose the folder to share to Macintosh clients:");
	bi.ulFlags			= (BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE);
	bi.lpfn				= NULL;
	bi.lParam			= 0;
	bi.iImage			= 0;

	lpii = SHBrowseForFolder(&bi);

	if (lpii != NULL)
	{
		result = (int)SHGetPathFromIDList(lpii, pFolderPath);
	}

	return( result );
}

