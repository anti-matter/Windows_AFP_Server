//===================================================================
// AFPRegistry.cpp
//===================================================================
// This class is used to manage the entries in the registry that
// the afp server uses.

#include "StdAfx.h"
#include <strsafe.h>

#include "AFPRegistry.h"
#include "afpdebug.h"
#include "fdparms.h"


//---------------------------------------------------------------------
// AFPRegistry()
//---------------------------------------------------------------------
// Initialize the registry class.
//

AFPRegistry::AFPRegistry()
{
	mVolumeKey		= NULL;
	mVolumeEnumKey	= NULL;
	mAddVolumeKey	= NULL;	
	mBaseKey		= NULL;
}


//---------------------------------------------------------------------
// ~AFPRegistry()
//---------------------------------------------------------------------
// Destructor
//

AFPRegistry::~AFPRegistry()
{
	if (mVolumeKey != NULL) {
		RegCloseKey(mVolumeKey);
	}

	if (mVolumeEnumKey != NULL) {
		RegCloseKey(mVolumeEnumKey);
	}

	if (mAddVolumeKey != NULL) {
		RegCloseKey(mAddVolumeKey);
	}

	if (mBaseKey != NULL) {
		RegCloseKey(mBaseKey);
	}
}


//---------------------------------------------------------------------
// OpenBaseKey()
//---------------------------------------------------------------------
// Open the base parameters reg key.
//

DWORD
AFPRegistry::OpenBaseKey()
{
	DWORD	dwDisposition;
	DWORD	dwError;

	if (mBaseKey != NULL)
	{
		//
		//The key is already open
		//
		return(NO_ERROR);
	}

	dwError = RegCreateKeyEx(
				HKEY_CURRENT_USER,
				SUBKEY_BASEPARAMS,
				0,
				NULL,
				REG_OPTION_NON_VOLATILE,
				KEY_ALL_ACCESS,
				NULL,
				&mBaseKey,
				&dwDisposition);
				
	if (dwError != ERROR_SUCCESS)
	{
		DBGWRITE(DbgLevelInfo, L"[AFPRegistry]RegCreateKeyEx() failed opening base key, error code = %d\n", dwError);
		return(dwError);
	}

	return(ERROR_SUCCESS);
}


//---------------------------------------------------------------------
// OpenVolumeListRegKey()
//---------------------------------------------------------------------
// Open the volumes reg key for enumeration.
//

BOOL
AFPRegistry::OpenVolumeListRegKey()
{
	DWORD	dwDisposition;
	DWORD	dwError;

	if (mVolumeEnumKey != NULL)
	{
		DBGWRITE(DbgLevelInfo, L"[AFPRegistry]Volume enum key already open\n");
		return(true);
	}

	dwError = RegCreateKeyEx(
				HKEY_CURRENT_USER,
				SUBKEY_VOLUMES,
				0,
				NULL,
				REG_OPTION_NON_VOLATILE,
				KEY_ALL_ACCESS,
				NULL,
				&mVolumeEnumKey,
				&dwDisposition);
				
	if (dwError != ERROR_SUCCESS)
	{
		DBGWRITE(DbgLevelInfo, L"[AFPRegistry]RegCreateKeyEx() failed, error code = %d\n", dwError);
		return(false);
	}
	else
	{
		//
		//Initialize our starting index position.
		//
		mEnumIndex = 0;
	}

	return(true);
}


//---------------------------------------------------------------------
// OpenVolumeRegKey()
//---------------------------------------------------------------------
// Open the volumes reg key for access.
//

BOOL
AFPRegistry::OpenVolumeRegKey(
	IN WCHAR* szShareName
)
{
	WCHAR	szRegPath[128];
	DWORD	dwDisposition;
	DWORD	dwError;

	if (mVolumeKey != NULL)
	{
		DBGWRITE(DbgLevelInfo, L"[AFPRegistry]Volume key already open\n");
		return(false);
	}

	StringCbPrintf(szRegPath, sizeof(szRegPath), TEXT("%s\\%s"), SUBKEY_VOLUMES, szShareName);

	dwError = RegCreateKeyEx(
				HKEY_CURRENT_USER,
				szRegPath,
				0,
				NULL,
				REG_OPTION_NON_VOLATILE,
				KEY_ALL_ACCESS,
				NULL,
				&mVolumeKey,
				&dwDisposition);
				
	if (dwError != ERROR_SUCCESS)
	{
		DBGWRITE(DbgLevelInfo, L"[AFPRegistry::OpenVolumeRegKey]RegCreateKeyEx() failed, error code = %d\n", dwError);
		return(false);
	}

	return(true);
}



