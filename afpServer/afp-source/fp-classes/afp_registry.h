#pragma once

#define SUBKEY_BASEPARAMS	TEXT("Software\\MacFile\\afpserver")
#define SUBKEY_VOLUMES		TEXT("Software\\MacFile\\afpserver\\volumes")
#include "afp.h"

enum
{
	afperr_registry_already_shared = 1,
	afperr_registry_add_failed = 2,
	afperr_registry_add_pass_failed = 3,
	afperr_registry_remove_volume_not_found = 4,
	afperr_registry_no_logon_msg = 5,
	afperr_registry_no_guest_setting = 6,
	afperr_registry_get_failed = 7,
	afperr_registry_set_failed = 8
};


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


class afp_registry final // NOLINT(cppcoreguidelines-special-member-functions)
{
public:
	afp_registry();
	~afp_registry();

	DWORD open_base_key();
	BOOL open_volume_list_reg_key();
	BOOL open_volume_reg_key(IN WCHAR* sz_share_name);

	BOOL next_volume(
		OUT TCHAR* sz_share_name,
		IN WORD cb_share_name,
		OUT TCHAR* sz_share_path,
		IN WORD cb_share_path,
		OUT CHAR* sz_password,
		IN WORD cb_password,
		OUT DWORD* flags
	);
	DWORD add_volume(
		IN TCHAR* sz_share_name,
		IN TCHAR* sz_share_path,
		IN TCHAR* sz_password,
		IN DWORD flags = 0
	);
	static DWORD remove_volume(
		IN TCHAR* sz_share_name
	);

	DWORD save_logon_message(
		IN TCHAR* sz_logon_message
	);
	DWORD get_logon_message(
		OUT TCHAR* sz_logon_message
	);
	DWORD get_force_encrypted_logon(
		OUT BOOL* f_force_encryption
	);
	DWORD set_force_encrypted_logon(
		OUT BOOL fForceEncryption
	);
	DWORD get_allow_guests_setting(
		OUT BOOL* f_allow_guests
	);
	DWORD set_allow_guests_setting(
		IN BOOL f_allow_guests
	);
	DWORD get_max_sessions(
		OUT DWORD* dw_max_sessions
	);
	DWORD set_max_sessions(
		IN DWORD dw_max_sessions
	);
	DWORD get_volume_flags(
		IN WCHAR* sz_share_name,
		OUT DWORD* flags
	);
	DWORD set_volume_flags(
		IN WCHAR* sz_share_name,
		IN DWORD flags
	);
	DWORD get_volume_password(
		IN WCHAR* sz_share_name,
		IN DWORD cb_password,
		OUT WCHAR* sz_password
	);
	DWORD get_volume_password(
		IN WCHAR* sz_share_name,
		IN DWORD cb_password,
		OUT PassWord* sz_password
	);
	DWORD set_volume_password(
		IN WCHAR* sz_share_name,
		IN WCHAR* sz_password
	);

protected:
	WORD m_enum_index_;
	HKEY m_volume_key_;
	HKEY m_volume_enum_key_;
	HKEY m_add_volume_key_;
	HKEY m_base_key_;
};
