#include "StdAfx.h"
#include <strsafe.h>
#include "afp_registry.h"

#include <sstream>

#include "afpdebug.h"
#include "fdparms.h"


afp_registry::afp_registry()
	: m_enum_index_(0)
	  , m_volume_key_(nullptr)
	  , m_volume_enum_key_(nullptr)
	  , m_add_volume_key_(nullptr)
	  , m_base_key_(nullptr)
{
}


afp_registry::~afp_registry()
{
	if (m_volume_key_ != nullptr)
	{
		RegCloseKey(m_volume_key_);
	}

	if (m_volume_enum_key_ != nullptr)
	{
		RegCloseKey(m_volume_enum_key_);
	}

	if (m_add_volume_key_ != nullptr)
	{
		RegCloseKey(m_add_volume_key_);
	}

	if (m_base_key_ != nullptr)
	{
		RegCloseKey(m_base_key_);
	}
}


DWORD
afp_registry::open_base_key()
{
	DWORD dw_disposition;

	if (m_base_key_ != nullptr)
	{
		// The key is already open
		return (NO_ERROR);
	}

	const DWORD dw_error = RegCreateKeyEx(
		HKEY_CURRENT_USER,
		SUBKEY_BASEPARAMS,
		0,
		nullptr,
		REG_OPTION_NON_VOLATILE,
		KEY_ALL_ACCESS,
		nullptr,
		&m_base_key_,
		&dw_disposition);

	if (dw_error != ERROR_SUCCESS)
	{
		DBGWRITE(dbg_level_info, L"RegCreateKeyEx() failed opening base key, error code = %d\n", dw_error);
		return (dw_error);
	}

	return (ERROR_SUCCESS);
}


BOOL
afp_registry::open_volume_list_reg_key()
{
	DWORD dw_disposition;

	if (m_volume_enum_key_ != nullptr)
	{
		DBGWRITE(dbg_level_info, L"Volume enum key already open\n");
		return (true);
	}

	const DWORD dw_error = RegCreateKeyEx(
		HKEY_CURRENT_USER,
		SUBKEY_VOLUMES,
		0,
		nullptr,
		REG_OPTION_NON_VOLATILE,
		KEY_ALL_ACCESS,
		nullptr,
		&m_volume_enum_key_,
		&dw_disposition);

	if (dw_error != ERROR_SUCCESS)
	{
		DBGWRITE(dbg_level_info, L"RegCreateKeyEx() failed, error code = %d\n", dw_error);
		return (false);
	}

	m_enum_index_ = 0;

	return (true);
}


BOOL
afp_registry::open_volume_reg_key(
	IN const WCHAR* sz_share_name
)
{
	WCHAR sz_reg_path[128];
	DWORD dw_disposition;

	if (m_volume_key_ != nullptr)
	{
		DBGWRITE(dbg_level_info, L"Volume key already open\n");
		return (false);
	}

	// ReSharper disable once CppFunctionResultShouldBeUsed
	StringCbPrintf(sz_reg_path, sizeof(sz_reg_path), TEXT("%s\\%s"), SUBKEY_VOLUMES, sz_share_name);

	const DWORD dw_error = RegCreateKeyEx(
		HKEY_CURRENT_USER,
		sz_reg_path,
		0,
		nullptr,
		REG_OPTION_NON_VOLATILE,
		KEY_ALL_ACCESS,
		nullptr,
		&m_volume_key_,
		&dw_disposition);

	if (dw_error != ERROR_SUCCESS)
	{
		DBGWRITE(dbg_level_info, L"RegCreateKeyEx() failed, error code = %d\n", dw_error);
		return (false);
	}

	return (true);
}


