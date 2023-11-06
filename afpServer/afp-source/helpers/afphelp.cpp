// ReSharper disable CppClangTidyClangDiagnosticGnuZeroVariadicMacroArguments
#include "StdAfx.h"

#include <windows.h>
#include <winerror.h>
#include <Aclapi.h>
#include <Ntsecapi.h>
#include <winnt.h>

#include "afphelp.h"
#include "afpdebug.h"
#include "afp.h"
#include "afp_array.h"
#include "users_and_groups_array.h"

users_and_groups_array g_user_and_group_id;
DWORD g_next_user_id = 1;
DWORD g_next_group_id = 1;

std::string wide_string_to_string(const std::wstring& wide_string)
{
	if (wide_string.empty())
	{
		return "";
	}

	const auto size_needed = WideCharToMultiByte(CP_UTF8, 0, &wide_string.at(0), static_cast<int>(wide_string.size()), nullptr, 0, nullptr, nullptr);
	if (size_needed <= 0)
	{
		throw std::runtime_error("WideCharToMultiByte() failed: " + std::to_string(size_needed));
	}

	std::string result(size_needed, 0);
	WideCharToMultiByte(CP_UTF8, 0, &wide_string.at(0), static_cast<int>(wide_string.size()), &result.at(0), size_needed, nullptr, nullptr);
	return result;
}

std::wstring string_to_wide_string(const std::string& string)
{
	if (string.empty())
	{
		return L"";
	}

	const auto size_needed = MultiByteToWideChar(CP_UTF8, 0, &string.at(0), static_cast<int>(string.size()), nullptr, 0);
	if (size_needed <= 0)
	{
		throw std::runtime_error("MultiByteToWideChar() failed: " + std::to_string(size_needed));
	}

	std::wstring result(size_needed, 0);
	MultiByteToWideChar(CP_UTF8, 0, &string.at(0), static_cast<int>(string.size()), &result.at(0), size_needed);
	return result;
}

BOOL afp_get_logon_sid(
	IN const HANDLE h_token,
	OUT std::shared_ptr<SID>& sid
)
{
	auto success = FALSE;
	DWORD length = 0;
	std::unique_ptr<TOKEN_GROUPS> ptg;

	// Get the required buffer size and allocate the TOKEN_GROUPS buffer.
	if (!GetTokenInformation(h_token, TokenGroups, nullptr, 0, &length))
	{
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
		{
			return FALSE;
		}

		ptg = std::unique_ptr<TOKEN_GROUPS>(new TOKEN_GROUPS[length / sizeof(TOKEN_GROUPS)]);

		if (ptg == nullptr)
		{
			// We failed to allocate enough memory for the buffer.
			return FALSE;
		}
	}

	if (GetTokenInformation(h_token, TokenGroups, ptg.get(), length, &length))
	{
		// Loop through the groups to find the logon SID.
		for (DWORD index = 0; index < ptg->GroupCount; index++)
		{
			if ((ptg->Groups[index].Attributes & SE_GROUP_LOGON_ID) == SE_GROUP_LOGON_ID)
			{
				length = GetLengthSid(ptg->Groups[index].Sid);
				sid = std::make_shared<SID>(length);

				if (sid != nullptr)
				{
					if (CopySid(length, sid.get(), ptg->Groups[index].Sid))
					{
						success = TRUE;
					}
				}
			}
		}
	}

	return success;
}

BOOL is_user_admin(
	IN const HANDLE token
)
{
	SID_IDENTIFIER_AUTHORITY nt_authority = SECURITY_NT_AUTHORITY;
	PSID administrators_group = nullptr;

	auto b = AllocateAndInitializeSid(
		&nt_authority,
		2,
		SECURITY_BUILTIN_DOMAIN_RID,
		DOMAIN_ALIAS_RID_ADMINS,
		0, 0, 0, 0, 0, 0,
		&administrators_group
	);
	if (b)
	{
		if (!CheckTokenMembership(token, administrators_group, &b))
		{
			b = FALSE;
		}
		FreeSid(administrators_group);
	}

	return b;
}