//---------------------------------------------------------------------
// NextVolume()
//---------------------------------------------------------------------
// Get the next volume in the registry. Use this, with mEnumIndex
// starting at 0 to enumerate all the volumes the server is to
// publish.
//

BOOL
AFPRegistry::NextVolume(
	OUT	TCHAR*	szShareName,
	IN	WORD	cbShareName,
	OUT	TCHAR*	szSharePath,
	IN	WORD	cbSharePath,
	OUT CHAR*	szPassword,
	OUT DWORD*	flags
)
{
	FILETIME			fileTime;
	TCHAR				szKeyName[64];
	TCHAR				szVolKey[64];
	TCHAR				szPath[MAX_PATH];
	TCHAR				szPass[24];
	DWORD				dwDataSize, dwType, dwError;
	HKEY				hVolKey;

	dwDataSize	= sizeof(szKeyName);
	dwError		= RegEnumKeyEx(mVolumeEnumKey, mEnumIndex++, szKeyName, &dwDataSize, NULL, NULL, 0, &fileTime);

	if (dwError != ERROR_SUCCESS)
	{
		if (dwError != ERROR_NO_MORE_ITEMS)
		{
			DBGWRITE(DbgLevelInfo, L"[AFPRegistry]RegEnuKeyEx() failed with error = %d\n", dwError);
		}

		//
		//There were no more entries in the registry to return. Pass false back.
		//
		return(false);
	}

	//
	//Build the registry path to the volume sub key that we need to add.
	//
	StringCbPrintf(szVolKey, sizeof(szVolKey), TEXT("%s\\%s"), SUBKEY_VOLUMES, szKeyName);

	dwError = RegOpenKeyEx(
					HKEY_CURRENT_USER,
					szVolKey,
					0,
					KEY_ALL_ACCESS,
					&hVolKey);

	if (dwError == ERROR_SUCCESS)
	{
		dwDataSize	= sizeof(szPath);
		dwError		= RegQueryValueEx(hVolKey, TEXT("path"), NULL, &dwType, (LPBYTE)szPath, &dwDataSize);

		if (dwError == ERROR_SUCCESS)
		{
			DBGWRITE(DbgLevelInfo, L"[AFPRegistry]Found volume path in registry = %s\n", szPath);
			
			//
			//Copy the sharename and path into our return variables.
			//
			StringCbCopy(szShareName, cbShareName, szKeyName);
			StringCbCopy(szSharePath, cbSharePath, szPath);
			
			//
			//Now we check for a password associated with the volume.
			//
			RtlZeroMemory(szPassword, sizeof(szPassword));

			dwDataSize	= sizeof(szPass);
			dwError		= RegQueryValueEx(hVolKey, TEXT("password"), NULL, &dwType, (LPBYTE)szPass, &dwDataSize);
			
			if (dwError == ERROR_SUCCESS)
			{
				CHAR	szAnsiPassword[10];
				BOOL	fUsedDefault;

				RtlZeroMemory(szAnsiPassword, sizeof(szAnsiPassword));

				//
				//We have a password associated with this volume, save it. Note that
				//the password is saved in the registry as unicode.
				//
				WideCharToMultiByte(
						CP_MACCP,
						0,
						szPass,
						dwDataSize,
						(PCHAR)szAnsiPassword,
						sizeof(PassWord),
						"!",
						&fUsedDefault);

				//strcpy(szPassword, szAnsiPassword);
				strcpy_s(szPassword, sizeof(PassWord), szAnsiPassword);
			}
		}
		else {
			return(false);
		}

		dwDataSize = sizeof(DWORD);
		dwError		= RegQueryValueEx(hVolKey, AFPREG_VOLUME_FLAGS, NULL, &dwType, (LPBYTE)flags, &dwDataSize);

		if (dwError != ERROR_SUCCESS)
		{
			DBGWRITE(DbgLevelInfo, L"[AFPRegistry]Failed to get volume flags key\n");
		}

		RegCloseKey(hVolKey);
	}
	else {
		return(false);
	}

	return(true);
}


