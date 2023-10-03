//===================================================================
// AFPRegistry.h
//===================================================================

#pragma once

#include "afphelp.h"
#include "volumes.h"

#define SUBKEY_BASEPARAMS	TEXT("Software\\SomeGuy\\afpserver")
#define SUBKEY_VOLUMES		TEXT("Software\\SomeGuy\\afpserver\\volumes")

//Results for methods.
#define AFPERR_REGISTRY_ALREADYSHARED		1
#define AFPERR_REGISTRY_ADDFAILED			2
#define AFPERR_REGISTRY_ADDPASSFAILED		3
#define AFPERR_REGISTRY_REMVVOLUMENOTFOUND	4
#define AFPERR_REGISTRY_NOLOGONMSG			5
#define AFPERR_REGISTRY_NOGUESTSETTING		6
#define AFPERR_REGISTRY_GETFAILED			7
#define AFPERR_REGISTRY_SETFAILED			8


//Values stored in each volume reg key
#define AFPREG_VOLUME_PASSWORD				TEXT("password")
#define AFPREG_VOLUME_PATH					TEXT("path")
#define AFPREG_VOLUME_FLAGS					TEXT("Flags")

//Values in base registry entry
#define AFPREG_BASE_LOGONMSG				TEXT("LogonMsg")
#define AFPREG_BASE_ALLOWGUEST				TEXT("AllowGuest")
#define AFPREG_BASE_MAXSESSIONS				TEXT("MaxSessions")
#define AFPREG_BASE_FLAGS					TEXT("Flags")
#define AFPREG_BASE_FORCEENCRYPTION			TEXT("ForceEncryption")


class AFPRegistry
{
	public:
										AFPRegistry();
									   ~AFPRegistry();

		virtual DWORD					OpenBaseKey();
		virtual BOOL					OpenVolumeListRegKey();
		virtual BOOL					OpenVolumeRegKey(IN WCHAR* szShareName);

		virtual BOOL					NextVolume(
											OUT	TCHAR*	szShareName,
											IN	WORD	cbShareName,
											OUT TCHAR*	szSharePath,
											IN	WORD	cbSharePath,
											OUT	CHAR*	szPassword,
											OUT DWORD*	flags
										);
		virtual DWORD					AddVolume(
											IN	TCHAR*	szShareName,
											IN	TCHAR*	szSharePath,
											IN	TCHAR*	szPassword,
											IN	DWORD	flags = 0
										);
		virtual DWORD					RemoveVolume(
											IN	TCHAR*	szShareName
										);
		
		virtual DWORD					SaveLogonMessage(
											IN	TCHAR*	szLogonMessage
										);
		virtual DWORD					GetLogonMessage(
											OUT TCHAR*	szLogonMessage
										);
		virtual DWORD 					GetForceEncryptedLogon(
											OUT	BOOL*	fForceEncryption
										);
		virtual DWORD 					SetForceEncryptedLogon(
											OUT	BOOL	fForceEncryption
										);
		virtual DWORD					GetAllowGuestsSetting(
											OUT	BOOL*	fAllowGuests
										);
		virtual DWORD					SetAllowGuestsSetting(
											IN	BOOL	fAllowGuests
										);
		virtual DWORD					GetMaxSessions(
											OUT DWORD* dwMaxSessions
										);
		virtual DWORD					SetMaxSessions(
											IN	DWORD	dwMaxSessions
										);
		virtual DWORD					GetVolumeFlags(
											IN	WCHAR*	szShareName,
											OUT DWORD*	flags
											);
		virtual DWORD					SetVolumeFlags(
											IN	WCHAR*	szShareName,
											IN	DWORD	flags
											);
		virtual DWORD					GetVolumePassword(
											IN	WCHAR*	szShareName,
											IN	DWORD	cbPassword,
											OUT WCHAR*	szPassword
											);
		virtual DWORD					GetVolumePassword(
											IN	WCHAR*		szShareName,
											IN	DWORD		cbPassword,
											OUT PassWord*	szPassword
											);
		virtual DWORD					SetVolumePassword(
											IN	WCHAR*	szShareName,
											IN	WCHAR*	szPassword
											);
	protected:
		WORD			mEnumIndex;
		HKEY			mVolumeKey;
		HKEY			mVolumeEnumKey;
		HKEY			mAddVolumeKey;
		HKEY			mBaseKey;
};