/*
 * afp_get_sid_for_user()
 *
 * Description:
 *	Return a user's SID.
 *
 *	Return Value: The user's SID
 */
PSID afp_get_sid_for_user(
	IN const PWCHAR user_name,
	IN const PWCHAR domain_name
)
{
	UNREFERENCED_PARAMETER(domain_name);

	DWORD cb_sid = 0;
	DWORD cb_referenced_domain_name = 0;
	SID_NAME_USE sid_name_use;

	LookupAccountName(
		nullptr,
		user_name,
		nullptr,
		&cb_sid,
		nullptr,
		&cb_referenced_domain_name,
		&sid_name_use
	);

	const auto p_sid = LocalAlloc(LMEM_ZEROINIT, cb_sid);

	if (p_sid != nullptr)
	{
		WCHAR ref_domain_name[128];

		const auto success = LookupAccountName(
			nullptr,
			user_name,
			p_sid,
			&cb_sid,
			ref_domain_name,
			&cb_referenced_domain_name,
			&sid_name_use
		);

		if (!success)
		{
			//
			//For some reason we failed to get the account SID.
			//
			DBGWRITE(dbg_level_info, L"[AFPHELP]LookupAccountName() failed getting user SID for [%s] (%d)\n", user_name,
			         GetLastError());

			LocalFree(p_sid);
			return nullptr;
		}
	}

	return p_sid;
}

/*
 * afp_get_directory_rights()
 *
 * Description:
 *	Returns a bitmap with rights associated with the supplied directory.
 *
 *	Return Value: error code
 */