//---------------------------------------------------------------------
// AddVolume()
//---------------------------------------------------------------------
// Add a new volume in the registry.
//

DWORD
AFPRegistry::AddVolume(
	IN	TCHAR*	szShareName,
	IN	TCHAR*	szSharePath,
	IN	TCHAR*	szPassword,
	IN	DWORD	flags
)
{
	TCHAR		szRegPath[64];
	DWORD		dwDisposition;
	DWORD		dwError = ERROR_SUCCESS;

	//
	//This is the new reg key we will create.
	//
	StringCbPrintf(szRegPath, sizeof(szRegPath), TEXT("%s\\%s"), SUBKEY_VOLUMES, szShareName);

	dwError = RegCreateKeyEx(
					HKEY_CURRENT_USER,
					szRegPath,
					0,
					NULL,
					REG_OPTION_NON_VOLATILE,
					KEY_ALL_ACCESS,
					NULL,
					&mAddVolumeKey,
					&dwDisposition);

	if (dwError == ERROR_SUCCESS)
	{
		if (dwDisposition == REG_OPENED_EXISTING_KEY)
		{
			//
			//The registry entry already exists which means this directory is already
			//shared out. Return the appropriate error code.
			//
			return(AFPERR_REGISTRY_ALREADYSHARED);
		}

		dwError = RegSetValueEx(
						mAddVolumeKey,
						AFPREG_VOLUME_PATH,
						0,
						REG_SZ,
						(BYTE*)szSharePath,
						(lstrlen(szSharePath) * sizeof(TCHAR)) + sizeof(TCHAR));

		if (dwError != ERROR_SUCCESS)
		{
			//
			//For some reason (a rarity??), we could not add the value to
			//the reg key.
			//
			return(AFPERR_REGISTRY_ADDFAILED);
		}

		//
		//Add the [optional] password to the registry.
		//
		if ((szPassword) && (lstrlen(szPassword) > 0))
		{
			dwError = RegSetValueEx(
							mAddVolumeKey,
							AFPREG_VOLUME_PASSWORD,
							0,
							REG_SZ,
							(BYTE*)szPassword,
							(lstrlen(szPassword) * sizeof(TCHAR)) + sizeof(TCHAR));

			if (dwError != ERROR_SUCCESS)
			{
				//
				//The operation failed for some reason, at this point the other
				//registry entries are already made though!
				//
				return(AFPERR_REGISTRY_ADDPASSFAILED);
			}
		}

		//
		//Add the volume flags to the registry.
		//
		dwError = RegSetValueEx(
						mAddVolumeKey,
						AFPREG_VOLUME_FLAGS,
						0,
						REG_DWORD,
						(BYTE*)&flags,
						sizeof(DWORD)
						);

		if (dwError != ERROR_SUCCESS)
		{
			//
			//For some reason (a rarity??), we could not add the value to
			//the reg key.
			//
			return(AFPERR_REGISTRY_ADDFAILED);
		}

		RegCloseKey(mAddVolumeKey);

		mAddVolumeKey = NULL;
	}
	
	return(ERROR_SUCCESS);
}


//---------------------------------------------------------------------
// RemoveVolume()
//---------------------------------------------------------------------
// Remove a volume from the registy.
//

DWORD
AFPRegistry::RemoveVolume(
	IN	TCHAR*	szShareName
)
{
	TCHAR	szRegPath[64];

	StringCbPrintf(szRegPath, sizeof(szRegPath), TEXT("%s\\%s"), SUBKEY_VOLUMES, szShareName);

	return(RegDeleteKey(HKEY_CURRENT_USER, szRegPath));
}


//---------------------------------------------------------------------
// SaveLogonMessage()
//---------------------------------------------------------------------
// Save the logon message in the registry.
//