BOOL
afp_registry::next_volume(
	OUT TCHAR* sz_share_name,
	IN const WORD cb_share_name,
	OUT TCHAR* sz_share_path,
	IN const WORD cb_share_path,
	OUT CHAR* sz_password,
	IN const WORD cb_password,
	OUT DWORD* flags
)
{
	FILETIME file_time;
	TCHAR sz_key_name[64];
	TCHAR sz_vol_key[64];
	DWORD dw_data_size, dw_type;
	HKEY h_vol_key;

	dw_data_size = sizeof(sz_key_name);
	DWORD dw_error = RegEnumKeyEx(m_volume_enum_key_, m_enum_index_++, sz_key_name,
	                              &dw_data_size, nullptr, nullptr, nullptr, &file_time);

	if (dw_error != ERROR_SUCCESS)
	{
		if (dw_error != ERROR_NO_MORE_ITEMS)
		{
			DBGWRITE(dbg_level_info, L"RegEnuKeyEx() failed with error = %d\n", dw_error);
		}

		//
		//There were no more entries in the registry to return. Pass false back.
		//
		return (false);
	}

	// ReSharper disable once CppFunctionResultShouldBeUsed
	StringCbPrintf(sz_vol_key, sizeof(sz_vol_key), TEXT("%s\\%s"), SUBKEY_VOLUMES, sz_key_name);

	dw_error = RegOpenKeyEx(
		HKEY_CURRENT_USER,
		sz_vol_key,
		0,
		KEY_ALL_ACCESS,
		&h_vol_key);

	if (dw_error == ERROR_SUCCESS)
	{
		TCHAR sz_path[MAX_PATH];
		dw_data_size = sizeof(sz_path);

		dw_error = RegQueryValueEx(h_vol_key, TEXT("path"), nullptr,
		                           &dw_type, reinterpret_cast<LPBYTE>(sz_path), &dw_data_size);

		if (dw_error == ERROR_SUCCESS)
		{
			TCHAR sz_pass[24];
			DBGWRITE(dbg_level_info, L"Found volume path in registry = %s\n", sz_path);

			// ReSharper disable CppFunctionResultShouldBeUsed
			StringCbCopy(sz_share_name, cb_share_name, sz_key_name);
			StringCbCopy(sz_share_path, cb_share_path, sz_path);
			// ReSharper restore CppFunctionResultShouldBeUsed

			RtlZeroMemory(sz_password, cb_password);

			dw_data_size = sizeof(sz_pass);
			dw_error = RegQueryValueEx(h_vol_key, TEXT("password"), nullptr,
			                           &dw_type, reinterpret_cast<LPBYTE>(sz_pass), &dw_data_size);

			if (dw_error == ERROR_SUCCESS)
			{
				CHAR sz_ansi_password[10];
				BOOL f_used_default;

				RtlZeroMemory(sz_ansi_password, sizeof(sz_ansi_password));

				//
				//We have a password associated with this volume, save it. Note that
				//the password is saved in the registry as unicode.
				//
				WideCharToMultiByte(
					CP_MACCP,
					0,
					sz_pass,
					static_cast<int>(dw_data_size),
					sz_ansi_password,
					sizeof(afp_clear_text_password),
					"!",
					&f_used_default);

				strcpy_s(sz_password, sizeof(afp_clear_text_password), sz_ansi_password);
			}
		}
		else
		{
			return (false);
		}

		dw_data_size = sizeof(DWORD);
		dw_error = RegQueryValueEx(h_vol_key, AFPREG_VOLUME_FLAGS, nullptr,
		                           &dw_type, reinterpret_cast<LPBYTE>(flags), &dw_data_size);

		if (dw_error != ERROR_SUCCESS)
		{
			DBGWRITE(dbg_level_info, L"Failed to get volume flags key\n");
		}

		RegCloseKey(h_vol_key);
	}
	else
	{
		return (false);
	}

	return (true);
}


DWORD
afp_registry::add_volume(
	IN TCHAR* sz_share_name,
	IN TCHAR* sz_share_path,
	IN TCHAR* sz_password,
	IN DWORD flags
)
{
	TCHAR sz_reg_path[64];
	DWORD dw_disposition;

	// ReSharper disable once CppFunctionResultShouldBeUsed
	StringCbPrintf(sz_reg_path, sizeof(sz_reg_path), TEXT("%s\\%s"), SUBKEY_VOLUMES, sz_share_name);

	DWORD dw_error = RegCreateKeyEx(
		HKEY_CURRENT_USER,
		sz_reg_path,
		0,
		nullptr,
		REG_OPTION_NON_VOLATILE,
		KEY_ALL_ACCESS,
		nullptr,
		&m_add_volume_key_,
		&dw_disposition);

	if (dw_error == ERROR_SUCCESS)
	{
		if (dw_disposition == REG_OPENED_EXISTING_KEY)
		{
			return (afperr_registry_already_shared);
		}

		dw_error = RegSetValueEx(
			m_add_volume_key_,
			AFPREG_VOLUME_PATH,
			0,
			REG_SZ,
			reinterpret_cast<BYTE*>(sz_share_path),
			lstrlen(sz_share_path) * sizeof(TCHAR) + sizeof(TCHAR));

		if (dw_error != ERROR_SUCCESS)
		{
			return afperr_registry_add_failed;
		}

		if (sz_password && lstrlen(sz_password) > 0)
		{
			dw_error = RegSetValueEx(
				m_add_volume_key_,
				AFPREG_VOLUME_PASSWORD,
				0,
				REG_SZ,
				reinterpret_cast<BYTE*>(sz_password),
				(lstrlen(sz_password) * sizeof(TCHAR)) + sizeof(TCHAR));

			if (dw_error != ERROR_SUCCESS)
			{
				return afperr_registry_add_pass_failed;
			}
		}

		//
		//Add the volume flags to the registry.
		//
		dw_error = RegSetValueEx(
			m_add_volume_key_,
			AFPREG_VOLUME_FLAGS,
			0,
			REG_DWORD,
			reinterpret_cast<BYTE*>(&flags),
			sizeof(DWORD)
		);

		if (dw_error != ERROR_SUCCESS)
		{
			return afperr_registry_add_failed;
		}

		RegCloseKey(m_add_volume_key_);

		m_add_volume_key_ = nullptr;
	}

	return ERROR_SUCCESS;
}


