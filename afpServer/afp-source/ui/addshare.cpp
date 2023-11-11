#include "stdafx.h"
#include "addshare.h"

#include <shlobj.h>

#include "volumes.h"


void add_share(const HWND front_window)
{
    WCHAR share_name[MAX_PATH];

    if (WCHAR path[MAX_PATH]; choose_folder(front_window, path, share_name))
    {
        if (path[0] != '\\' && lstrlen(path) > 3)
        {
            WCHAR temp[512];

            if (SUCCEEDED(AFPAddNewVolumeFromUI(share_name, path, NULL)))
            {
                std::ignore = StringCbPrintf(
                    temp,
                    sizeof(temp),
                    TEXT("The following directory has been successfully shared to Macintosh clients (via AFP):\n\n %s"),
                    path
                );
            }
            else
            {
                std::ignore = StringCbPrintf(
                    temp, sizeof(temp),
                    TEXT(
                        "The directory was not shared, please make sure you are only sharing folders on the local disk.")
                );
            }

            MessageBox(
                front_window,
                temp,
                TEXT("Sharing Notification"),
                MB_OK
            );
        }
        else
        {
            MessageBox(
                front_window,
                TEXT("Only local directories (folders) may be shared."),
                TEXT("Sharing Error"),
                MB_OK
            );
        }
    }
}


int choose_folder(const HWND front_window, const PTCHAR folder_path, const PTCHAR share_name)
{
    int result = FALSE;
    BROWSEINFO bi;

    bi.hwndOwner = front_window;
    bi.pidlRoot = nullptr;
    bi.pszDisplayName = share_name;
    bi.lpszTitle = TEXT("Choose the folder to share to Macintosh clients:");
    bi.ulFlags = (BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE);
    bi.lpfn = nullptr;
    bi.lParam = 0;
    bi.iImage = 0;

    if (const LPITEMIDLIST item_id_list = SHBrowseForFolder(&bi); item_id_list != nullptr)
    {
        result = SHGetPathFromIDList(item_id_list, folder_path);
    }

    return result;
}