DWORD afp_get_directory_rights(
    // ReSharper disable once CppParameterNeverUsed
    IN const HANDLE token, //login token
	IN const PWCHAR p_dir_win_path, //directory to check
	IN const PWCHAR p_user_name,
	IN const PWCHAR p_domain_name,
	IN DWORD* afp_rights
)
{
	PACL p_dacl = nullptr;
	PSECURITY_DESCRIPTOR p_security_descriptor = nullptr;
	PSID p_sid_group = nullptr;
	PSID p_sid_owner = nullptr;
	ACCESS_MASK access_rights = 0;
	TRUSTEE trustee = {};

	DBGWRITE(dbg_level_all, L"[AFPHELP]AFPGetDirectoryRights() enter...\n");
	DBGWRITE(dbg_level_all, L"[AFPHELP]user name = %s\n", p_user_name);

	*afp_rights = 0;

	//
	//First step is to get the security descriptor and SIDs for
	//the directory.
	//
	DWORD error = GetNamedSecurityInfo(
		p_dir_win_path,
		SE_FILE_OBJECT,
		DACL_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION,
		&p_sid_owner,
		&p_sid_group,
		&p_dacl,
		nullptr,
		&p_security_descriptor
	);

	if (SUCCEEDED(error) && p_sid_owner != nullptr)
	{
		//
		//Check to see if the current user is the owner of the directory. If not, we
		//get the access rights for this user as allowed by it's SID.
		//
		const auto user_sid = afp_get_sid_for_user(p_user_name, p_domain_name);

		if (user_sid != nullptr)
		{
			if (EqualSid(user_sid, p_sid_owner))
			{
				DBGWRITE(dbg_level_all, L"[AFPHELP]Logged on user is owner!!\n");
				DBGWRITE(dbg_level_all, L"[AFPHELP]Checking access rights for owner\n");

				//
				//Use the handy-dandy routine to build the trustee for us.
				//
				BuildTrusteeWithSid(&trustee, p_sid_owner);
				error = GetEffectiveRightsFromAcl(p_dacl, &trustee, &access_rights);

				DBGWRITE(dbg_level_all, L"[AFPHELP]GetEffectiveRightsFromAcl() returned %d\n", error);

				if (access_rights & (WRITE_OWNER | WRITE_DAC | DELETE))
				{
					*afp_rights |= (kAccess_OW | kAccess_UAW);
					DBGWRITE(dbg_level_all, L"[AFPHELP]Owner has write access!\n");
				}

				if (access_rights & READ_CONTROL)
				{
					*afp_rights |= (kAccess_OS | kAccess_OR | kAccess_UAS | kAccess_UAR);
					DBGWRITE(dbg_level_all, L"[AFPHELP]Owner has read access!\n");
				}

				*afp_rights |= kAccess_Owner;
			}
			else
			{
				//
				//Get the explicit access rights for this user.
				//
				access_rights = 0;

				BuildTrusteeWithSid(&trustee, user_sid);
				error = GetEffectiveRightsFromAcl(p_dacl, &trustee, &access_rights);

				if (SUCCEEDED(error))
				{
					if (access_rights & (WRITE_OWNER | WRITE_DAC | DELETE))
					{
						*afp_rights |= kAccess_UAW;
						DBGWRITE(dbg_level_all, L"[AFPHELP]User has write access!\n");
					}

					if (access_rights & READ_CONTROL)
					{
						*afp_rights |= kAccess_UAS | kAccess_UAR;
						DBGWRITE(dbg_level_all, L"[AFPHELP]User has read access!\n");
					}

					//
					//Needed to move this check here for Vista since admins don't get
					//owner access information anymore.
					//
					//if (is_user_admin(token))
					{
						*afp_rights |= kAccess_Owner;
					}
				}
			}

			LocalFree(user_sid);
		}

		//
		//Now check for guest access.
		//
		//BUGBUG: I cannot find a way to get the name of the guest account. This
		//will fail on international systems as the guest name is different.
		//
		constexpr WCHAR guest[] = L"Guest";
		const auto guest_sid = afp_get_sid_for_user(const_cast<PWCHAR>(guest), nullptr);

		if (guest_sid != nullptr)
		{
			//
			//Get the explicit access rights for this user.
			//
			access_rights = 0;

			BuildTrusteeWithSid(&trustee, guest_sid);
			error = GetEffectiveRightsFromAcl(p_dacl, &trustee, &access_rights);

			if (SUCCEEDED(error))
			{
				if (access_rights & (WRITE_OWNER | WRITE_DAC | DELETE))
				{
					*afp_rights |= kAccess_WW;
					DBGWRITE(dbg_level_all, L"[AFPHELP]Guest has write access!\n");
				}

				if (access_rights & READ_CONTROL)
				{
					*afp_rights |= kAccess_WS | kAccess_WR;
					DBGWRITE(dbg_level_all, L"[AFPHELP]Guest has read access!\n");
				}
			}

			LocalFree(guest_sid);
		}

#if 0
		//
		//Now, get the access rights for the owning group for this object.
		//
        accessRights = 0;

		//
		//Use the handy-dandy routine to build the trustee for us.
		//
        BuildTrusteeWithSid(&trustee, pSidGroup);
        dwError = GetEffectiveRightsFromAcl(pDACL, &trustee, &accessRights);

        DBGWRITE(DbgLevelAll, L"[AFPHELP]GetEffectiveRightsFromAcl() returned %d\n", dwError);

        if (SUCCEEDED(dwError))
        {
            if (accessRights & (WRITE_OWNER | WRITE_DAC | DELETE))
            {
                *afpRights |= kAccess_GW;
                DBGWRITE(DbgLevelAll, L"[AFPHELP]Group has write access!\n");
            }

            if (accessRights & READ_CONTROL)
            {
                *afpRights |= kAccess_GS | kAccess_GR;
                DBGWRITE(DbgLevelAll, L"[AFPHELP]Group has read access!\n");
            }
        }
#endif

		if (p_security_descriptor != nullptr)
		{
			//
			//This pointer was allocate on our behalf by GetNamedSecurityInfo().
			//Clear it now that we're done with it.
			//
			LocalFree(p_security_descriptor);
		}
	}
	else
	{
		DBGWRITE(dbg_level_info, L"[AFPHELP]GetNamedSecurityInfo() failed getting dir rights (%d)!\n", error);
	}

	return error;
}


