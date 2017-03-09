//===================================================================
// afphelp.c
//===================================================================

#include "StdAfx.h"

#include <windows.h>
#include <winerror.h>
#include <Aclapi.h>
#include <Ntsecapi.h>
#include <winnt.h>

#include "afphelp.h"
#include "afpdebug.h"
#include "afp.h"
#include "LArray.h"

LArray gUserAndGroupID;
DWORD gNextUserID = 1;
DWORD gNextGroupID = 1;

/*
 * AFPGetLogonSID()
 *
 * Description:
 *	Returns the SID for the logged in access token
 *	
 *	Return Value: 
 *		TRUE - Call succeeded. 
 *		FALSE - Call failed
 */
BOOL AFPGetLogonSID(
	IN HANDLE hToken,
	   OUT PSID* ppSID
)
{
	auto bSuccess = FALSE;
	DWORD dwIndex;
	DWORD dwLength = 0;
	std::unique_ptr<TOKEN_GROUPS> ptg;

	//
	//Get the required buffer size and allocate the TOKEN_GROUPS buffer.
	//
	if (!GetTokenInformation(hToken, TokenGroups, nullptr, 0, &dwLength))
	{
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
		{
			return FALSE;
		}

		ptg = std::unique_ptr<TOKEN_GROUPS>(new TOKEN_GROUPS[dwLength / sizeof(TOKEN_GROUPS)]);

		if (ptg == nullptr)
		{
			//
			//We failed to allocate enough memory for the buffer.
			//
			return FALSE;
		}
	}

	if (GetTokenInformation(hToken, TokenGroups, static_cast<LPVOID>(ptg.get()), dwLength, &dwLength))
	{
		//
		//Loop through the groups to find the logon SID.
		//

		for (dwIndex = 0; dwIndex < ptg->GroupCount; dwIndex++)
		{
			if ((ptg->Groups[dwIndex].Attributes & SE_GROUP_LOGON_ID) == SE_GROUP_LOGON_ID)
			{
				dwLength = GetLengthSid(ptg->Groups[dwIndex].Sid);
				*ppSID = static_cast<PSID>(DBG_ALLOC(dwLength));

				if (*ppSID != nullptr)
				{
					if (!CopySid(dwLength, *ppSID, ptg->Groups[dwIndex].Sid))
					{
						DBG_FREE(*ppSID);
					}
					else
					{
						bSuccess = TRUE;
					}
				}
			}
		}
	}

	return bSuccess;
}

/*
 * IsUserAdmin()
 *
 * Description:
 *	Routine Description: This routine returns TRUE if the caller's process
 *	is a member of the Administrators local group. Caller is NOT expected to
 *	be impersonating anyone and is expected to be able to open their own process 
 *	and process token.
 *	
 *	Arguments: None. 
 *	Return Value: 
 *		TRUE - Caller has Administrators local group. 
 *		FALSE - Caller does not have Administrators local group. --
 */
BOOL IsUserAdmin(
	IN HANDLE token
)
{
	SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
	PSID AdministratorsGroup = nullptr;

	auto b = AllocateAndInitializeSid(
		&NtAuthority,
		2,
		SECURITY_BUILTIN_DOMAIN_RID,
		DOMAIN_ALIAS_RID_ADMINS,
		0, 0, 0, 0, 0, 0,
		&AdministratorsGroup
	);
	if (b)
	{
		if (!CheckTokenMembership(token, AdministratorsGroup, &b))
		{
			b = FALSE;
		}
		FreeSid(AdministratorsGroup);
	}

	return b;
}

//---------------------------------------------------------------------
// AFPGetSIDforUser()
//---------------------------------------------------------------------
//
PSID AFPGetSIDforUser(
	IN PWCHAR userName,
	   IN PWCHAR domainName
)
{
	UNREFERENCED_PARAMETER(domainName);

	DWORD cbSid = 0;
	DWORD cbReferencedDomainName = 0;
	SID_NAME_USE sidNameUse;
	WCHAR refDomainName[128];

	LookupAccountName(
		nullptr,
		userName,
		nullptr,
		&cbSid,
		nullptr,
		&cbReferencedDomainName,
		&sidNameUse
	);

	auto pSid = LocalAlloc(LMEM_ZEROINIT, cbSid);

	if (pSid != nullptr)
	{
		auto success = LookupAccountName(
			nullptr,
			userName,
			pSid,
			&cbSid,
			refDomainName,
			&cbReferencedDomainName,
			&sidNameUse
		);

		if (!success)
		{
			//
			//For some reason we failed to get the account SID.
			//
			DBGWRITE(DbgLevelInfo, L"[AFPHELP]LookupAccountName() failed getting user SID for [%s] (%d)\n", userName, GetLastError());

			LocalFree(pSid);
			return nullptr;
		}
	}

	return pSid;
}