DWORD
afp_registry::remove_volume(
	IN TCHAR* sz_share_name
)
{
	TCHAR sz_reg_path[64];

	// ReSharper disable once CppFunctionResultShouldBeUsed
	StringCbPrintf(sz_reg_path, sizeof(sz_reg_path), TEXT("%s\\%s"), SUBKEY_VOLUMES, sz_share_name);

	return RegDeleteKey(HKEY_CURRENT_USER, sz_reg_path);
}


DWORD
afp_registry::save_logon_message(
	IN TCHAR* sz_logon_message
)
{
	open_base_key();

	const DWORD dw_error = RegSetValueEx(
		m_base_key_,
		AFPREG_BASE_LOGONMSG,
		0,
		REG_SZ,
		reinterpret_cast<BYTE*>(sz_logon_message),
		(lstrlen(sz_logon_message) * sizeof(TCHAR)) + sizeof(TCHAR));

	if (dw_error != ERROR_SUCCESS)
	{
		//
		//For some reason (a rarity??), we could not add the value to
		//the reg key.
		//
		return (afperr_registry_add_failed);
	}

	return NO_ERROR;
}


DWORD
afp_registry::get_logon_message(
	OUT TCHAR* sz_logon_message
)
{
	DWORD dw_data_size;
	DWORD dw_type;

	open_base_key();

	dw_data_size = (message_max_length * sizeof(TCHAR)) + sizeof(TCHAR);
	DWORD dw_error = RegQueryValueEx(m_base_key_, AFPREG_BASE_LOGONMSG, nullptr,
	                                 &dw_type, reinterpret_cast<LPBYTE>(sz_logon_message), &dw_data_size);

	if (dw_error != NO_ERROR)
	{
		DBGWRITE(dbg_level_info, L"GetLogonMsg failed = %d\n", dw_error);
		dw_error = afperr_registry_no_logon_msg;
	}

	return dw_error;
}


DWORD
afp_registry::get_force_encrypted_logon(
	OUT BOOL* f_force_encryption
)
{
	DWORD dw_data_size;
	DWORD dw_type;

	open_base_key();

	dw_data_size = sizeof(BOOL);
	const DWORD dw_error = RegQueryValueEx(
		m_base_key_,
		AFPREG_BASE_FORCEENCRYPTION,
		nullptr,
		&dw_type,
		reinterpret_cast<LPBYTE>(f_force_encryption),
		&dw_data_size
	);

	if (dw_error != NO_ERROR)
	{
		DBGWRITE(dbg_level_info, L"[AFPRegistry]Get encryption setting failed = %d\n", dw_error);
	}

	return (dw_error);
}


DWORD
afp_registry::set_force_encrypted_logon(
	OUT BOOL fForceEncryption
)
{
	open_base_key();

	const DWORD dw_error = RegSetValueEx(
		m_base_key_,
		AFPREG_BASE_FORCEENCRYPTION,
		0,
		REG_DWORD,
		reinterpret_cast<BYTE*>(&fForceEncryption),
		sizeof(BOOL)
	);

	if (dw_error != ERROR_SUCCESS)
	{
		return afperr_registry_add_failed;
	}

	return NO_ERROR;
}