AFPERROR afp_get_user_name_by_id(
	IN const DWORD id,
	IN const PWCHAR name
)
{
	const auto user_and_group = g_user_and_group_id.find_by_id(id);

	if (user_and_group != nullptr)
	{
		lstrcpyn(name, user_and_group->name, lstrlen(user_and_group->name) + 1);
		return AFP_OK;
	}

	return afpParmErr;
}

DWORD afp_get_directory_owner_and_group_id(
	IN const PWCHAR p_dir_win_path,
	OUT DWORD* owner_id,
	OUT DWORD* group_id
)
{
	PSID p_sid_group = nullptr;
	PSID p_sid_owner = nullptr;
	PSECURITY_DESCRIPTOR pSecurityDescriptor = nullptr;

	ASSERT(owner_id != nullptr);
	ASSERT(group_id != nullptr);

	//
	//Initialize ID's to zero, this means no owner or group assigned.
	//
	*owner_id = 0;
	*group_id = 0;

	//
	//First step is to get the security descriptor and SIDs for
	//the directory.
	//
	DWORD error = GetNamedSecurityInfo(
		p_dir_win_path,
		SE_FILE_OBJECT,
		GROUP_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION,
		&p_sid_owner,
		&p_sid_group,
		nullptr,
		nullptr,
		&pSecurityDescriptor
	);

	if (SUCCEEDED(error))
	{
		WCHAR sz_name[64];
		WCHAR sz_domain_name[64];
		DWORD cb_name = sizeof(sz_name) / 2;
		DWORD cb_domain_name = sizeof(sz_domain_name) / 2;
		DWORD sid_type = 0;

		//
		//First we get the name of the owner of the directory.
		//
		auto succeeded = LookupAccountSid(
			nullptr,
			p_sid_owner,
			sz_name,
			&cb_name,
			sz_domain_name,
			&cb_domain_name,
			reinterpret_cast<PSID_NAME_USE>(&sid_type)
		);

		if (succeeded)
		{
			//
			//We successfully got the owner name from the directory SID. Now we
			//need to convert it to an AFP user ID.
			//
			DBGWRITE(dbg_level_info, L"[AFPHELP]Owner name from SID = %s for dir %s\n", sz_name, p_dir_win_path);

			//
			//We only care about user SID's, check to make sure that's what we got.
			//
			if (sid_type == SidTypeUser)
			{
				//
				//First, search to see if the user name is already in our tables with
				//a valid afp user ID.
				//
				const auto user_and_group = g_user_and_group_id.find_by_name(sz_name, afp_user_name_type);
				if (user_and_group != nullptr)
				{
					*owner_id = user_and_group->id;
				}

				//
				//If a matching user name wasn't found in the list, then add it.
				//
				if (*owner_id == 0)
				{
					auto user_or_group_name = std::make_unique<afp_user_group_name>();

					if (user_or_group_name)
					{
						*owner_id = g_next_user_id++;
						user_or_group_name->id = *owner_id;
						user_or_group_name->type = afp_user_name_type;

						lstrcpyn(user_or_group_name->name, sz_name, lstrlen(sz_name) + 1);

						g_user_and_group_id.add_item(user_or_group_name);

						DBGWRITE(dbg_level_info, L"[AFPHELP]Added username %s to the array\n", user_or_group_name->name);
					}
				}
			}
		}
		else
		{
			//
			//We failed to get the owner name. Report it but only in debug.
			//
			error = GetLastError();

			DBGWRITE(dbg_level_info, L"[AFPHELP]****Error getting owner name for [%s], error code: %d\n",
			         p_dir_win_path,
			         error
			);
		}

		cb_name = sizeof(sz_name) / 2;
		cb_domain_name = sizeof(sz_domain_name) / 2;

		//
		//Get the name of the group that owns the directory.
		//
		succeeded = LookupAccountSid(
			nullptr,
			p_sid_group,
			sz_name,
			&cb_name,
			sz_domain_name,
			&cb_domain_name,
			reinterpret_cast<PSID_NAME_USE>(&sid_type)
		);

		if (succeeded)
		{
			//
			//We successfully got the group name from the directory SID. Now we
			//need to convert it to an AFP group ID.
			//
			DBGWRITE(dbg_level_all, L"[AFPHELP]Group name from SID = %s for dir %s\n", sz_name, p_dir_win_path);

			//
			//We only care about group SID's, check to make sure that's what we got.
			//
			if (sid_type == SidTypeGroup)
			{
			}
		}
		else
		{
			//
			//We failed to get the group name. Report it but only in debug.
			//
			error = GetLastError();

			DBGWRITE(dbg_level_info, L"[AFPHELP]****Error getting group name! (%d)\n", error);
		}

		if (pSecurityDescriptor)
		{
			//
			//This pointer was allocate on our behalf by GetNamedSecurityInfo().
			//Clear it now that we're done with it.
			//
			LocalFree(pSecurityDescriptor);
		}
	}

	return error;
}

