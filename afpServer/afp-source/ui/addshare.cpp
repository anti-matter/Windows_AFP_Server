#include "stdafx.h"
#include "addshare.h"

#include <shobjidl.h>
#include <strsafe.h>

#include "volumes.h"

namespace
{
    // Extracts the leaf folder name from a full path.
    std::wstring leaf_name(const std::wstring& path)
    {
        size_t end = path.size();
        while (end > 0 && (path[end - 1] == L'\\' || path[end - 1] == L'/'))
            --end;

        const size_t slash = path.find_last_of(L"\\/", end == 0 ? 0 : end - 1);
        if (slash == std::wstring::npos)
            return path.substr(0, end);
        return path.substr(slash + 1, end - slash - 1);
    }

    bool pick_folder(const HWND owner, std::wstring& path_out)
    {
        IFileOpenDialog* dialog = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&dialog));
        if (FAILED(hr))
            return false;

        bool picked = false;

        DWORD options = FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
        dialog->SetOptions(options);
        dialog->SetTitle(L"Choose a folder to share with Macintosh clients");

        if (SUCCEEDED(dialog->Show(owner)))
        {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item)))
            {
                PWSTR raw_path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw_path)))
                {
                    path_out = raw_path;
                    CoTaskMemFree(raw_path);
                    picked = !path_out.empty();
                }
                item->Release();
            }
        }

        dialog->Release();
        return picked;
    }
}

add_share_outcome prompt_for_new_share(const HWND owner)
{
    add_share_outcome outcome;

    std::wstring path;
    if (!pick_folder(owner, path))
    {
        outcome.result = add_share_result::cancelled;
        return outcome;
    }

    outcome.path = path;
    outcome.name = leaf_name(path);

    // Only folders on local disks can be shared (UNC paths and drive roots
    // are rejected), matching the original server-side restriction.
    const bool looks_local = path.size() > 3 && path[0] != L'\\';
    if (!looks_local || outcome.name.empty())
    {
        outcome.result = add_share_result::invalid_folder;
        return outcome;
    }

    // Copy out writable buffers for the (legacy) volume API.
    std::wstring name_buf = outcome.name;
    std::wstring path_buf = outcome.path;

    const DWORD error = AFPAddNewVolumeFromUI(name_buf.data(), path_buf.data(), nullptr);
    outcome.result = SUCCEEDED(error) ? add_share_result::shared : add_share_result::share_failed;
    return outcome;
}
