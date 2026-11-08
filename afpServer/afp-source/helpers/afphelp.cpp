#include "StdAfx.h"

#include <windows.h>
#include <winerror.h>
#include <Aclapi.h>
#include <winnt.h>
#include <Shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")

#include "afphelp.h"
#include "afpdebug.h"
#include "afp.h"

/// <summary>
/// These objects are ignored by the change notify mechanism.
/// </summary>
static std::wstring objects_to_ignore[] = { // NOLINT(misc-use-anonymous-namespace)
    L".git",            // Hidden git folder with lots of perm issues
    L".finf",           // SheepShaver Finder Info streams
    L"afp_desktop.db"   // Users never need to see this
};

bool should_ignore_object(const std::wstring& object_name)
{
    //Windows object names are case-insensitive; compare that way so
    //"AFP_DESKTOP.DB" etc. is ignored too.
    for (const auto& ignore : objects_to_ignore)  // NOLINT(readability-use-anyofallof)
    {
        if (_wcsicmp(object_name.c_str(), ignore.c_str()) == 0)
        {
            return true;
        }
    }
    return false;
}

std::string wide_string_to_string(const std::wstring& wide_string, const UINT code_page)
{
    if (wide_string.empty())
    {
        return "";
    }

    const auto size_needed = WideCharToMultiByte(code_page, 0, &wide_string.at(0), static_cast<int>(wide_string.size()),
                                                 nullptr, 0, nullptr, nullptr);
    if (size_needed <= 0)
    {
        throw std::runtime_error("WideCharToMultiByte() failed: " + std::to_string(size_needed));
    }

    std::string result(size_needed, 0);
    WideCharToMultiByte(code_page, 0, &wide_string.at(0), static_cast<int>(wide_string.size()), &result.at(0),
                        size_needed, nullptr, nullptr);
    return result;
}

std::wstring string_to_wide_string(const std::string& string, const UINT code_page)
{
    if (string.empty())
    {
        return L"";
    }

    const auto size_needed = MultiByteToWideChar(code_page, 0, &string.at(0), static_cast<int>(string.size()), nullptr,
                                                 0);
    if (size_needed <= 0)
    {
        throw std::runtime_error("MultiByteToWideChar() failed: " + std::to_string(size_needed));
    }

    std::wstring result(size_needed, 0);
    MultiByteToWideChar(code_page, 0, &string.at(0), static_cast<int>(string.size()), &result.at(0), size_needed);
    return result;
}

std::wstring get_root_drive_of_file_path(const std::wstring& file_path)
{
    const int drive_number = PathGetDriveNumber(file_path.c_str());
    if (drive_number == -1)
    {
	    return L"";
    }

    wchar_t buff[4] = {};

    PathBuildRoot(buff, drive_number);

    return { buff };
}

void afp_extract_domain_and_user_name(
    IN std::wstring& user_name,
    OUT std::wstring& domain_name
)
{
    if (user_name.empty())
    {
        return;
    }

    const std::size_t pos = user_name.find(L'\\');
    if (pos == std::wstring::npos)
    {
        // No domain name, leave everything untouched.
        return;
    }

    domain_name = user_name.substr(0, pos);
    user_name = user_name.substr(pos + 1);
}

ULONG afp_windows_to_mac_time(
    IN const PFILETIME file_time
)
{
    ULARGE_INTEGER time;
    time.LowPart = file_time->dwLowDateTime;
    time.HighPart = file_time->dwHighDateTime;

    const ULONGLONG seconds_since_1601 = time.QuadPart / num_100_ns_per_second;

    //
    //AFP dates are seconds since the Mac epoch (1904-01-01 UTC) - the
    //same epoch HFS uses. Times before the epoch are reported as 0,
    //the AFP convention for "no date".
    //
    if (seconds_since_1601 < seconds_to_start_of_1904)
    {
        return 0;
    }

    return static_cast<ULONG>(seconds_since_1601 - seconds_to_start_of_1904);
}

ULONG afp_windows_to_mac_local_time(
    IN const PFILETIME file_time
)
{
    FILETIME local_time{};

    FileTimeToLocalFileTime(file_time, &local_time);

    return afp_windows_to_mac_time(&local_time);
}

void afp_mac_to_windows_time(
    IN ULONG mac_time,
    IN const PFILETIME file_time
)
{
    LARGE_INTEGER utc_time;

    //
    //mac_time is seconds since 1904-01-01 UTC; shift into the Windows
    //epoch and scale to 100ns units.
    //
    utc_time.QuadPart = (static_cast<LONGLONG>(mac_time) +
        static_cast<LONGLONG>(seconds_to_start_of_1904)) * num_100_ns_per_second;

    file_time->dwLowDateTime = utc_time.LowPart;
    file_time->dwHighDateTime = utc_time.HighPart;
}

