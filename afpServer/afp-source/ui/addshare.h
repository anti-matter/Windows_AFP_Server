#pragma once

#include <windows.h>
#include <string>

enum class add_share_result
{
    cancelled,
    shared,
    invalid_folder,
    share_failed,
};

struct add_share_outcome
{
    add_share_result result = add_share_result::cancelled;
    std::wstring name; // share name (volume name shown to Mac clients)
    std::wstring path; // Windows folder path
};

// Shows the modern folder picker and, when a folder is chosen, shares it
// via AFPAddNewVolumeFromUI. No message boxes are shown here; the caller
// decides how to present the outcome.
add_share_outcome prompt_for_new_share(HWND owner);