//---------------------------------------------------------------------
// AFPGetDirectoryRights()
//---------------------------------------------------------------------
//
AFPERROR AFPGetDirectoryRights(
	IN HANDLE token, //login token
	   IN PWCHAR pDirWinPath, //directory to check
	   IN PWCHAR pUserName,
	   IN PWCHAR pDomainName,
	   IN DWORD* afpRights
)
{
	DWORD dwError;
	PACL pDACL = nullptr;
	PSECURITY_DESCRIPTOR pSecurityDescriptor = nullptr;
	PSID pSidGroup = nullptr;
	PSID pSidOwner = nullptr;
	ACCESS_MASK accessRights = 0;
	TRUSTEE trustee;

	DBGWRITE(DbgLevelAll, L"[AFPHELP]AFPGetDirectoryRights() enter...\n");
	DBGWRITE(DbgLevelAll, L"[AFPHELP]user name = %s\n", pUserName);

	*afpRights = 0;

	//
	//First step is to get the security descriptor and SIDs for
	//the directory.
	//
	dwError = GetNamedSecurityInfo(
		pDirWinPath,
		SE_FILE_OBJECT,
		DACL_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION,
		&pSidOwner,
		&pSidGroup,
		&pDACL,
		nullptr,
		&pSecurityDescriptor
	);

	if ((SUCCEEDED(dwError)) && (pSidOwner != nullptr))
	{
		//
		//Check to see if the current user is the owner of the directory. If not, we
		//get the access rights for this user as allowed by it's SID.
		//
		auto userSid = AFPGetSIDforUser(pUserName, pDomainName);

		if (userSid != nullptr)
		{
			if (EqualSid(userSid, pSidOwner))
			{
				DBGWRITE(DbgLevelAll, L"[AFPHELP]Logged on user is owner!!\n");
				DBGWRITE(DbgLevelAll, L"[AFPHELP]Checking access rights for owner\n");

				//
				//Use the handy-dandy routine to build the trustee for us.
				//
				BuildTrusteeWithSid(&trustee, pSidOwner);
				dwError = GetEffectiveRightsFromAcl(pDACL, &trustee, &accessRights);

				DBGWRITE(DbgLevelAll, L"[AFPHELP]GetEffectiveRightsFromAcl() returned %d\n", dwError);

				if (accessRights & (WRITE_OWNER | WRITE_DAC | DELETE))
				{
					*afpRights |= (kAccess_OW | kAccess_UAW);
					DBGWRITE(DbgLevelAll, L"[AFPHELP]Owner has write access!\n");
				}

				if (accessRights & READ_CONTROL)
				{
					*afpRights |= (kAccess_OS | kAccess_OR | kAccess_UAS | kAccess_UAR);
					DBGWRITE(DbgLevelAll, L"[AFPHELP]Owner has read access!\n");
				}

				*afpRights |= kAccess_Owner;
			}
			else
			{
				//
				//Get the explicit access rights for this user.
				//
				accessRights = 0;

				BuildTrusteeWithSid(&trustee, userSid);
				dwError = GetEffectiveRightsFromAcl(pDACL, &trustee, &accessRights);

				if (SUCCEEDED(dwError))
				{
					if (accessRights & (WRITE_OWNER | WRITE_DAC | DELETE))
					{
						*afpRights |= kAccess_UAW;
						DBGWRITE(DbgLevelAll, L"[AFPHELP]User has write access!\n");
					}

					if (accessRights & READ_CONTROL)
					{
						*afpRights |= kAccess_UAS | kAccess_UAR;
						DBGWRITE(DbgLevelAll, L"[AFPHELP]User has read access!\n");
					}

					//
					//Needed to move this check here for Vista since admins don't get
					//owner access information anymore.
					//
					if (IsUserAdmin(token))
					{
						*afpRights |= kAccess_Owner;
					}
				}
			}

			LocalFree(userSid);
		}

		//
		//Now check for guest access.
		//
		//BUGBUG: I cannot find a way to get the name of the guest account. This
		//will fail on international systems as the guest name is different.
		//
		auto guestSID = AFPGetSIDforUser(TEXT("Guest"), nullptr);

		if (guestSID != nullptr)
		{
			//
			//Get the explicit access rights for this user.
			//
			accessRights = 0;

			BuildTrusteeWithSid(&trustee, guestSID);
			dwError = GetEffectiveRightsFromAcl(pDACL, &trustee, &accessRights);

			if (SUCCEEDED(dwError))
			{
				if (accessRights & (WRITE_OWNER | WRITE_DAC | DELETE))
				{
					*afpRights |= kAccess_WW;
					DBGWRITE(DbgLevelAll, L"[AFPHELP]Guest has write access!\n");
				}

				if (accessRights & READ_CONTROL)
				{
					*afpRights |= kAccess_WS | kAccess_WR;
					DBGWRITE(DbgLevelAll, L"[AFPHELP]Guest has read access!\n");
				}
			}

			LocalFree(guestSID);
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

		if (pSecurityDescriptor != nullptr)
		{
			//
			//This pointer was allocate on our behalf by GetNamedSecurityInfo().
			//Clear it now that we're done with it.
			//
			LocalFree(pSecurityDescriptor);
		}
	}
	else
	{
		DBGWRITE(DbgLevelInfo, L"[AFPHELP]GetNamedSecurityInfo() failed getting dir rights (%d)!\n", dwError);
	}

	return dwError;
}

#ifdef __MWERKS__
#pragma pop
#endif

//---------------------------------------------------------------------
// AFPGetUserNameByID()
//---------------------------------------------------------------------
//
AFPERROR AFPGetUserNameByID(
	IN DWORD dwID,
	   IN PWCHAR szName
)
{
	PAFP_USERGROUP_NAME pName = nullptr;

	DBGWRITE(DbgLevelInfo, L"[AFPHELP]Looking for user id %d\n", dwID);

	gUserAndGroupID.Rewind();

	while (gUserAndGroupID.Next(&pName))
	{
		DBGWRITE(DbgLevelInfo, L"[AFPHELP]Found user ID %d\n", pName->id);

		if (pName->id == dwID)
		{
			DBGWRITE(DbgLevelInfo, L"[AFPHELP]Found user name by id!!!! (%s)\n", pName->name);

			lstrcpyn(szName, pName->name, lstrlen(pName->name) + 1);
			return AFP_OK;
		}
	}

	return afpParmErr;
}

//---------------------------------------------------------------------
// AFPGetDirectoryOwnerAndGroupID()
//---------------------------------------------------------------------
//
AFPERROR AFPGetDirectoryOwnerAndGroupID(
	IN PWCHAR pDirWinPath,
	   OUT DWORD* dwOwnerID,
	   OUT DWORD* dwGroupID
)
{
	DWORD dwError;
	PSID pSidGroup = nullptr;
	PSID pSidOwner = nullptr;
	PSECURITY_DESCRIPTOR pSecurityDescriptor = nullptr;

	ASSERT(dwOwnerID != nullptr);
	ASSERT(dwGroupID != nullptr);

	//
	//Initialize ID's to zero, this means no owner or group assigned.
	//
	*dwOwnerID = 0;
	*dwGroupID = 0;

	//
	//First step is to get the security descriptor and SIDs for
	//the directory.
	//
	dwError = GetNamedSecurityInfo(
		pDirWinPath,
		SE_FILE_OBJECT,
		GROUP_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION,
		&pSidOwner,
		&pSidGroup,
		nullptr,
		nullptr,
		&pSecurityDescriptor
	);

	if (SUCCEEDED(dwError))
	{
		WCHAR szName[64];
		WCHAR szDomainName[64];
		DWORD cbName = sizeof(szName);
		DWORD cbDomainName = sizeof(szDomainName);
		DWORD sidType = 0;

		//
		//First we get the name of the owner of the directory.
		//
		auto succeeded = LookupAccountSid(
			nullptr,
			pSidOwner,
			szName,
			&cbName,
			szDomainName,
			&cbDomainName,
			reinterpret_cast<PSID_NAME_USE>(&sidType)
		);

		if (succeeded)
		{
			//
			//We successfully got the owner name from the directory SID. Now we
			//need to convert it to an AFP user ID.
			//
			DBGWRITE(DbgLevelInfo, L"[AFPHELP]Owner name from SID = %s for dir %s\n", szName, pDirWinPath);

			//
			//We only care about user SID's, check to make sure that's what we got.
			//
			if (sidType == SidTypeUser)
			{
				PAFP_USERGROUP_NAME pName = nullptr;

				//
				//First, search to see if the user name is already in our tables with
				//a valid afp user ID.
				//
				gUserAndGroupID.Rewind();

				while (gUserAndGroupID.Next(&pName))
				{
					if ((!lstrcmp(pName->name, szName)) && (pName->type == AFP_USERNAME_TYPE))
					{
						*dwOwnerID = pName->id;

						DBGWRITE(DbgLevelInfo, L"[AFPHELP]Found a matching user ID!\n");
						break;
					}
				}

				//
				//If a matching user name wasn't found in the list, then add it.
				//
				if (*dwOwnerID == 0)
				{
					pName = static_cast<PAFP_USERGROUP_NAME>(LocalAlloc(LMEM_ZEROINIT, sizeof(AFP_USERGROUP_NAME)));

					if (pName)
					{
						*dwOwnerID = gNextUserID++;
						pName->id = *dwOwnerID;
						pName->type = AFP_USERNAME_TYPE;

						lstrcpyn(pName->name, szName, lstrlen(szName) + 1);

						gUserAndGroupID.AddItem(&pName);

						DBGWRITE(DbgLevelInfo, L"[AFPHELP]Added username %s to the array\n", pName->name);
					}
				}
			}
		}
		else
		{
			//
			//We failed to get the owner name. Report it but only in debug.
			//
			dwError = GetLastError();

			DBGWRITE(DbgLevelInfo, L"[AFPHELP]****Error getting owner name for [%s], error code: %d\n",
				pDirWinPath,
				dwError
			);
		}

		cbName = sizeof(szName);
		cbDomainName = sizeof(szDomainName);

		//
		//Get the name of the group that owns the directory.
		//
		succeeded = LookupAccountSid(
			nullptr,
			pSidGroup,
			szName,
			&cbName,
			szDomainName,
			&cbDomainName,
			reinterpret_cast<PSID_NAME_USE>(&sidType)
		);

		if (succeeded)
		{
			//
			//We successfully got the group name from the directory SID. Now we
			//need to convert it to an AFP group ID.
			//
			DBGWRITE(DbgLevelAll, L"[AFPHELP]Group name from SID = %s for dir %s\n", szName, pDirWinPath);

			//
			//We only care about group SID's, check to make sure that's what we got.
			//
			if (sidType == SidTypeGroup)
			{
			}
		}
		else
		{
			//
			//We failed to get the group name. Report it but only in debug.
			//
			dwError = GetLastError();

			DBGWRITE(DbgLevelInfo, L"[AFPHELP]****Error getting group name! (%d)\n", dwError);
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

	return dwError;
}

//---------------------------------------------------------------------
// AFPExtractDomainAndUserName()
//---------------------------------------------------------------------
// Extracts the domain and user name from a string. The string can
// consist of "domain\username" or just "username".
//
// pUserName	contains the string to extract the domain name and
//				will point to just the user name on exit.
// pDomain		buffer that will contain the extracted domain name.
//
void AFPExtractDomainAndUserName(
	IN PWCHAR pUserName,
	   IN size_t cbUserName,
	   OUT PWCHAR pDomain,
	   IN size_t cbDomain
)
{
	WCHAR szUserName[64];
	WCHAR* pTemp;

	if (lstrlen(pUserName) == 0)
	{
		//
		//Empty string was passed, nothing to do.
		//
		return;
	}

	ZeroMemory(szUserName, sizeof(szUserName));

	//
	//First extract the domain name from the big username string.
	//
	pTemp = wcschr(pUserName, '\\');

	if (pTemp)
	{
		lstrcpyn(
			pDomain,
			pUserName,
			min((int)cbDomain, (int)((pTemp-pUserName)+1))
		);
	}
	else
	{
		//
		//No domain name supplied, so zero out the buffer.
		//
		ZeroMemory(pDomain, cbDomain);
	}

	//
	//Now get just the username in the username buffer.
	//
	lstrcpyn(szUserName, pUserName, sizeof(szUserName));

	pTemp = wcschr(szUserName, '\\');

	if (pTemp)
	{
		StringCbCopy(pUserName, cbUserName, pTemp + 1);
	}

	//
	//Note, if we didn't find a '\' in the username, then we'll just
	//pass back whatever we got as the user name untouched.
	//
}

//---------------------------------------------------------------------
// AFPWindowsToMacTime()
//---------------------------------------------------------------------
// Converts Windows time to Mac AFP time.
ULONG AFPWindowsToMacTime(
	IN PFILETIME filetime
)
{
	LARGE_INTEGER seconds;
	LARGE_INTEGER fTime;
	LARGE_INTEGER SecondsToStartOf1980;

	SecondsToStartOf1980.LowPart = 0xc8df3700;
	SecondsToStartOf1980.HighPart = 0x00000002;

	fTime.LowPart = filetime->dwLowDateTime;
	fTime.HighPart = filetime->dwHighDateTime;

	seconds.QuadPart = fTime.QuadPart / NUM_100ns_PER_SECOND;
	seconds.QuadPart = seconds.QuadPart - SecondsToStartOf1980.QuadPart;

	return seconds.LowPart - SECONDS_FROM_1980_2000;
}

//---------------------------------------------------------------------
// AFPWindowsToMacLocalTime()
//---------------------------------------------------------------------
// Converts Windows time to Mac AFP time.
ULONG AFPWindowsToMacLocalTime(
	IN PFILETIME filetime
)
{
	LARGE_INTEGER localTime;
	LARGE_INTEGER seconds;
	LARGE_INTEGER SecondsToStartOf1980;

	SecondsToStartOf1980.LowPart = 0xc8df3700;
	SecondsToStartOf1980.HighPart = 0x00000002;

	FileTimeToLocalFileTime(filetime, reinterpret_cast<PFILETIME>(&localTime));

	seconds.QuadPart = localTime.QuadPart / NUM_100ns_PER_SECOND;
	seconds.QuadPart = seconds.QuadPart - SecondsToStartOf1980.QuadPart;

	return seconds.LowPart - SECONDS_FROM_1980_2000;
}

//---------------------------------------------------------------------
// AFPMacToWindowsTime()
//---------------------------------------------------------------------
// Converts Mac AFP time to Windows time.
void AFPMacToWindowsTime(
	IN ULONG dwMacTime,
	   IN PFILETIME filetime
)
{
	LARGE_INTEGER localTime;
	LARGE_INTEGER SecondsToStartOf1980;

	SecondsToStartOf1980.LowPart = 0xc8df3700;
	SecondsToStartOf1980.HighPart = 0x00000002;

	dwMacTime += SECONDS_FROM_1980_2000;

	localTime.QuadPart = dwMacTime + SecondsToStartOf1980.QuadPart;
	localTime.QuadPart = localTime.QuadPart * NUM_100ns_PER_SECOND;

	filetime->dwLowDateTime = localTime.LowPart;
	filetime->dwHighDateTime = localTime.HighPart;
}

//---------------------------------------------------------------------
// AFPMacToWindowsLocalTime()
//---------------------------------------------------------------------
// Converts Mac AFP time to Windows time and converts the UTC time
// to local (calculating time zones).
void AFPMacToWindowsLocalTime(
	IN ULONG dwMacTime,
	   IN PFILETIME fileTime
)
{
	FILETIME utcTime;

	AFPMacToWindowsTime(dwMacTime, &utcTime);
	FileTimeToLocalFileTime(&utcTime, fileTime);
}

//---------------------------------------------------------------------
// OpenCurrentToken()
//---------------------------------------------------------------------
// Open a token for the privileges. First attempts on current thread.
//
BOOL OpenCurrentToken(
	IN OUT PHANDLE Token
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
		Token))
	{
		//
		//Couldn't open a thread token, so now open one for
		//the process.
		//
		if (!OpenProcessToken(
			GetCurrentProcess(),
			TOKEN_ADJUST_PRIVILEGES,
			Token))
		{
			DBGWRITE(DbgLevelInfo, L"[SEC]OpenProcessToken() failed! (%d)\n", GetLastError());
			return FALSE;
		}
	}

	return TRUE;
}

//---------------------------------------------------------------------
// AFPAdjustPrivilege()
//---------------------------------------------------------------------
// Adjusts the privilege of a token. If no token handle is not supplied
// then we will get the token for the current thread or process.
//
BOOL AFPAdjustPrivilege(
	IN PWCHAR szPrivilege,
	   IN BOOL bEnable,
	   IN HANDLE hToken
)
{
	auto Token = INVALID_HANDLE_VALUE;
	LUID luid;
	TOKEN_PRIVILEGES privs;
	DWORD dwError;

	if (!LookupPrivilegeValue(nullptr, szPrivilege, &luid))
	{
		DBGWRITE(DbgLevelInfo, L"[SEC]LookupPrivilegeValue() failed for %s! (%d)\n", szPrivilege, GetLastError());
		return FALSE;
	}

	//
	//If hToken was not supplied, then we get the token for the
	//currently running thread.
	//
	if (hToken == INVALID_HANDLE_VALUE)
	{
		if (!OpenCurrentToken(&Token))
		{
			DBGWRITE(DbgLevelInfo, L"[SEC]OpenCurrentToken() failed! (%d)\n", GetLastError());
			return FALSE;
		}
	}
	else
	{
		Token = hToken;
	}

	privs.PrivilegeCount = 1;
	privs.Privileges[0].Luid = luid;
	privs.Privileges[0].Attributes = bEnable ? SE_PRIVILEGE_ENABLED : 0;

	//
	//Here's the real API call that adjusts the privileges.
	//
	if (!AdjustTokenPrivileges(
		Token,
		FALSE,
		&privs,
		0,
		nullptr,
		nullptr))
	{
		dwError = GetLastError();

		DBGWRITE(DbgLevelInfo, L"[SEC]AdjustTokenPrivileges() failed! (%d)\n", dwError);

		//
		//If we have an invalid handle, we don't want to make a call on
		//it at this stage or we'll die. Only close if we created the token
		//handle locally.
		//
		if ((dwError != ERROR_INVALID_HANDLE) && (hToken == INVALID_HANDLE_VALUE))
		{
			CloseHandle(Token);
		}

		return FALSE;
	}

	DBGWRITE(DbgLevelAll, L"[SEC]AdjustTokenPrivileges() succeeded for %s\n", szPrivilege);

	//
	//We only want to close the Token handle if we created it
	//locally with OpenCurrentToken().
	//
	if (hToken == INVALID_HANDLE_VALUE)
	{
		CloseHandle(Token);
	}

	return TRUE;
}

//---------------------------------------------------------------------
// AFPAdjustAccountRight()
//---------------------------------------------------------------------
// Adjust the account rights for the supplied user.
//
BOOL AFPAdjustAccountRight(
	IN HANDLE hUserToken,
	   PWCHAR szPriv,
	   USHORT cbPriv //count in bytes the size of the szPriv buffer
)
{
	UNREFERENCED_PARAMETER(hUserToken);

	LSA_OBJECT_ATTRIBUTES objectAttributes;
	LSA_UNICODE_STRING userRights;
	PSID accountSID = nullptr;
	auto policyHandle = INVALID_HANDLE_VALUE;

	//
	//First open a policy object for the local system.
	//
	ZeroMemory(&objectAttributes, sizeof(objectAttributes));

	auto status = LsaOpenPolicy(nullptr, &objectAttributes, POLICY_LOOKUP_NAMES | POLICY_CREATE_ACCOUNT, &policyHandle);

	if (status == ERROR_SUCCESS)
	{
		ZeroMemory(&userRights, sizeof(userRights));

		//
		//Build the LSA unicode string structure
		//
		userRights.Length = static_cast<USHORT>(lstrlen(szPriv));
		userRights.MaximumLength = cbPriv;
		userRights.Buffer = szPriv;

		status = LsaAddAccountRights(
			policyHandle,
			accountSID,
			&userRights,
			1
		);

		LsaClose(policyHandle);

		if (status != ERROR_SUCCESS)
		{
			DBGWRITE(DbgLevelInfo, L"[SEC:AFPAdjustAccountRight]LsaAddAccountRights() failed %lu\n", LsaNtStatusToWinError(status));
			return FALSE;
		}
	}
	else
	{
		DBGWRITE(DbgLevelInfo, L"[SEC:AFPAdjustAccountRight]LsaOpenPolicy() failed %lu\n", LsaNtStatusToWinError(status));
		return FALSE;
	}

	return TRUE;
}

//---------------------------------------------------------------------
// AFPImpersonateUser()
//---------------------------------------------------------------------
// Workhorse routine for impersonating a logged in client. We have to
// do some special handling, especially for Vista, to ensure that
// the logged on user token has the privilege to impersonate on this
// machine.
//
// NOTE: In order to set the impersonation privilege, the logged on user
// must be enabled as an impersonable user. To do this, go to the Local 
// Security Settings in the control panel, go to Local Policies\User Rights
// Assignment and add your user to the 'Impersonate a client after 
// authentication' policy.
//
BOOL AFPImpersonateUser(
	IN HANDLE hUserToken
)
{
	auto success = AFPAdjustPrivilege(SE_IMPERSONATE_NAME, TRUE);

	if (success)
	{
		success = ImpersonateLoggedOnUser(hUserToken);

		DBGWRITE(DbgLevelAll, L"[SEC:AFPImpersonateUser]Impersonation succeeded:  %s\n", (success) ? L"YES" : L"NO");
	}

	return success;
}

//---------------------------------------------------------------------
// AFPRevertToSelf()
//---------------------------------------------------------------------
// Reciprocal of above.
//
BOOL AFPRevertToSelf(
	IN HANDLE hUserToken
)
{
	UNREFERENCED_PARAMETER(hUserToken);

	BOOL success;

	success = RevertToSelf();
	AFPAdjustPrivilege(SE_IMPERSONATE_NAME, FALSE);

	return success;
}

//---------------------------------------------------------------------
// AFPMacToWindowsString()
//---------------------------------------------------------------------
// Converts a Mac format string into a windows unicode string.
//
PWCHAR AFPMacToWindowsString(
	IN PCHAR pMacString,
	   IN WORD cbWideString,
	   OUT PWCHAR pWideString
)
{
	if (strlen(pMacString) == 0)
	{
		pWideString[0] = '\0';
		return pWideString;
	}

	auto wSize = MultiByteToWideChar(
		CP_MACCP,
		0,
		pMacString,
		static_cast<int>(strlen(pMacString)),
		pWideString,
		cbWideString
	);

	if (wSize == 0)
	{
		auto dwError = GetLastError();

		DBGWRITE(DbgLevelInfo, L"[HELP]MultiByteToWideChar() failed in AFPMacToWindowsString()! (%d)\n", dwError);
		return nullptr;
	}

	pWideString[wSize] = '\0';

	return pWideString;
}

//---------------------------------------------------------------------
// AFPMacToWindowsPath()
//---------------------------------------------------------------------
// Converts a Mac format pathname into a windows pathname.
//
PWCHAR AFPMacToWindowsPath(
	IN PCHAR pAFPPath,
	   OUT PWCHAR pWidePath,
	   OUT AFPCHARMAPARRAY pCharMap
)
{
	auto dwLen = strlen(pAFPPath);

	if (dwLen == 0)
	{
		pWidePath[0] = '\0';
		return pWidePath;
	}

	if (pCharMap != nullptr)
	{
		memset(pCharMap, 0, sizeof(AFPCHARMAPARRAY));
	}

	WORD wCharMapPos = 0;

	for (WORD i = 0; i < dwLen; i++)
	{
		//
		//Make sure we're ansi chars and make sure the name doesn't contain
		//one of the illegal file characters for Windows.
		//
		if (pAFPPath[i] < FIRST_VALID_ANSI_CHAR ||
			static_cast<UCHAR>(pAFPPath[i]) > LAST_VALID_ANSI_CHAR ||
			pAFPPath[i] == '\\' ||
			pAFPPath[i] == '/' ||
			pAFPPath[i] == '"' ||
			pAFPPath[i] == '?' ||
			pAFPPath[i] == '*' ||
			pAFPPath[i] == '<' ||
			pAFPPath[i] == '>' ||
			pAFPPath[i] == '|')
		{
			//
			//If pCharMap is not null, then we are building the char map for
			//the index entry as well.
			//
			if (pCharMap != nullptr)
			{
				pCharMap[wCharMapPos].wKeyCode = static_cast<BYTE>(pAFPPath[i]);
				pCharMap[wCharMapPos].wOffset = i;

				DBGWRITE(DbgLevelAll, L"[HELP]Adding keycode %d offset %d\n",
					pCharMap[wCharMapPos].wKeyCode,
					pCharMap[wCharMapPos].wOffset);

				wCharMapPos++;
			}

			pAFPPath[i] = REPLACEMENT_CHAR;
		}
		else
		{
			switch (pAFPPath[i])
			{
				//
				//The macintosh path seperator is a ":", convert to windows path
				//seperator of "/". Note that we don't save for conversion back,
				//since we'll never send paths back to the mac client.
				//
			case ':':
				pAFPPath[i] = '/';
				break;

				//
				//Windows can't handle multiple periods in a row, we need to replace
				//one of them. We also can't have the last character in the file name
				//be a period.
				//
			case '.':
				if (pAFPPath[i + 1] == '.' || i == dwLen - 1)
				{
					if (pCharMap != nullptr)
					{
						pCharMap[wCharMapPos].wKeyCode = static_cast<BYTE>(pAFPPath[i]);
						pCharMap[wCharMapPos].wOffset = i;

						wCharMapPos++;
					}

					pAFPPath[i] = REPLACEMENT_CHAR;
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
	if (IsWhiteSpace(pAFPPath[dwLen - 1]))
	{
		pAFPPath[dwLen - 1] = '\0';
	}

	//
	//So we know where to stop when reading the char table.
	//
	if (pCharMap != nullptr)
	{
		pCharMap[wCharMapPos].wKeyCode = 0;
		pCharMap[wCharMapPos].wOffset = 0;
	}

	auto wSize = MultiByteToWideChar(
		CP_MACCP,
		0,
		pAFPPath,
		static_cast<int>(strlen(pAFPPath)),
		pWidePath,
		MAX_PATH);

	if (wSize == 0)
	{
		auto dwError = GetLastError();

		DBGWRITE(DbgLevelInfo, L"[HELP]MultiByteToWideChar() failed! (%d)\n", dwError);
		return nullptr;
	}

	pWidePath[wSize] = '\0';

	return pWidePath;
}

//---------------------------------------------------------------------
// IsWhiteSpace()
//---------------------------------------------------------------------
// A white space checker that doesn't choke on negative values.
//
bool IsWhiteSpace(char c)
{
	return (c == ' ' || c == '\t' || c == '\r' || c == '\n');
}

//---------------------------------------------------------------------
// AFPWindowsToMacPath()
//---------------------------------------------------------------------
// Converts a Windows format pathname into a mac pathname.
//
PCHAR AFPWindowsToMacPath(
	IN PWCHAR pWinPath,
	   IN WORD wAFPBuffSize,
	   IN AFPCHARMAPARRAY pCharMap,
	   OUT PCHAR pAFPPath
)
{
	auto dwLen = lstrlen(pWinPath);

	if (dwLen == 0)
	{
		pAFPPath[0] = '\0';
		return pAFPPath;
	}

	BOOL fUsedDefaultChar;
	auto wSize = WideCharToMultiByte(
		CP_MACCP,
		0,
		pWinPath,
		static_cast<int>(lstrlen(pWinPath)),
		pAFPPath,
		wAFPBuffSize,
		"!",
		&fUsedDefaultChar);

	if (wSize == 0)
	{
		auto dwError = GetLastError();
		DBGWRITE(DbgLevelInfo, L"[HELP]WideCharToMultiByte() failed! (%d)\n", dwError);
	}

	pAFPPath[wSize] = '\0';

	if (pCharMap != nullptr)
	{
		auto i = 0;

		while (pCharMap[i].wKeyCode != 0)
		{
			if (pCharMap[i].wOffset > wSize)
			{
				break;
			}

			pAFPPath[pCharMap[i].wOffset] = static_cast<char>(pCharMap[i].wKeyCode);

			DBGWRITE(DbgLevelAll, L"[HELP]Converting keycode %d offset %d\n",
				pCharMap[i].wKeyCode,
				pCharMap[i].wOffset);

			i++;
		}
	}

	return pAFPPath;
}

//---------------------------------------------------------------------
// AFPGetObjectNameFromPath()
//---------------------------------------------------------------------
// Returns just the file or dir name from a full pathname.
//
void AFPGetObjectNameFromPath(
	IN PWCHAR pPath,
	   OUT PWCHAR pNameBuff,
	   IN size_t cbNameBuff
)
{
	auto wLen = lstrlen(pPath);

	if (wLen == 0)
	{
		return;
	}

	//
	//When this loop completes, szTemp should contain the
	//object name.
	//
	auto i = wLen - 1;

	do
	{
		if (pPath[i] == '\\')
		{
			i++;
			break;
		}

		i--;
	}
	while (i >= 0);

	StringCbCopy(pNameBuff, cbNameBuff, &pPath[max(0, i)]);

	//
	//BUGBUG: Add code here to check for proper length of filename!
	//
}

//---------------------------------------------------------------------
// AFPGetObjectDot3Extension()
//---------------------------------------------------------------------
// Returns just the last characters after the last period.
//
void AFPGetObjectDot3Extension(
	IN PWCHAR pPath,
	   OUT PWCHAR pNameBuff,
	   IN size_t cbNameBuff
)
{
	auto wLen = lstrlen(pPath);

	if (wLen == 0)
	{
		return;
	}

	//
	//When this loop completes, szTemp should contain the
	//object extension.
	//
	auto i = wLen - 1;
	auto j = 0;

	do
	{
		if (pPath[i] == '.')
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
	StringCbCopy(pNameBuff, cbNameBuff, &pPath[max(0, i)]);
}

//---------------------------------------------------------------------
// AFPGetObjectPath()
//---------------------------------------------------------------------
// Returns just the path to a file or dir. For example, if the full
// path to a file is c:\dir1\dir2\dir3\filename, then this function
// will return c:\dir1\dir2\dir3.
//
void AFPGetObjectPath(
	IN PWCHAR pPath,
	   OUT PWCHAR pPathBuff
)
{
	auto wLen = lstrlen(pPath);

	pPathBuff[0] = '\0';

	if (wLen == 0)
	{
		return;
	}

	auto j = wLen - 1;
	auto i = 0;

	do
	{
		if (pPath[j] == '\\')
		{
			break;
		}

		i++;
		j--;
	}
	while (i < wLen);

	lstrcpyn(pPathBuff, pPath, wLen - i);
}

//---------------------------------------------------------------------
// AFPMapWinErrorToAFP()
//---------------------------------------------------------------------
// Maps a Windows error code to an Afp error code.
//
AFPERROR AFPMapWinErrorToAFP(
	IN DWORD dwWinError, //Windows code to map
	   IN DWORD dwDefaultError //Default AFP error to use if no map found
)
{
	switch (dwWinError)
	{
	case ERROR_LOCK_VIOLATION: return (afpLockErr);
	case ERROR_FILE_NOT_FOUND: return (afpObjectNotFound);
	case ERROR_PATH_NOT_FOUND: return (afpObjectNotFound);
	case ERROR_INVALID_PARAMETER: return (afpParmErr);
	case ERROR_INVALID_NAME: return (afpParmErr);
	case ERROR_ACCESS_DENIED: return (afpAccessDenied);
	case ERROR_HANDLE_DISK_FULL: return (afpDiskFull);
	case ERROR_FILE_EXISTS: return (afpObjectExists);
	case ERROR_DIR_NOT_EMPTY: return (afpDirNotEmpty);
	case ERROR_HANDLE_EOF: return (afpEofError);
	case ERROR_INVALID_LOGON_HOURS: return (afpUserNotAuth);
	case ERROR_PASSWORD_EXPIRED: return (afpPwdExpiredErr);

	default: return dwDefaultError;
	}
}

//---------------------------------------------------------------------
// AFPReAlloc()
//---------------------------------------------------------------------
// Windows realloc sucks! Here's my own inefficient one. :-(
//
PVOID AFPReAlloc(
	IN PCHAR pOldBuffer,
	   IN DWORD dwOldBufferSize,
	   IN DWORD dwNewSize,
	   IN BOOL fCopyOldToNew
)
{
	auto pNewBuffer = static_cast<PCHAR>(DBG_ALLOC(dwNewSize));
	if (pNewBuffer == nullptr)
	{
		return nullptr;
	}

	//
	//Copy the contents of the old buffer
	//
	if (fCopyOldToNew)
	{
		RtlCopyMemory(pNewBuffer, pOldBuffer, min(dwOldBufferSize, dwNewSize));
	}

	//
	//Free the old buffer pointer.
	//
	DBG_FREE(pOldBuffer);

	return pNewBuffer;
}


//---------------------------------------------------------------------
// AFPExtractStringAndAdvanceBuffPtr()
//---------------------------------------------------------------------
// Extract a string from a buffer and advance the buffer pointer
// to the end of the string (to the next parameter in an AFP command).
//

HRESULT AFPExtractStringAndAdvanceBuffPtr(
	IN PCHAR* pBuffer, //Buffer containing string to extract
	   IN PWCHAR pWinString, //Wide buffer to hold extracted unicode string
	   IN WORD cchWinString, //Size in characters of pWinString
	   IN BOOL fIsPath, //Is the string a pathname to a file?
	   OPTIONAL LPAFPCHARMAP pCharMap //Optional charmap table for Mac->Win conversion
)
{
	auto pTempString = *pBuffer;
	CHAR szTempString[MAX_AFP_PATH];

	pWinString[0] = '\0';

	//
	//Validate the data sent to us by the caller.
	//
	if (pTempString == nullptr || pWinString == nullptr)
	{
		return (ERROR_INVALID_DATA);
	}

	//
	//If there is no string to extract, save some cycles and exit.
	//
	if (static_cast<BYTE>(pTempString[0]) > 0)
	{
		//
		//Check the length of the pascal string. If it's too big,
		//we bail. We add one for the null trailer.
		//
		if (static_cast<BYTE>(pTempString[0]) + 1 > cchWinString)
		{
			//
			//The supplied buffer is not big enough to hold the
			//string passed from the client.
			//
			return ERROR_NOT_ENOUGH_MEMORY;
		}

		//
		//Because the buffer we're passed does not contain a null
		//terminated string, we need to make one to pass to the
		//AFP conversion routine.
		//
		RtlCopyMemory(szTempString, &pTempString[1], static_cast<BYTE>(pTempString[0]));
		szTempString[static_cast<BYTE>(pTempString[0])] = '\0';

		//
		//Call the AFP helper function that does the Mac ANSI to
		//Windows UNICODE conversion.
		//
		if (fIsPath)
		{
			//
			//Set the fIsPath parameter if this is a pathname in an AFP command.
			//
			if (AFPMacToWindowsPath(szTempString, pWinString, pCharMap) == nullptr)
			{
				//
				//There was a failure in the conversion process. Return
				//appropriate error code.
				//
				return (GetLastError());
			}
		}
		else
		{
			if (AFPMacToWindowsString(szTempString, cchWinString, pWinString) == nullptr)
			{
				return GetLastError();
			}
		}
	}

	*pBuffer += sizeof(BYTE) + static_cast<BYTE>(pTempString[0]);

	return NO_ERROR;
}