void afp_mac_to_windows_local_time(
    IN const ULONG mac_time,
    IN const PFILETIME file_time
)
{
    FILETIME utc_time;

    afp_mac_to_windows_time(mac_time, &utc_time);
    FileTimeToLocalFileTime(&utc_time, file_time);
}

bool is_white_space(const char c)
{
    // A white space checker that doesn't choke on negative values.

    return (c == ' ' || c == '\t' || c == '\r' || c == '\n');
}

void afp_get_object_name_from_path(
    IN const PWCHAR p_path,
    OUT const PWCHAR p_name_buff,
    IN const size_t cb_name_buff
)
{
    const auto path_len = lstrlen(p_path);

    if (path_len == 0)
    {
        return;
    }

    // When this loop completes, szTemp should contain the
    // object name.
    auto i = path_len - 1;

    do
    {
        if (p_path[i] == '\\')
        {
            i++;
            break;
        }

        i--;
    }
    while (i >= 0);

    // ReSharper disable once CppFunctionResultShouldBeUsed
    StringCbCopy(p_name_buff, cb_name_buff, &p_path[max(0, i)]);

    // TODO: Add code here to check for proper length of filename!
}

void afp_get_object_dot3_extension(
    IN const PWCHAR p_path,
    OUT const PWCHAR p_name_buff,
    IN const size_t cb_name_buff
)
{
    //Start empty: when no extension is found the caller must get a
    //cleared buffer, not whatever was left in it.
    if (cb_name_buff >= sizeof(WCHAR))
    {
        p_name_buff[0] = L'\0';
    }

    const auto path_len = lstrlen(p_path);

    if (path_len == 0)
    {
        return;
    }

    // When this loop completes, szTemp should contain the
    // object extension.
    auto i = path_len - 1;
    auto j = 0;

    do
    {
        if (p_path[i] == '.')
        {
            i++;
            break;
        }

        i--;
        j++;

        if (j >= 4) break;
    }
    while (i >= 0);

    if (j < 4)
    {
        // ReSharper disable once CppFunctionResultShouldBeUsed
        StringCbCopy(p_name_buff, cb_name_buff, &p_path[max(0, i)]);
    }
}

void afp_get_object_path(
    IN const PWCHAR p_path,
    OUT const PWCHAR p_path_buff
)
{
    // Returns just the path to a file or dir. For example, if the full
    // path to a file is c:\dir1\dir2\dir3\filename, then this function
    // will return c:\dir1\dir2\dir3.

    const auto path_len = lstrlen(p_path);

    p_path_buff[0] = '\0';

    if (path_len == 0)
    {
        return;
    }

    auto j = path_len - 1;
    auto i = 0;

    do
    {
        if (p_path[j] == '\\')
        {
            break;
        }

        i++;
        j--;
    }
    while (i < path_len);

    lstrcpyn(p_path_buff, p_path, path_len - i);
}

AFPERROR afp_map_win_error_to_afp(
    IN const DWORD win_error,
    IN const AFPERROR default_error //Default AFP error to use if no map found
)
{
    switch (win_error)
    {
    case NO_ERROR: return AFP_OK;
    case ERROR_LOCK_VIOLATION: return afpLockErr;

    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND: return afpObjectNotFound;

    case ERROR_INVALID_PARAMETER:
    case ERROR_INVALID_NAME: return afpParmErr;

    // A file held open by another session (or being deleted while open):
    // closer to "object locked" than a parameter error.
    case ERROR_SHARING_VIOLATION:
    case ERROR_USER_MAPPED_FILE: return afpObjectLocked;
    case ERROR_ALREADY_EXISTS: return afpObjectExists;

    case ERROR_ACCESS_DENIED: return afpAccessDenied;
    case ERROR_HANDLE_DISK_FULL: return afpDiskFull;
    case ERROR_FILE_EXISTS: return afpObjectExists;
    case ERROR_DIR_NOT_EMPTY: return afpDirNotEmpty;
    case ERROR_HANDLE_EOF: return afpEofError;
    case ERROR_INVALID_LOGON_HOURS: return afpUserNotAuth;
    case ERROR_PASSWORD_EXPIRED: return afpPwdExpiredErr;

    default: return default_error;
    }
}