/*
 * afp_extract_domain_and_user_name()
 *
 * Description:
 *  Extracts the domain and user name from a string. The string can
 *  consist of "domain\username" or just "username". *
 *
 *	Return Value: none
 */
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
	ULARGE_INTEGER seconds;
	ULARGE_INTEGER time;
	LARGE_INTEGER seconds_to_start_of1980;

	seconds_to_start_of1980.LowPart = 0xc8df3700;
	seconds_to_start_of1980.HighPart = 0x00000002;

	time.LowPart = file_time->dwLowDateTime;
	time.HighPart = file_time->dwHighDateTime;

	seconds.QuadPart = time.QuadPart / num_100_ns_per_second;
	seconds.QuadPart = seconds.QuadPart - seconds_to_start_of1980.QuadPart;

	return seconds.LowPart - seconds_from_1980_2000;
}

ULONG afp_windows_to_mac_local_time(
	IN const PFILETIME file_time
)
{
	LARGE_INTEGER local_time;
	LARGE_INTEGER seconds;
	LARGE_INTEGER seconds_to_start_of1980;

	seconds_to_start_of1980.LowPart = 0xc8df3700;
	seconds_to_start_of1980.HighPart = 0x00000002;

	FileTimeToLocalFileTime(file_time, reinterpret_cast<PFILETIME>(&local_time));

	seconds.QuadPart = local_time.QuadPart / num_100_ns_per_second;
	seconds.QuadPart = seconds.QuadPart - seconds_to_start_of1980.QuadPart;

	return seconds.LowPart - seconds_from_1980_2000;
}