DWORD
AFPRegistry::SaveLogonMessage(
	IN	TCHAR*	szLogonMessage
)
{
	DWORD	dwError;

	OpenBaseKey();

	dwError = RegSetValueEx(
					mBaseKey,
					AFPREG_BASE_LOGONMSG,
					0,
					REG_SZ,
					(BYTE*)szLogonMessage,
					(lstrlen(szLogonMessage) * sizeof(TCHAR)) + sizeof(TCHAR));

	if (dwError != ERROR_SUCCESS)
	{
		//
		//For some reason (a rarity??), we could not add the value to
		//the reg key.
		//
		return(AFPERR_REGISTRY_ADDFAILED);
	}

	return(NO_ERROR);
}


//---------------------------------------------------------------------
// GetLogonMessage()
//---------------------------------------------------------------------
// Retrieve the logon message from the registry. We assume that szLogonMessage
// is at least AFP_MSG_MAXLEN in size.
//

DWORD
AFPRegistry::GetLogonMessage(
	OUT TCHAR* szLogonMessage
)
{
	DWORD	dwDataSize;
	DWORD	dwError;
	DWORD	dwType;

	OpenBaseKey();

	dwDataSize	= (AFP_MSG_MAXLEN*sizeof(TCHAR)) + sizeof(TCHAR);
	dwError		= RegQueryValueEx(mBaseKey, AFPREG_BASE_LOGONMSG, NULL, &dwType, (LPBYTE)szLogonMessage, &dwDataSize);

	if (dwError != NO_ERROR)
	{
		DBGWRITE(DbgLevelInfo, L"[AFPRegistry]GetLogonMsg failed = %d\n", dwError);
		dwError = AFPERR_REGISTRY_NOLOGONMSG;
	}

	return(dwError);
}


//---------------------------------------------------------------------
// GetForceEncryptedLogon()
//---------------------------------------------------------------------
// Get the setting for whether or not to enforce encrypted logon.
//

DWORD
AFPRegistry::GetForceEncryptedLogon(
	OUT	BOOL*	fForceEncryption
)
{
	DWORD	dwDataSize;
	DWORD	dwError;
	DWORD	dwType;

	OpenBaseKey();

	dwDataSize	= sizeof(BOOL);
	dwError		= RegQueryValueEx(
						mBaseKey,
						AFPREG_BASE_FORCEENCRYPTION,
						NULL,
						&dwType,
						(LPBYTE)fForceEncryption,
						&dwDataSize
						);

	if (dwError != NO_ERROR)
	{
		DBGWRITE(DbgLevelInfo, L"[AFPRegistry]Get encryption setting failed = %d\n", dwError);
	}

	return(dwError);
}


//---------------------------------------------------------------------
// SetForceEncryptedLogon()
//---------------------------------------------------------------------
// Set the setting for whether or not to enforce encrypted logon.
//

DWORD
AFPRegistry::SetForceEncryptedLogon(
	OUT	BOOL	fForceEncryption
)
{
	DWORD	dwError;

	OpenBaseKey();

	dwError = RegSetValueEx(
					mBaseKey,
					AFPREG_BASE_FORCEENCRYPTION,
					0,
					REG_DWORD,
					(BYTE*)&fForceEncryption,
					sizeof(BOOL)
					);

	if (dwError != ERROR_SUCCESS)
	{
		//
		//For some reason (a rarity??), we could not add the value to
		//the reg key.
		//
		return(AFPERR_REGISTRY_ADDFAILED);
	}

	return(NO_ERROR);
}


//---------------------------------------------------------------------
// GetAllowGuestsSetting()
//---------------------------------------------------------------------
// Get the allow guests setting from the reg.
//

DWORD
AFPRegistry::GetAllowGuestsSetting(
	OUT	BOOL*	fAllowGuests
)
{
	DWORD	dwDataSize;
	DWORD	dwError;
	DWORD	dwType;

	OpenBaseKey();

	dwDataSize	= sizeof(BOOL);
	dwError		= RegQueryValueEx(mBaseKey, AFPREG_BASE_ALLOWGUEST, NULL, &dwType, (LPBYTE)fAllowGuests, &dwDataSize);

	if (dwError != NO_ERROR)
	{
		DBGWRITE(DbgLevelInfo, L"[AFPRegistry]Get allow guests failed = %d\n", dwError);
		dwError = AFPERR_REGISTRY_NOGUESTSETTING;
	}

	return(dwError);
}


