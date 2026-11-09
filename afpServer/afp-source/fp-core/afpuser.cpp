// ReSharper disable CppClangTidyClangDiagnosticGnuZeroVariadicMacroArguments
// ReSharper disable CppParameterMayBeConstPtrOrRef
// ReSharper disable CppClangTidyMiscUseAnonymousNamespace
#include "StdAfx.h"
#include "afpuser.h"

#include <logonimp.h>

#include "afpEvents.h"
#include "afpdhxlogin.h"
#include "afp_registry.h"
#include "afp_session.h"
#include "windows_security.h"
#include "dsi/dsi_connections.h"

extern dsi::dsi_connections active_connections;

WCHAR g_server_message[message_max_length + 1];   // NOLINT(misc-use-internal-linkage)
DWORD g_max_sessions = 0;						  // NOLINT(misc-use-internal-linkage)
BOOL  g_allow_guests = TRUE;					  // NOLINT(misc-use-internal-linkage)
BOOL  g_force_encrypted_logon = FALSE;			  // NOLINT(misc-use-internal-linkage)
std::wstring g_xor_key = L"mL@TCEruQG$?S5m%Ks}N"; // NOLINT(misc-use-internal-linkage)

static UINT8 get_afp_version_as_int(const WCHAR* version)
{
	INT8 result = 0;

	if (!lstrcmp(version, AFP_22_VERSION_STR))
		result = afpVersion22;
	else if (!lstrcmp(version, AFP_30_VERSION_STR))
		result = afpVersion30;
	else if (!lstrcmp(version, AFP_31_VERSION_STR))
		result = afpVersion31;
	else if (!lstrcmp(version, AFP_32_VERSION_STR))
		result = afpVersion32;
	// An unknown version string leaves the result at zero; FPLogin
	// rejects those with afpBadUAM. Clients may send any string, so
	// there is deliberately no assert here.

	return result;
}

static INT8 get_uam_type_as_int(const WCHAR* uam_string)
{
	INT8 result = -1;

	if (!lstrcmp(uam_string, UAM_NONE_STR))
		result = afpUAMNone;
	else if (!lstrcmp(uam_string, UAM_CLEAR_TEXT))
		result = afpUAMCleartext;
	else if (!lstrcmp(uam_string, UAM_DHCAST128))
		result = afpUAMDHCAST128;
	else if (!lstrcmp(uam_string, UAM_XOR))
		result = afpUAMXor;
	// An unknown UAM string leaves the result at -1; FPLogin rejects
	// those with afpBadUAM. Clients may send any string, so there is
	// deliberately no assert here.

	return (result);
}

static std::wstring xor_decrypt(std::wstring& encrypted, std::wstring& key)
{
	std::wstring decrypted;
	const auto key_length = key.length();
	const auto encrypted_length = encrypted.length();

    if (encrypted.empty() || key.empty())
    {
        return L"";
    }

    // Ensure the decrypted string has the same length as the encrypted one.
    decrypted.resize(encrypted_length);

    // The key must be at least 1 character long.
    if (key.length() < 2)
    {
        DBGWRITE(dbg_level_error, L"Key is too short for decryption.\n");
        return L"";
    }

	if (encrypted_length > UAM_MAX_XORPWDLEN)
	{
		DBGWRITE(dbg_level_error, L"Password is too long for decryption.\n");
		return L"";
	}

	for (size_t i = 0; i < encrypted_length; i++)
	{
	    decrypted[i] = encrypted[i] ^ key[i % key_length];
	}

	return decrypted;
}