DWORD
afp_registry::get_allow_guests_setting(
	OUT BOOL* f_allow_guests
)
{
	DWORD dw_data_size;
	DWORD dw_type;

	open_base_key();

	dw_data_size = sizeof(BOOL);
	DWORD dw_error = RegQueryValueEx(m_base_key_, AFPREG_BASE_ALLOWGUEST, nullptr,
		&dw_type, reinterpret_cast<LPBYTE>(f_allow_guests), &dw_data_size);

	if (dw_error != NO_ERROR)
	{
		DBGWRITE(dbg_level_info, L"Get allow guests failed = %d\n", dw_error);
		dw_error = afperr_registry_no_guest_setting;
	}

	return dw_error;
}


DWORD
afp_registry::set_allow_guests_setting(
	IN BOOL f_allow_guests
)
{
	open_base_key();

	const DWORD dw_error = RegSetValueEx(
		m_base_key_,
		AFPREG_BASE_ALLOWGUEST,
		0,
		REG_DWORD,
		reinterpret_cast<BYTE*>(&f_allow_guests),
		sizeof(BOOL));

	if (dw_error != ERROR_SUCCESS)
	{
		return (afperr_registry_add_failed);
	}

	return NO_ERROR;
}


DWORD
afp_registry::get_max_sessions(
	OUT DWORD* dw_max_sessions
)
{
	DWORD dw_data_size;
	DWORD dw_type;

	open_base_key();

	dw_data_size = sizeof(DWORD);
	DWORD dw_error = RegQueryValueEx(m_base_key_, AFPREG_BASE_MAXSESSIONS, nullptr,
		&dw_type, reinterpret_cast<LPBYTE>(dw_max_sessions), &dw_data_size);

	if (dw_error != NO_ERROR)
	{
		DBGWRITE(dbg_level_info, L"Get max sessions failed = %d\n", dw_error);
		dw_error = afperr_registry_no_guest_setting;
	}

	return dw_error;
}


DWORD
afp_registry::set_max_sessions(
	IN DWORD dw_max_sessions
)
{
	open_base_key();

	const DWORD dw_error = RegSetValueEx(
		m_base_key_,
		AFPREG_BASE_MAXSESSIONS,
		0,
		REG_DWORD,
		reinterpret_cast<BYTE*>(&dw_max_sessions),
		sizeof(DWORD));

	if (dw_error != ERROR_SUCCESS)
	{
		//
		//For some reason (a rarity??), we could not add the value to
		//the reg key.
		//
		return (afperr_registry_add_failed);
	}

	return NO_ERROR;
}


DWORD
afp_registry::get_volume_flags(
	IN WCHAR* sz_share_name,
	OUT DWORD* flags
)
{
	DWORD dw_data_size;
	DWORD dw_type;

	open_volume_reg_key(sz_share_name);

	dw_data_size = sizeof(DWORD);
	DWORD dw_error = RegQueryValueEx(m_volume_key_, AFPREG_VOLUME_FLAGS, nullptr,
		&dw_type, reinterpret_cast<LPBYTE>(flags), &dw_data_size);

	if (dw_error != NO_ERROR)
	{
		DBGWRITE(dbg_level_info, L"Get volume flags failed = %d\n", dw_error);
		dw_error = afperr_registry_get_failed;
	}

	return dw_error;
}


DWORD
afp_registry::set_volume_flags(
	IN const WCHAR* sz_share_name,
	IN DWORD flags
)
{
	open_volume_reg_key(sz_share_name);

	const DWORD dw_error = RegSetValueEx(
		m_volume_key_,
		AFPREG_VOLUME_FLAGS,
		0,
		REG_DWORD,
		reinterpret_cast<BYTE*>(&flags),
		sizeof(DWORD));

	if (dw_error != ERROR_SUCCESS)
	{
		DBGWRITE(dbg_level_info, L"RegSetValueEx() failed!\n");
		return afperr_registry_set_failed;
	}

	return NO_ERROR;
}


DWORD
afp_registry::get_volume_password(
	IN const WCHAR* sz_share_name,
	IN const DWORD cb_password,
	OUT WCHAR* sz_password
)
{
	DWORD dw_data_size;
	DWORD dw_type;

	open_volume_reg_key(sz_share_name);

	RtlZeroMemory(sz_password, cb_password);

	dw_data_size = cb_password;
	DWORD dw_error = RegQueryValueEx(m_volume_key_, TEXT("password"), nullptr,
		&dw_type, reinterpret_cast<LPBYTE>(sz_password), &dw_data_size);

	if (dw_error != NO_ERROR)
	{
		DBGWRITE(dbg_level_info, L"No password for volume (probably ok), error: %d\n", dw_error);
		dw_error = afperr_registry_get_failed;
	}
	else
	{
		DBGWRITE(dbg_level_info, L"Got password: %s\n", sz_password);
	}

	return dw_error;
}


