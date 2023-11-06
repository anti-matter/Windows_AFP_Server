// ReSharper disable CppClangTidyClangDiagnosticGnuZeroVariadicMacroArguments
// ReSharper disable CppParameterMayBeConstPtrOrRef
#include "StdAfx.h"
#include "afpuser.h"

#include <logonimp.h>

#include "afpEvents.h"
#include "afpdhxlogin.h"
#include "afp_registry.h"
#include "afp_session.h"
#include "dsi/dsi_connections.h"


WCHAR g_server_message[message_max_length + 1];
BOOL g_allow_guests = TRUE;
DWORD g_max_sessions = 0;
BOOL g_force_encrypted_logon = FALSE;

extern dsi::dsi_connections active_connections;


UINT8 get_afp_version_as_int(const WCHAR* version)
{
	int8 result = 0;

	if (!lstrcmp(version, AFP_22_VERSION_STR))
		result = afpVersion22;
	else if (!lstrcmp(version, AFP_30_VERSION_STR))
		result = afpVersion30;
	else if (!lstrcmp(version, AFP_31_VERSION_STR))
		result = afpVersion31;
	else if (!lstrcmp(version, AFP_32_VERSION_STR))
		result = afpVersion32;
	else
	{
		ASSERT(0);
	}

	return result;
}


INT8 get_uam_type_as_int(const WCHAR* uam_string)
{
	int8 result = -1;

	if (!lstrcmp(uam_string, UAM_NONE_STR))
		result = afpUAMNone;
	else if (!lstrcmp(uam_string, UAM_CLEAR_TEXT))
		result = afpUAMCleartext;
	else if (!lstrcmp(uam_string, UAM_DHCAST128))
		result = afpUAMDHCAST128;
	else
	{
		ASSERT(0);
	}

	return (result);
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
	int32 afp_id_size = 0;
	std::unique_ptr<char[]> afp_id;
	uint32 afp_time_stamp = 0;
	const UINT32 afp_token = session->get_token();

    DBGWRITE(dbg_level_info, L"Enter...\n");

	afp_request.advance_pos_ptr(sizeof(int16));
	const auto afp_type = afp_request.pull_num<INT16>();

	if (afp_type != kLoginWithoutID)
	{
		afp_id_size = afp_request.pull_num<INT32>();

		DBGWRITE(dbg_level_info, L"ID size from client = %d\n", afp_id_size);

		// For types 3 & 4, a time stamp is included for additional comparison.
		if (afp_type == kLoginWithTimeAndID || afp_type == kReconnWithTimeAndID)
		{
			afp_time_stamp = afp_request.pull_num<UINT32>();
		}

		// Save the AFP id in the AFPSession object for later comparison.
		afp_id = std::make_unique<char[]>(afp_id_size);

		if (afp_id != nullptr)
		{
			afp_request.pull_raw_data(afp_id.get(), static_cast<INT16>(afp_id_size));
		}
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

	// All these id's are unsupported by this AFP Server.
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
 *		AFP clients call this when they are inadvertently cutoff from
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
	const int32 afp_token_length = afp_request.pull_num<INT32>();

	if (afp_token_length != AFP_SESSION_TOKEN_SIZE)
	{
		DBGWRITE(dbg_level_info, L"Bad token length\n");
		return afpMiscErr;
	}

	const auto afp_token = afp_request.pull_num<UINT32>();
	const afp_session* afp_lost_session = active_connections.find_afp_session_with_token(afp_token);

	if (afp_lost_session != nullptr)
	{
		DBGWRITE(dbg_level_info, L"Found diconnected session!\n");
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
	const int8 afp_command = afp_request.pull_num<INT8>();

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
		return afpBadUAM;
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

	// Every UAM other then guest requires the user name, so retrieve it now.
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

			sz_user_name = L"Guest";
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
			// byte preceded or null terminated.
			afp_request.pull_raw_data(afp_password, UAM_CLRTXTPWDLEN);
			password = string_to_wide_string(afp_password);

			DBGWRITE(dbg_level_info, L"Cleartext password = %s\n", password.c_str());
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
					DBGWRITE(dbg_level_warning, L"WARNING: Alread have extended login info!\n");
					ASSERT(0);

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

			auto password = string_to_wide_string(sz_password);
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

	afp_request.advance_pos_ptr(sizeof(int16));

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
					// We failed to allocated the necessary block of data to store information
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
				// process. Returning afpAuthContinue and we'll be called again with
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
				if (!afp_mac_to_windows_string(sz_new_password, sizeof(sz_new_password_w), sz_new_password_w))
				{
					DBGWRITE(dbg_level_warning, L"Error converting new password to unicode\n");
					afp_error = afpMiscErr;
				}
				else
				{
					//**** TO BE IMPLEMENTED ****
					//afpError = afpImpChangePswd(
					//				szUserName,
					//				szOldPassword,
					//				szNewPassword
					//				);
				}
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
 *		Map a user ID to a user name.
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
	WCHAR sz_name[64];

	afp_request.advance_pos_ptr(sizeof(INT8));

	// What are we doing, user or group?
	const auto afp_sub_func = afp_request.pull_num<INT8>();

	// Retrieve the id of the user or group to obtain.
	const auto afp_user_id = afp_request.pull_num<UINT32>();

	DBGWRITE(dbg_level_info, L"Looking for ID %d\n", afp_user_id);

	switch (afp_sub_func)
	{
	//Looking for user name
	case 1:
		afp_error = afp_get_user_name_by_id(afp_user_id, sz_name);

		if (AFP_SUCCESS(afp_error))
		{
			DBGWRITE(dbg_level_info, L"Mapped id to name %s\n", sz_name);
			afp_reply.push_string(sz_name);
		}
		break;

	//Looking for group name
	case 2:
		afp_reply.push_string(L"Everyone");
		break;

	//Unicode user name
	case 3:
		afp_error = afp_get_user_name_by_id(afp_user_id, sz_name);

		if (AFP_SUCCESS(afp_error))
		{
			DBGWRITE(dbg_level_info, L"Mapped id to name %s\n", sz_name);
			afp_reply.push_unicode_string(sz_name);
		}
		break;

	//Unicode group name
	case 4:
		// ReSharper disable once CppStringLiteralToCharPointerConversion
		afp_reply.push_unicode_string(TEXT("Everyone"));
		break;

	default:
		ASSERT(0);
		break;
	}

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

	unichar message[message_max_length + 1];
	afp_registry the_registry;

	afp_request.advance_pos_ptr(sizeof(int16));

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
		ASSERT(0);
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
		afp_reply.push_unicode_string(message);
	}
	else
	{
		afp_reply.push_pascal_string(message);
	}

	return AFP_OK;
}