//---------------------------------------------------------------------
// SetAllowGuestsSetting()
//---------------------------------------------------------------------
// Set the allow guests setting from the reg.
//

DWORD
AFPRegistry::SetAllowGuestsSetting(
	IN	BOOL	fAllowGuests
)
{
	DWORD	dwError;

	OpenBaseKey();

	dwError = RegSetValueEx(
					mBaseKey,
					AFPREG_BASE_ALLOWGUEST,
					0,
					REG_DWORD,
					(BYTE*)&fAllowGuests,
					sizeof(BOOL));

	if (dwError != ERROR_SUCCESS)
	{
		//
		//For some reason (a rarity??), we could not add the value to
		//the reg key.
		//
		return(AFPERR_REGISTRY_ADDFAILED);
	}

	return(NO_ERROR);
}


//---------------------------------------------------------------------
// GetMaxSessions()
//---------------------------------------------------------------------
// Retrieve the maximum number of sessions allowed on server.
//

DWORD
AFPRegistry::GetMaxSessions(
	OUT DWORD* dwMaxSessions
)
{
	DWORD	dwDataSize;
	DWORD	dwError;
	DWORD	dwType;

	OpenBaseKey();

	dwDataSize	= sizeof(DWORD);
	dwError		= RegQueryValueEx(mBaseKey, AFPREG_BASE_MAXSESSIONS, NULL, &dwType, (LPBYTE)dwMaxSessions, &dwDataSize);

	if (dwError != NO_ERROR)
	{
		DBGWRITE(DbgLevelInfo, L"[AFPRegistry]Get max sessions failed = %d\n", dwError);
		dwError = AFPERR_REGISTRY_NOGUESTSETTING;
	}

	return(dwError);
}


//---------------------------------------------------------------------
// SetMaxSessions()
//---------------------------------------------------------------------
// Set max sessions
//

DWORD
AFPRegistry::SetMaxSessions(
	IN	DWORD	dwMaxSessions
)
{
	DWORD	dwError;

	OpenBaseKey();

	dwError = RegSetValueEx(
					mBaseKey,
					AFPREG_BASE_MAXSESSIONS,
					0,
					REG_DWORD,
					(BYTE*)&dwMaxSessions,
					sizeof(DWORD));

	if (dwError != ERROR_SUCCESS)
	{
		//
		//For some reason (a rarity??), we could not add the value to
		//the reg key.
		//
		return(AFPERR_REGISTRY_ADDFAILED);
	}

	return(NO_ERROR);
}


//---------------------------------------------------------------------
// GetVolumeFlags()
//---------------------------------------------------------------------
// Get the volume flags.
//

DWORD
AFPRegistry::GetVolumeFlags(
	IN	WCHAR*	szShareName,
	OUT DWORD*	flags
)
{
	DWORD	dwDataSize;
	DWORD	dwError;
	DWORD	dwType;

	OpenVolumeRegKey(szShareName);

	dwDataSize	= sizeof(DWORD);
	dwError		= RegQueryValueEx(mVolumeKey, AFPREG_VOLUME_FLAGS, NULL, &dwType, (LPBYTE)flags, &dwDataSize);

	if (dwError != NO_ERROR)
	{
		DBGWRITE(DbgLevelInfo, L"[AFPRegistry]Get volume flags failed = %d\n", dwError);
		dwError = AFPERR_REGISTRY_GETFAILED;
	}
	
	return(NO_ERROR);
}


//---------------------------------------------------------------------
// GetVolumeFlags()
//---------------------------------------------------------------------
// Get the volume flags.
//