DWORD
afp_registry::get_volume_password(
	IN const WCHAR* sz_share_name,
	IN const DWORD cb_password,
	OUT afp_clear_text_password* sz_password
)
{
	TCHAR sz_password_w[32];
	auto dw_error = static_cast<DWORD>(-1);

	if (get_volume_password(sz_share_name, sizeof(sz_password_w), sz_password_w) == NO_ERROR)
	{
		BOOL f_used_default;

		RtlZeroMemory(sz_password, cb_password);

		if (lstrlen(sz_password_w) > 0)
		{
			CHAR sz_pass[10];

			dw_error = WideCharToMultiByte(
				CP_MACCP,
				0,
				sz_password_w,
				static_cast<int>(cb_password),
				sz_pass,
				sizeof(sz_pass),
				"!",
				&f_used_default
			);

			if (dw_error == 0)
			{
				dw_error = GetLastError();
			}
			else
			{
				memset(sz_password, 0, sizeof(afp_clear_text_password));
				memcpy(sz_password, sz_pass, strlen(sz_pass));

				dw_error = NO_ERROR;
			}
		}
	}

	return dw_error;
}


DWORD
afp_registry::set_volume_password(
	IN const WCHAR* sz_share_name,
	IN WCHAR* sz_password
)
{
	open_volume_reg_key(sz_share_name);

	const DWORD dw_error = RegSetValueEx(
		m_volume_key_,
		AFPREG_VOLUME_PASSWORD,
		0,
		REG_SZ,
		reinterpret_cast<BYTE*>(sz_password),
		lstrlen(sz_password) * sizeof(WCHAR) + sizeof(WCHAR));

	if (dw_error != ERROR_SUCCESS)
	{
		DBGWRITE(dbg_level_info, L"RegSetValueEx() failed!\n");
		return afperr_registry_set_failed;
	}

	return NO_ERROR;
}

regerror afp_registry::get_extensions_for_line_conversion(std::vector<std::wstring>& extensions)
{
	std::wstring input_string;
	if (const auto error = get_extensions_for_line_conversion(input_string); error != NO_ERROR)
	{
		return error;
	}

	std::wstringstream wss(input_string);
	std::wstring token;

	while (std::getline(wss, token, conversion_delimiter))
	{
		extensions.push_back(token);
	}

	if (extensions.empty())
	{
		DBGWRITE(dbg_level_error, L"No file extensions found for line conversion\n");
		return afperr_registry_no_extensions_for_conversion;
	}

	return NO_ERROR;
}

regerror afp_registry::get_extensions_for_line_conversion(std::wstring& extensions)
{
	DWORD dw_data_size;
	DWORD dw_type;
	WCHAR sz_extensions[max_extension_string_size + sizeof(WCHAR)]{};

	open_base_key();

	dw_data_size = (max_extension_string_size * sizeof(WCHAR)) + sizeof(WCHAR);
	const DWORD dw_error = RegQueryValueEx(m_base_key_, AFPREG_BASE_EXTENSIONS_FOR_CONVERSION, nullptr,
		&dw_type, reinterpret_cast<LPBYTE>(sz_extensions), &dw_data_size);

	if (dw_error != NO_ERROR)
	{
		DBGWRITE(dbg_level_warning, L"Get conversion file extensions failed = %d\n", dw_error);
		return afperr_registry_no_extensions_for_conversion;
	}

	if (dw_data_size == 0)
	{
		DBGWRITE(dbg_level_warning, L"Could not read extensions string from reg key.\n");
		return afperr_registry_no_extensions_for_conversion;
	}

	extensions = sz_extensions;

	return NO_ERROR;
}

regerror afp_registry::set_extensions_for_conversion(const std::wstring& extensions)
{
	open_base_key();

	const DWORD dw_error = RegSetValueEx(
		m_base_key_,
		AFPREG_BASE_EXTENSIONS_FOR_CONVERSION,
		0,
		REG_SZ,
		reinterpret_cast<const BYTE*>(extensions.c_str()),
		static_cast<DWORD>((extensions.length() * sizeof(TCHAR)) + sizeof(TCHAR)));

	if (dw_error != ERROR_SUCCESS)
	{
		//
		//For some reason (a rarity??), we could not add the value to
		//the reg key.
		//
		return afperr_registry_add_failed;
	}

	return NO_ERROR;
}