/*
 * FPGetSessionToken()
 *
 * Description:
 *		Returns a session token to the client for re-connection at
 *		a later time if the client crashes or connection is lost.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPGetSessionToken(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi::dsi_connection* connection
)
{
	auto* session = connection->get_afp_session();
	const afp_session* afp_old_session;
	INT32 afp_id_size = 0;
	std::unique_ptr<char[]> afp_id;
	UINT32 afp_time_stamp = 0;
	const UINT32 afp_token = session->get_token();

    DBGWRITE(dbg_level_info, L"Enter...\n");

	afp_request.advance_pos_ptr(sizeof(INT16));
	const auto afp_type = afp_request.pull_num<INT16>();

	if (afp_type != kLoginWithoutID)
	{
		afp_id_size = afp_request.pull_num<INT32>();

		DBGWRITE(dbg_level_info, L"ID size from client = %d\n", afp_id_size);

		// The client supplies this length; validate it before using it
		// as an allocation size. A negative value would wrap when
		// converted to size_t (bad_alloc/exception in a network path)
		// and an oversized one is a trivial memory DoS. Also, the
		// pull below would truncate it through an INT16 cast.
		constexpr INT32 max_session_id_size = 1024;
		if (afp_id_size <= 0 || afp_id_size > max_session_id_size)
		{
			DBGWRITE(dbg_level_warning, L"Bad AFP ID size (%d)\n", afp_id_size);
			return afpParmErr;
		}

		// For types 3 & 4, a time stamp is included for additional comparison.
		if (afp_type == kLoginWithTimeAndID || afp_type == kReconnWithTimeAndID)
		{
			afp_time_stamp = afp_request.pull_num<UINT32>();
		}

		// Save the AFP id in the AFPSession object for later comparison.
		afp_id = std::make_unique<char[]>(afp_id_size);

		afp_request.pull_raw_data(afp_id.get(), static_cast<size_t>(afp_id_size));
	}

	switch (afp_type)
	{
	case kLoginWithoutID:
		// Now we find the session corresponding to the token and kill it.
		break;

	// The AFP client can set the Type parameter to kLoginWithID, set the ID 
	// parameter to a unique client-defined value, and send_ this command. The server 
	// will associate the value of ID with the session. Later, if the local system crashes 
	// and the AFP client logs in again, the AFP client can set the Type parameter to 
	// kLoginWithID, set the ID parameter to the ID that it previously sent to the remote 
	// server, and send_ this command again. The remote server will look for a session that 
	// matches the value of ID. If a match is found, it will close any files associated with 
	// the session that are open, free any locked resources, and disconnect the matching session
	case kLoginWithID:
		DBGWRITE(dbg_level_info, L"kLoginWithID...\n");

		afp_old_session = active_connections.find_afp_session_with_id(afp_id.get(), afp_id_size);

		if (afp_old_session != nullptr)
		{
			DBGWRITE(dbg_level_info, L"Found old session!!!\n");
			afp_old_session->get_dsi_session()->kill_connection();
			break;
		}

		session->set_session_id(afp_id, afp_id_size);
		break;

	case kReconnWithID:
		break;

	// The client wants his old session to be discarded. The client is sending
	// an IDLength, an ID, and a Timestamp. We only discard the old session if
	// the timestamps DO NOT match.
	case kLoginWithTimeAndID:
		DBGWRITE(dbg_level_info, L"kLoginWithTimeAndID...\n");

		afp_old_session = active_connections.find_afp_session_with_id(afp_id.get(), afp_id_size);

		if (afp_old_session != nullptr)
		{
			DBGWRITE(dbg_level_info, L"Found old session!!!\n");

			if (afp_old_session->get_afp_time_stamp() != afp_time_stamp)
			{
				// Time stamps do not match, discard the old session.
				afp_old_session->get_dsi_session()->kill_connection();
				break;
			}
		}

		session->set_afp_time_stamp(afp_time_stamp);
		session->set_session_id(afp_id, afp_id_size);
		break;

	// The client has just finished a successful reconnect, is sending an IDLength,
	// an ID, and a Timestamp parameter, and wants the session to be updated
	// with the ID parameter.
	case kReconnWithTimeAndID:
		DBGWRITE(dbg_level_info, L"kReconnWithTimeAndID...\n");

		session->set_afp_time_stamp(afp_time_stamp);
		session->set_session_id(afp_id, afp_id_size);
		break;

	// All these ids are unsupported by this AFP Server.
	case kRecon1Login:
	case kRecon1ReconnectLogin:
	case kRecon1Refresh:
	case kGetKerberosSessionKey:
		break;

	default:
		DBGWRITE(dbg_level_info, L"Bad type! (%d)\n", afp_type);
		return (afpParmErr);
	}

	afp_reply.push_num<UINT32>(AFP_SESSION_TOKEN_SIZE);
	afp_reply.push_num(afp_token);

	DBGWRITE(dbg_level_info, L"Token = %x\n", afp_token);

	return AFP_OK;
}


/*
 * FPDisconnectOldSession()
 *
 * Description:
 *		AFP clients call this when they are inadvertently cut off from
 *		the file server. The client calls this to transfer all resources
 *		from the old session to this new one.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPDisconnectOldSession(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi::dsi_connection* connection
)
{
	UNREFERENCED_PARAMETER(afp_reply);
	UNREFERENCED_PARAMETER(connection);

	DBGWRITE(dbg_level_info, L"Enter...\n");

	afp_request.advance_pos_ptr(sizeof(UINT16) + sizeof(UINT8));
	const INT32 afp_token_length = afp_request.pull_num<INT32>();

	if (afp_token_length != AFP_SESSION_TOKEN_SIZE)
	{
		DBGWRITE(dbg_level_info, L"Bad token length\n");
		return afpMiscErr;
	}

	const auto afp_token = afp_request.pull_num<UINT32>();
	const afp_session* afp_lost_session = active_connections.find_afp_session_with_token(afp_token);

	if (afp_lost_session != nullptr)
	{
		DBGWRITE(dbg_level_info, L"Found disconnected session!\n");
		afp_lost_session->get_dsi_session()->kill_connection();
	}

	return AFP_OK;
}


/*
 * FPLogin()
 *
 * Description:
 *		The client is attempting to authenticate to this afp server.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPLogin(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi::dsi_connection* connection
)
{
	//
	//Check for maximum logon sessions limit being reached. If it is, we
	//just return afpAccessDenied.
	//
	DBGWRITE(dbg_level_info, L"FPLogin entered (connection %d)\n", connection->get_connection_id());
	if (g_max_sessions != 0)
	{
		//
		//Note that we only count authenticated sessions on the server. There could be
		//some "utility" sessions still looming around that we don't want to count.
		//
		if (active_connections.count(true) >= g_max_sessions)
		{
			//
			//Too many sessions open on the server. The spec doesn't say what error
			//code to return in this case.
			//
			return afpAccessDenied;
		}
	}

	//
	//The first byte in the buffer is the afp command byte.
	//
	const INT8 afp_command = afp_request.pull_num<INT8>();

	if (afp_command == afpLoginExt)
	{
		afp_request.advance_pos_ptr(sizeof(UINT8));
		afp_request.advance_pos_ptr(sizeof(UINT16)); // afp_flags
	}

	const auto afp_version = afp_request.pull_string();
	if (afp_version.empty())
	{
		return afpParmErr;
	}

	const auto afp_uam = afp_request.pull_string();
	if (afp_uam.empty())
	{
		return afpParmErr;
	}

	DBGWRITE(dbg_level_info, L"AFP Version = %s\n", afp_version.c_str());
	DBGWRITE(dbg_level_info, L"UAM type    = %s\n", afp_uam.c_str());

	// Get the AFP version passed by the client into a more usable type
	const auto afp_version_int = get_afp_version_as_int(afp_version.c_str());
	if (afp_version_int == 0)
	{
		DBGWRITE(dbg_level_error, L"Bad AFP version, terminating login\n");
		return afpBadVersNum;
	}

	// Get the UAM version passed by the client into a more usable type
	const auto afp_uam_type = get_uam_type_as_int(afp_uam.c_str());
	if (afp_uam_type == -1)
	{
		DBGWRITE(dbg_level_error, L"Bad UAM type, terminating login\n");
		return (afpBadUAM);
	}

	connection->set_afp_version(afp_version_int);
	connection->set_uam_type(afp_uam_type);

	std::wstring sz_user_name;
	std::wstring sz_domain_name;

	// Every UAM other than guest requires the username, so retrieve it now.
	if (afp_uam_type != afpUAMNone)
	{
		if (afp_command == afpLoginExt)
		{
			auto afp_path_type = afp_request.pull_num<UINT8>();
			sz_user_name = afp_request.pull_string(afp_path_type);
			if (sz_user_name.empty())
			{
				DBGWRITE(dbg_level_error, L"Failed to retrieve user name, terminating login\n");
				return afpParmErr;
			}

			afp_path_type = afp_request.pull_num<UINT8>();
			const auto sz_pathname = afp_request.pull_string(afp_path_type);

			DBGWRITE(dbg_level_info, L"pathname: %s\n", sz_pathname.c_str());
		}
		else
		{
			sz_user_name = afp_request.pull_string();
			if (sz_user_name.empty())
			{
				return afpParmErr;
			}
		}

		// Extract the domain name (if there is one) from the username buffer.
		afp_extract_domain_and_user_name(sz_user_name, sz_domain_name);

		DBGWRITE(dbg_level_info, L"Username    = %s\n", sz_user_name.c_str());
		DBGWRITE(dbg_level_info, L"Domain name = %s\n", sz_domain_name.c_str());

		// We need the name and domain for later to get the user SID.
		connection->set_user_name(sz_user_name);
		connection->set_domain_name(sz_domain_name);

		afp_request.advance_if_odd(); //Padding byte
	}

	char afp_password[UAM_CLRTXTPWDLEN + 1]{};
	std::wstring password;

	switch (afp_uam_type)
	{
	case afpUAMNone:
		{
			if (!g_allow_guests || g_force_encrypted_logon)
			{
				return afpBadUAM;
			}

			// The guest identity is the built-in Guest account, resolved
			// through its canonical name + verified RID (501) so it works
			// on localized Windows (where the display name is not
			// "Guest"). The connection carries the resolved name/domain so
			// the shared logon path authenticates it (blank password)
			// instead of calling LogonUser with an empty name.
			std::wstring guest_name;
			std::wstring guest_domain;

			DBGWRITE(dbg_level_info, L"FPLogin: guest (UAM-none) session, resolving built-in Guest account\n");
			if (!get_guest_account_name(guest_name, guest_domain))
			{
				DBGWRITE(dbg_level_warning,
				         L"UAM-none login: could not resolve the built-in Guest account.\n");
				return afpUserNotAuth;
			}

			DBGWRITE(dbg_level_info, L"FPLogin: guest account resolved to [%s]\\[%s]\n", guest_domain.c_str(), guest_name.c_str());
			connection->set_user_name(guest_name);
			connection->set_domain_name(guest_domain);
			connection->set_guest_session(true);

			sz_user_name = guest_name;
			break;
		}

	case afpUAMXor:
		{
			// Not very secure, but better than just passing passwords in the clear.
			password = afp_request.pull_string();
			if (password.empty())
			{
				DBGWRITE(dbg_level_error, L"Failed to retrieve XOR password, terminating login\n");
				return afpParmErr;
			}

			password = xor_decrypt(password, g_xor_key);
			break;
		}

	case afpUAMCleartext:
		{
			// Make sure we're not supposed to force unencrypted logon. This may
			// be done programatically to work around the mac client!
			if (g_force_encrypted_logon)
			{
				DBGWRITE(dbg_level_warning, L"Attempted cleartext login when encryption is required\n");
				return afpBadUAM;
			}

			// The password is sent to us as an 8 byte array. This array is not length
			// byte preceded or null terminated. Classic clients encode it in
			// Mac Roman (kLongNames on the wire), not a Windows code page.
			afp_request.pull_raw_data(afp_password, UAM_CLRTXTPWDLEN);
			password = string_to_wide_string(afp_password, CP_MACCP);
			break;
		}

	case afpUAMDHCAST128:
		{
			// This login UAM uses SSL to encrypt the password buffer using
			// public and private keys.

			// This is where we store all the information we'll need for the login continue.
            const auto dhx_info = new DHXInfo;
			if (dhx_info == nullptr)
			{
				DBGWRITE(dbg_level_error, L"Failed to allocate DHXInfo structure, exiting.\n");
				return afpMiscErr;
			}

			RtlZeroMemory(dhx_info, sizeof(DHXInfo));

			const AFPERROR afp_error = DHXLogin(
				afp_request,
				afp_reply,
				connection,
				dhx_info
			);

			if (afp_error == afpAuthContinue)
			{
				// There should never be two logins for the same session happening
				// simultaneously. If we find we already have an info buffer,
				// it's bad news.
				if (connection->get_extended_logon_info() != nullptr)
				{
					DBGWRITE(dbg_level_warning, L"WARNING: Already have extended login info!\n");

					connection->free_extended_logon_info();
				}

				connection->set_extended_logon_info(dhx_info);
			}

			return afp_error;
		}

	default:
		return afpBadUAM;
	}

	// Now do the actual logon, the implementation routine "does the right thing".
	DBGWRITE(dbg_level_info, L"FPLogin: performing logon for user [%s]\n", sz_user_name.c_str());
	return afp_imp_user_logon(connection, afp_reply, password);
}


/*
 * FPContLogin()
 *
 * Description:
 *		Continue the login sequence from the encrypted UAM.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPContLogin(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi::dsi_connection* connection
)
{
	AFPERROR afp_error;
	const INT8 uam_type = connection->get_uam_type();

	afp_request.advance_pos_ptr(sizeof(UINT16)); //command and padding

	DBGWRITE(dbg_level_info, L"Enter\n");
	DBGWRITE(dbg_level_info, L"LogonUAM = %d\n", uam_type);

	if (uam_type == afpUAMDHCAST128)
	{
		char sz_password[DHX_PASSWDLEN + 1]{};
		const auto dhx_info = static_cast<DHXInfo*>(connection->get_extended_logon_info());

		// The DHXInfo structure should have been allocated and filled in already
		// from the FPLogin() sequence.
		if (dhx_info == nullptr)
		{
			DBGWRITE(dbg_level_error, L"DHXInfo is invalid\n");
			return afpMiscErr;
		}

		// The heavy lifting of all the encryption/decryption goop is done by the
		// encryption routines.
		afp_error = DHXLoginContinue(
			afp_request,
			afp_reply,
			connection,
			sz_password,
			sizeof(sz_password),
			dhx_info
		);

		if (AFP_SUCCESS(afp_error))
		{
			// We've had a successful key exchange and decryption of passwords. Now
			// convert the password to a unicode string and authenticate this user
			// using the Windows Logon API.

			//The DHX blob carries the password in the client's text encoding;
			//kUnicodeNames means UTF-8 on the wire in this codebase.
			auto password = string_to_wide_string(sz_password, CP_UTF8);
			afp_error = afp_imp_user_logon(connection, afp_reply, password);

		    password.clear();
		}

		SecureZeroMemory(sz_password, sizeof(sz_password));

		// We don't require the extended login info anymore. Free it and
		// be sure the clear the pointer for the next go-around.
		connection->free_extended_logon_info();
	}
	else
	{
		return afpParmErr;
	}

	DBGWRITE(dbg_level_info, L"Returning: %d\n", afp_error);

	return afp_error;
}


/*
 * FPChangePassword()
 *
 * Description:
 *		Change the password for the user using the supplied UAM.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPChangePassword(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi::dsi_connection* connection
)
{
	std::wstring sz_user_name;
	AFPERROR afp_error = afpParmErr;

	DBGWRITE(dbg_level_info, L"Enter...\n");

	afp_request.advance_pos_ptr(sizeof(INT16));

	const auto sz_uam_version = afp_request.pull_string();
	const auto uam_type = get_uam_type_as_int(sz_uam_version.c_str());

	afp_request.advance_if_odd();

	// Starting with AFP3.0, the username is just 2 bytes of zeros.
	if (connection->get_afp_version() > afpVersion22)
	{
		afp_request.advance_pos_ptr(sizeof(INT16));
		sz_user_name = connection->get_user_name();
	}
	else
	{
		sz_user_name = afp_request.pull_string();
		afp_request.advance_if_odd();
	}

	DBGWRITE(dbg_level_info, L"UAM Str:  %s\n", sz_uam_version.c_str());
	DBGWRITE(dbg_level_info, L"Username: %s\n", sz_user_name.c_str());

	switch (uam_type)
	{
	case afpUAMNone:
		return afpBadUAM;

	case afpUAMCleartext:
		{
			char sz_old_password[UAM_CLRTXTPWDLEN + 1];
			char sz_new_password[UAM_CLRTXTPWDLEN + 1];

			//
			//Grab the old and new cleartext passwords from the buffer.
			//
			afp_request.pull_raw_data(sz_old_password, UAM_CLRTXTPWDLEN);
			sz_old_password[UAM_CLRTXTPWDLEN] = 0;
			afp_request.pull_raw_data(sz_new_password, UAM_CLRTXTPWDLEN);
			sz_new_password[UAM_CLRTXTPWDLEN] = 0;

			//**** TO BE IMPLEMENTED ****
			//afpError = afpImpChangePswd(
			//				szUserName,
			//				szOldPassword,
			//				szNewPassword
			//				);

			SecureZeroMemory(sz_old_password, sizeof(sz_old_password));
			SecureZeroMemory(sz_new_password, sizeof(sz_new_password));

			afp_error = AFP_OK;
			break;
		}

	case afpUAMDHCAST128:
		{
			char sz_old_password[DHX_PASSWDLEN + 1];
			char sz_new_password[DHX_PASSWDLEN + 1];
			WCHAR sz_new_password_w[DHX_PASSWDLEN + 1];

			// This may be our second time through in which case dxhInfo is already
			// created and filled with information.
			if (connection->get_extended_logon_info() == nullptr)
			{
				// Must be our first time through, so no blob info saved yet.
                const auto dhx_info = new DHXInfo;
				if (dhx_info == nullptr)
				{
					// We failed to allocate the necessary block of data to store information
					// between login api calls. No choice but to bail out.

					DBGWRITE(dbg_level_warning, L"Failed to allocate DHXInfo structure, exiting.\n");
					return (afpMiscErr);
				}

				RtlZeroMemory(dhx_info, sizeof(DHXInfo));
				connection->set_extended_logon_info(dhx_info);
			}

			memset(sz_old_password, 0, sizeof(sz_old_password));
			memset(sz_new_password, 0, sizeof(sz_new_password));

			// This routine does 2 things:
			//	1) re-logs into the server via DH key exchanges for auth
			//	2) decrypts received old and new passwords based on DH key exchange
			afp_error = DHXChangePassword(
				afp_request,
				afp_reply,
				connection,
				sz_old_password,
				sizeof(sz_old_password),
				sz_new_password,
				sizeof(sz_new_password),
				static_cast<DHXInfo*>(connection->get_extended_logon_info())
			);

			if (afp_error == afpAuthContinue)
			{
				// We've only complete the first step in the key exchange/encryption
				// process. Returning afpAuthContinue, and we'll be called again with
				// the new/old password encrypted payload.

				return afp_error;
			}

			// We're done with the DHXInfo so free it. Also be sure to clear
			// the blob field in the DSI session, or we'll be in trouble
			// next time we change password.
			connection->free_extended_logon_info();

			// We successfully re-logged onto the server and successfully decrypted
			// the old and new passwords. Now convert to unicode strings and
			// call the change password implementation routine.

			if (AFP_SUCCESS(afp_error))
			{
				//Same encoding as the DHX login password (see FPContLogin).
				const std::wstring new_password = string_to_wide_string(sz_new_password, CP_UTF8);

                //**** TO BE IMPLEMENTED ****
                //afpError = afpImpChangePswd(
                //				szUserName,
                //				szOldPassword,
                //				new_password
                //				);
            }

			SecureZeroMemory(sz_old_password, sizeof(sz_old_password));
			SecureZeroMemory(sz_new_password, sizeof(sz_new_password));
			SecureZeroMemory(sz_new_password_w, sizeof(sz_new_password_w));
			break;
		}

	default:
		break;
	}

	DBGWRITE(dbg_level_info, L"Returning: %d\n", afp_error);

	return afp_error;
}


/*
 * FPLogout()
 *
 * Description:
 *		The user is logging out of the server. Clean up the token handle
 *		and user state.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPLogout(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi::dsi_connection* connection
)
{
	UNREFERENCED_PARAMETER(afp_reply);
	UNREFERENCED_PARAMETER(afp_request);

	// Close the handle to the token we're using for impersonation.

	const HANDLE user_token = connection->get_user_token();

	if (user_token != nullptr && connection->get_is_session_authenticated())
	{
		CloseHandle(user_token);

		connection->set_user_token(nullptr);
		connection->set_session_authenticated(false);
		connection->set_guest_session(false);

		// Notify the administrator of the client logoff the system.
		EventWriteAFPServerEvt_ClientLogoff(connection->get_user_name().c_str());
	}
	else
	{
		return afpParmErr;
	}

	return AFP_OK;
}


/*
 * FPMapID()
 *
 * Description:
 *		Map a user ID to a username.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPMapID(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi::dsi_connection* connection
)
{
	UNREFERENCED_PARAMETER(connection);

	AFPERROR afp_error;
	WCHAR sz_name[64]{};

	// Request layout (per the AFP reference): CommandCode, Subfunction, ID.
	// No pad byte after the command - same as FPOpenFork/FPMapName, and
	// matching how netatalk parses it. Advancing more here makes real
	// clients' Subfunction read as the ID's high byte and every lookup
	// fails with afpParmErr (Finder then shows blank owner/group names).
	afp_request.advance_pos_ptr(sizeof(INT8)); // command code

	// What are we doing, user or group?
	const auto afp_sub_func = afp_request.pull_num<INT8>();

	// Retrieve the id of the user or group to obtain.
	const auto id = afp_request.pull_num<UINT32>();

	DBGWRITE(dbg_level_info, L"Looking for ID %d\n", id);

	// 1 = user name, 2 = group name, 3 = Unicode user name, 4 = Unicode
	// group name. Validate before the lookup: an unknown subfunction is a
	// parameter error regardless of the id.
	if (afp_sub_func < 1 || afp_sub_func > 4)
	{
		DBGWRITE(dbg_level_warning, L"Bad FPMapID subfunction (%d)\n", afp_sub_func);
		return afpParmErr;
	}

	afp_error = get_user_name_by_id(id, sz_name, ARRAYSIZE(sz_name));
	if (AFP_FAILED(afp_error))
	{
		return afp_error;
	}

	const bool is_group = afp_sub_func == 2 || afp_sub_func == 4;
	const bool is_unicode = afp_sub_func >= 3;

	DBGWRITE(dbg_level_info, L"Mapped id to %s name %s\n",
	         is_group ? L"group" : L"user", sz_name);

	if (is_unicode)
	{
		afp_reply.push_string(sz_name, kUnicodeNames);
	}
	else
	{
		afp_reply.push_string(sz_name);
	}

	return AFP_OK;
}


/*
 * FPMapName()
 *
 * Description:
 *		Map a username to a user or group ID (the inverse of FPMapID).
 *		Required by clients resolving names typed into the Finder
 *		Sharing panel (owner/group changes) to the ids this server's
 *		directory parameter calls report.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPMapName(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	// ReSharper disable once CppParameterMayBeConstPtrOrRef
	IN dsi::dsi_connection* connection
)
{
	UNREFERENCED_PARAMETER(connection);

	WCHAR name[256]{};

	// Request layout (per the AFP reference): CommandCode, Subfunction,
	// Name. No pad byte after the command.
	afp_request.advance_pos_ptr(sizeof(INT8)); // command code
	const auto afp_sub_sub = afp_request.pull_num<INT8>();

	if (afp_sub_sub < 1 || afp_sub_sub > 4)
	{
		DBGWRITE(dbg_level_warning, L"Bad FPMapName subfunction (%d)\n", afp_sub_sub);
		return afpParmErr;
	}

	const bool is_group = afp_sub_sub == 2 || afp_sub_sub == 4;
	const bool is_unicode = afp_sub_sub >= 3;

	const auto raw_name = afp_request.pull_string(is_unicode ? kUnicodeNames : kLongNames);
	if (raw_name.empty())
	{
		return afpParmErr;
	}

	// The name must refer to a real account of the requested kind; this
	// also rejects stray cached names never issued as ids.
	const auto sid = get_sid_for_user(raw_name);
	if (sid == nullptr)
	{
		DBGWRITE(dbg_level_info, L"FPMapName: account [%s] not found\n", raw_name.c_str());
		return afpItemNotFound;
	}

	DWORD cch_name = ARRAYSIZE(name);
	DWORD cch_domain = 0;
	SID_NAME_USE use = SidTypeUnknown;

	// Size the domain string from the failing first call.
	LookupAccountSidW(nullptr, sid, name, &cch_name, nullptr, &cch_domain, &use);
	if (cch_domain == 0)
	{
		cch_domain = 2;
	}

	std::wstring domain;
	domain.resize(cch_domain + 1);
	cch_name = ARRAYSIZE(name);
	cch_domain = static_cast<DWORD>(domain.size());

	const auto resolved = LookupAccountSidW(nullptr, sid, name, &cch_name,
	                                        domain.data(), &cch_domain, &use);
	LocalFree(sid);

	if (!resolved)
	{
		return afpItemNotFound;
	}

	const bool sid_is_user = use == SidTypeUser;
	const bool sid_is_group = use == SidTypeGroup || use == SidTypeAlias ||
	                          use == SidTypeWellKnownGroup || use == SidTypeLabel;

	if (is_group ? !sid_is_group : !sid_is_user)
	{
		DBGWRITE(dbg_level_info, L"FPMapName: [%s] is not a %s account (%d)\n",
		         raw_name.c_str(), is_group ? L"group" : L"user", use);
		return afpItemNotFound;
	}

	// Cache under the account's canonical (resolved) name so the id is the
	// same one get_directory_owner_and_group_id reports for this account,
	// regardless of how the client spelled it (domain-qualified, etc.).
	const std::wstring account_name = name;
	const auto id = afp_id_for_account_name(is_group ? afp_group_name_type : afp_user_name_type, account_name);

	DBGWRITE(dbg_level_info, L"FPMapName: [%s] -> id %u (%s)\n",
	         account_name.c_str(), id, is_group ? L"group" : L"user");

	afp_reply.push_num<UINT32>(id);

	return AFP_OK;
}


/*
 * FPGetSrvrMsg()
 *
 * Description:
 *		The client calls this API to check and see if there is a server
 *		message pending. The tells the server what type of message it's
 *		looking for, as follows:
 *
 *		Login message	= 0
 *		Server message	= 1  (in response to a message attn code)
 *		
 *		For now, we just ignore the msg bitmap since it really servers
 *		no purpose.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPGetSrvrMsg(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi::dsi_connection* connection
)
{
	UNREFERENCED_PARAMETER(connection);

	WCHAR message[message_max_length + 1]{};
	afp_registry the_registry;

	afp_request.advance_pos_ptr(sizeof(INT16));

	const auto afp_message_type = afp_request.pull_num<UINT16>();
	const auto afp_message_bitmap = afp_request.pull_num<UINT16>();

	switch (afp_message_type)
	{
	case message_login_type:
		DBGWRITE(dbg_level_info, L"Message type = Logon\n");

		the_registry.get_logon_message(message);
		break;

	case message_server_type:
		DBGWRITE(dbg_level_info, L"Message type = Server\n");

		// ReSharper disable once CppFunctionResultShouldBeUsed
		StringCbCopy(message, sizeof(message), g_server_message);
		break;

	default:
		DBGWRITE(dbg_level_warning, L"Bad message type in FPGetSrvrMsg!\n");
		break;
	}

	DBGWRITE(dbg_level_info, L"Message = %s\n", message);

	// Pack in all the parameters.
	afp_reply.push_num(afp_message_type);
	afp_reply.push_num(afp_message_bitmap);

	// Starting with AFP3.0, the client sets bit 1 of the message
	// bitmap to indicate that it wants the message string in
	// unicode format.
	if (afp_message_bitmap & kSendMessageAsUnicode)
	{
		afp_reply.push_string(message, kUnicodeNames);
	}
	else
	{
		afp_reply.push_string(message);
	}

	return AFP_OK;
}