DWORD
AFPRegistry::SetVolumeFlags(
	IN	WCHAR*	szShareName,
	IN	DWORD	flags
)
{
	DWORD	dwError;

	OpenVolumeRegKey(szShareName);

	dwError = RegSetValueEx(
					mVolumeKey,
					AFPREG_VOLUME_FLAGS,
					0,
					REG_DWORD,
					(BYTE*)&flags,
					sizeof(DWORD));

	if (dwError != ERROR_SUCCESS)
	{
		//
		//For some reason (a rarity??), we could not add the value to
		//the reg key.
		//
		DBGWRITE(DbgLevelInfo, L"[AFPRegistry::SetVolumeFlags]RegSetValueEx() failed!\n");
		return(AFPERR_REGISTRY_SETFAILED);
	}

	return(NO_ERROR);
}


//---------------------------------------------------------------------
// GetVolumePassword()
//---------------------------------------------------------------------
// Retrieve the volume password for the volume.
//

DWORD
AFPRegistry::GetVolumePassword(
	IN	WCHAR*	szShareName,
	IN	DWORD	cbPassword,
	OUT WCHAR*	szPassword
)
{
	DWORD	dwError;
	DWORD	dwDataSize;
	DWORD	dwType;

	OpenVolumeRegKey(szShareName);

	RtlZeroMemory(szPassword, cbPassword);

	dwDataSize	= cbPassword;
	dwError		= RegQueryValueEx(mVolumeKey, TEXT("password"), NULL, &dwType, (LPBYTE)szPassword, &dwDataSize);

	if (dwError != NO_ERROR)
	{
		DBGWRITE(DbgLevelInfo, L"[AFPRegistry::GetVolumePassword]Failed to get vol password (%d)\n", dwError);
		dwError = AFPERR_REGISTRY_GETFAILED;
	}

	DBGWRITE(DbgLevelInfo, L"[AFPRegistry::GetVolumePassword]Got password: %s\n", szPassword);

	return(dwError);
}


//---------------------------------------------------------------------
// GetVolumePassword() [ANSI]
//---------------------------------------------------------------------
// Retrieve the volume password for the volume.
//

DWORD
AFPRegistry::GetVolumePassword(
	IN	WCHAR*		szShareName,
	IN	DWORD		cbPassword,
	OUT PassWord*	szPassword
)
{
	TCHAR	szPasswordW[32];
	CHAR	szPass[10];
	DWORD	dwError = (DWORD)-1;

	if (GetVolumePassword(szShareName, sizeof(szPasswordW), szPasswordW) == NO_ERROR)
	{
		BOOL	fUsedDefault;

		RtlZeroMemory(szPassword, cbPassword);

		if (lstrlen(szPasswordW) > 0)
		{
			//
			//We have a password associated with this volume, save it. Note that
			//the password is saved in the registry as unicode.
			//
			dwError = WideCharToMultiByte(
								CP_MACCP,
								0,
								szPasswordW,
								cbPassword,
								(PCHAR)szPass,
								sizeof(szPass),
								"!",
								&fUsedDefault
								);

			if (dwError == 0) {
				dwError = GetLastError();
			}
			else
			{
				memset(szPassword, 0, sizeof(PassWord));
				memcpy(szPassword, szPass, strlen(szPass));

				dwError = NO_ERROR;
			}
		}
	}

	return(dwError);
}


//---------------------------------------------------------------------
// SetVolumePassword()
//---------------------------------------------------------------------
// Sets the volume password in the reg.
//

DWORD
AFPRegistry::SetVolumePassword(
	IN	WCHAR*	szShareName,
	IN	WCHAR*	szPassword
)
{
	DWORD	dwError;

	OpenVolumeRegKey(szShareName);

	dwError = RegSetValueEx(
					mVolumeKey,
					AFPREG_VOLUME_PASSWORD,
					0,
					REG_SZ,
					(BYTE*)szPassword,
					(lstrlen(szPassword) * sizeof(WCHAR)) + sizeof(WCHAR));

	if (dwError != ERROR_SUCCESS)
	{
		//
		//For some reason (a rarity??), we could not add the value to
		//the reg key.
		//
		DBGWRITE(DbgLevelInfo, L"[AFPRegistry::SetVolumePassword]RegSetValueEx() failed!\n");
		return(AFPERR_REGISTRY_SETFAILED);
	}

	return(NO_ERROR);
}