void afp_mac_to_windows_time(
	IN ULONG mac_time,
	IN const PFILETIME file_time
)
{
	LARGE_INTEGER local_time;
	LARGE_INTEGER seconds_to_start_of1980;

	seconds_to_start_of1980.LowPart = 0xc8df3700;
	seconds_to_start_of1980.HighPart = 0x00000002;

	mac_time += seconds_from_1980_2000;

	local_time.QuadPart = mac_time + seconds_to_start_of1980.QuadPart;
	local_time.QuadPart = local_time.QuadPart * num_100_ns_per_second;

	file_time->dwLowDateTime = local_time.LowPart;
	file_time->dwHighDateTime = local_time.HighPart;
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

BOOL open_current_token(
	IN OUT const PHANDLE token
)
{
	//
	//Attempt to open a thread token first since it take precedence
	//over a process token for most operations.
	//
	if (!OpenThreadToken(
		GetCurrentThread(),
		TOKEN_ADJUST_PRIVILEGES,
		FALSE,
		token))
	{
		//
		//Couldn't open a thread token, so now open one for
		//the process.
		//
		if (!OpenProcessToken(
			GetCurrentProcess(),
			TOKEN_ADJUST_PRIVILEGES,
			token))
		{
			DBGWRITE(dbg_level_info, L"[SEC]OpenProcessToken() failed! (%d)\n", GetLastError());
			return FALSE;
		}
	}

	return TRUE;
}

BOOL afp_adjust_privilege(
	IN const PWCHAR privilege,
	IN const BOOL enable,
	IN const HANDLE h_token
)
{
	auto token = INVALID_HANDLE_VALUE;
	LUID luid;
	TOKEN_PRIVILEGES privileges;

	if (!LookupPrivilegeValue(nullptr, privilege, &luid))
	{
		DBGWRITE(dbg_level_error, L"LookupPrivilegeValue() failed for %s! (%d)\n", privilege, GetLastError());
		return FALSE;
	}

	// If hToken was not supplied, then we get the token for the
	// currently running thread.
	if (h_token == INVALID_HANDLE_VALUE)
	{
		if (!open_current_token(&token))
		{
			DBGWRITE(dbg_level_error, L"OpenCurrentToken() failed! (%d)\n", GetLastError());
			return FALSE;
		}
	}
	else
	{
		token = h_token;
	}

	privileges.PrivilegeCount = 1;
	privileges.Privileges[0].Luid = luid;
	privileges.Privileges[0].Attributes = enable ? SE_PRIVILEGE_ENABLED : 0;

	//
	//Here's the real API call that adjusts the privileges.
	//
	if (!AdjustTokenPrivileges(
		token,
		FALSE,
		&privileges,
		0,
		nullptr,
		nullptr))
	{
		const DWORD error = GetLastError();

		DBGWRITE(dbg_level_error, L"AdjustTokenPrivileges() failed! (%d)\n", error);

		//
		//If we have an invalid handle, we don't want to make a call on
		//it at this stage or we'll die. Only close if we created the token
		//handle locally.
		//
		if (error != ERROR_INVALID_HANDLE && h_token == INVALID_HANDLE_VALUE)
		{
			CloseHandle(token);
		}

		return FALSE;
	}

	DBGWRITE(dbg_level_all, L"AdjustTokenPrivileges() succeeded for %s\n", privilege);

	// We only want to close the Token handle if we created it
	// locally with OpenCurrentToken().
	if (h_token == INVALID_HANDLE_VALUE)
	{
		CloseHandle(token);
	}

	return TRUE;
}

BOOL afp_adjust_account_right(
	IN const HANDLE h_user_token,
	const PWCHAR privileges,
	const USHORT cb_privileges //count in bytes the size of the privileges buffer
)
{
	UNREFERENCED_PARAMETER(h_user_token);

	LSA_OBJECT_ATTRIBUTES object_attributes;
	LSA_UNICODE_STRING user_rights;
	auto policy_handle = INVALID_HANDLE_VALUE;

	// First open a policy object for the local system.
	ZeroMemory(&object_attributes, sizeof(object_attributes));

	auto status = LsaOpenPolicy(nullptr, &object_attributes, POLICY_LOOKUP_NAMES | POLICY_CREATE_ACCOUNT, &policy_handle);
	if (status == ERROR_SUCCESS)
	{
		const PSID account_sid = nullptr;
		ZeroMemory(&user_rights, sizeof(user_rights));

		// Build the LSA unicode string structure
		user_rights.Length = static_cast<USHORT>(lstrlen(privileges));
		user_rights.MaximumLength = cb_privileges;
		user_rights.Buffer = privileges;

		status = LsaAddAccountRights(
			policy_handle,
			account_sid,
			&user_rights,
			1
		);

		// ReSharper disable once CppFunctionResultShouldBeUsed
		LsaClose(policy_handle);

		if (status != ERROR_SUCCESS)
		{
			DBGWRITE(dbg_level_info, L"LsaAddAccountRights() failed %lu\n",
			         LsaNtStatusToWinError(status));
			return FALSE;
		}
	}
	else
	{
		DBGWRITE(dbg_level_info, L"LsaOpenPolicy() failed %lu\n",
		         LsaNtStatusToWinError(status));
		return FALSE;
	}

	return TRUE;
}


/*
 * afp_impersonate_user()
 *
 * Description:
 *  Workhorse routine for impersonating a logged in client. We have to
 *  do some special handling, especially for Vista, to ensure that
 *  the logged on user token has the privilege to impersonate on this
 *  machine.
 *
 *  NOTE: In order to set the impersonation privilege, the logged on user
 *  must be enabled as an impersonable user. To do this, go to the Local
 *  Security Settings in the control panel, go to Local Policies\User Rights
 *  Assignment and add your user to the 'Impersonate a client after
 *  authentication' policy.
 *
 *	Return Value: true if successful.
 */
BOOL afp_impersonate_user(
	IN const HANDLE h_user_token
)
{
	const std::wstring impersonate_name = SE_IMPERSONATE_NAME;

	auto success = afp_adjust_privilege(const_cast<PWCHAR>(impersonate_name.c_str()), TRUE);

	if (success)
	{
		success = ImpersonateLoggedOnUser(h_user_token);
		if (!success)
        {
            DBGWRITE(dbg_level_error, L"Impersonation failed\n");
        }
	}

	return success;
}

BOOL afp_revert_to_self(
	IN const HANDLE h_user_token
)
{
	UNREFERENCED_PARAMETER(h_user_token);

	const BOOL success = RevertToSelf();
	const std::wstring impersonate_name = SE_IMPERSONATE_NAME;

	afp_adjust_privilege(const_cast<PWCHAR>(impersonate_name.c_str()), FALSE);

	return success;
}

PWCHAR afp_mac_to_windows_string(
	IN const PCHAR p_mac_string,
	IN const WORD cb_wide_string,
	OUT const PWCHAR p_wide_string
)
{
	if (strlen(p_mac_string) == 0)
	{
		p_wide_string[0] = '\0';
		return p_wide_string;
	}

	const auto size = MultiByteToWideChar(
		CP_MACCP,
		0,
		p_mac_string,
		static_cast<int>(strlen(p_mac_string)),
		p_wide_string,
		cb_wide_string
	);

	if (size == 0)
	{
		DBGWRITE(dbg_level_error, L"MultiByteToWideChar() failed in AFPMacToWindowsString()! (%d)\n", GetLastError());
		return nullptr;
	}

	p_wide_string[size] = '\0';

	return p_wide_string;
}

PWCHAR afp_mac_to_windows_path(
	IN const PCHAR p_afp_path,
	OUT const PWCHAR p_wide_path,
	OUT AFPCHARMAPARRAY p_char_map
)
{
	const auto path_len = strlen(p_afp_path);

	if (path_len == 0)
	{
		p_wide_path[0] = '\0';
		return p_wide_path;
	}

	if (p_char_map != nullptr)
	{
		memset(p_char_map, 0, sizeof(AFPCHARMAPARRAY));
	}

	WORD char_map_pos = 0;

	for (WORD i = 0; i < path_len; i++)
	{
		//
		//Make sure we're ansi chars and make sure the name doesn't contain
		//one of the illegal file characters for Windows.
		//
		if (p_afp_path[i] < FIRST_VALID_ANSI_CHAR ||
			static_cast<UCHAR>(p_afp_path[i]) > LAST_VALID_ANSI_CHAR ||
			p_afp_path[i] == '\\' ||
			p_afp_path[i] == '/' ||
			p_afp_path[i] == '"' ||
			p_afp_path[i] == '?' ||
			p_afp_path[i] == '*' ||
			p_afp_path[i] == '<' ||
			p_afp_path[i] == '>' ||
			p_afp_path[i] == '|')
		{
			//
			//If pCharMap is not null, then we are building the char map for
			//the index entry as well.
			//
			if (p_char_map != nullptr)
			{
				p_char_map[char_map_pos].wKeyCode = static_cast<BYTE>(p_afp_path[i]);
				p_char_map[char_map_pos].wOffset = i;

				DBGWRITE(dbg_level_all, L"[HELP]Adding keycode %d offset %d\n",
				         p_char_map[char_map_pos].wKeyCode,
				         p_char_map[char_map_pos].wOffset);

				char_map_pos++;
			}

			p_afp_path[i] = REPLACEMENT_CHAR;
		}
		else
		{
			switch (p_afp_path[i])
			{
			//
			//The macintosh path separator is a ":", convert to windows path
			//separator of "/". Note that we don't save for conversion back,
			//since we'll never send paths back to the mac client.
			//
			case ':':
				p_afp_path[i] = '/';
				break;

			//
			//Windows can't handle multiple periods in a row, we need to replace
			//one of them. We also can't have the last character in the file name
			//be a period.
			//
			case '.':
				if (p_afp_path[i + 1] == '.' || i == path_len - 1)
				{
					if (p_char_map != nullptr)
					{
						p_char_map[char_map_pos].wKeyCode = static_cast<BYTE>(p_afp_path[i]);
						p_char_map[char_map_pos].wOffset = i;

						char_map_pos++;
					}

					p_afp_path[i] = REPLACEMENT_CHAR;
				}
				break;

			default:
				break;
			}
		}
	}

	//
	//Lastly, make sure the last character in the afp path is not a white space
	//because Windows has trouble dealing with that.
	//
	if (is_white_space(p_afp_path[path_len - 1]))
	{
		p_afp_path[path_len - 1] = '\0';
	}

	//
	//So we know where to stop when reading the char table.
	//
	if (p_char_map != nullptr)
	{
		p_char_map[char_map_pos].wKeyCode = 0;
		p_char_map[char_map_pos].wOffset = 0;
	}

	const auto size = MultiByteToWideChar(
		CP_MACCP,
		0,
		p_afp_path,
		static_cast<int>(strlen(p_afp_path)),
		p_wide_path,
		MAX_PATH);

	if (size == 0)
	{
		DBGWRITE(dbg_level_info, L"[HELP]MultiByteToWideChar() failed! (%d)\n", GetLastError());
		return nullptr;
	}

	p_wide_path[size] = '\0';

	return p_wide_path;
}


bool is_white_space(const char c)
{
	// A white space checker that doesn't choke on negative values.

	return (c == ' ' || c == '\t' || c == '\r' || c == '\n');
}

PCHAR afp_windows_to_mac_path(
	IN const WCHAR* p_win_path,
	IN const WORD afp_buff_size,
	IN AFPCHARMAPARRAY p_char_map,
	OUT const PCHAR p_afp_path
)
{
	const auto path_len = lstrlen(p_win_path);

	if (path_len == 0)
	{
		p_afp_path[0] = '\0';
		return p_afp_path;
	}

	BOOL f_used_default_char;
	const auto size = WideCharToMultiByte(
		CP_MACCP,
		0,
		p_win_path,
		lstrlen(p_win_path),
		p_afp_path,
		afp_buff_size,
		"!",
		&f_used_default_char);

	if (size == 0)
	{
		DBGWRITE(dbg_level_info, L"[HELP]WideCharToMultiByte() failed! (%d)\n", GetLastError());
		return p_afp_path;
	}

	p_afp_path[size] = '\0';

	if (p_char_map != nullptr)
	{
		auto i = 0;

		while (p_char_map[i].wKeyCode != 0)
		{
			if (p_char_map[i].wOffset > size)
			{
				break;
			}

			p_afp_path[p_char_map[i].wOffset] = static_cast<char>(p_char_map[i].wKeyCode);

			DBGWRITE(dbg_level_all, L"[HELP]Converting keycode %d offset %d\n",
			         p_char_map[i].wKeyCode,
			         p_char_map[i].wOffset);

			i++;
		}
	}

	return p_afp_path;
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

	//
	//When this loop completes, szTemp should contain the
	//object name.
	//
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

	//
	//BUGBUG: Add code here to check for proper length of filename!
	//
}


void afp_get_object_dot3_extension(
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

	//
	//When this loop completes, szTemp should contain the
	//object extension.
	//
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
