// ReSharper disable CppStringLiteralToCharPointerConversion
// ReSharper disable CppParameterMayBeConstPtrOrRef
// ReSharper disable CppClangTidyClangDiagnosticGnuZeroVariadicMacroArguments
#include "StdAfx.h"
#include "afpsrvr.h"

#include "afphelp.h"
#include "afp_volumes.h"
#include "dsi/dsi_connection.h"

extern BOOL g_allow_guests;
extern BOOL g_force_encrypted_logon;
extern std::unique_ptr<afp_volumes> g_volumes;


/*
 * FPGetSrvrParms()
 *
 * Description:
 *		Returns server parameters, mainly, what volumes are available
 *		for this server.
 *
 * Returns: AFP error code
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPGetSrvrParms(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi::dsi_connection* connection
)
{
	UNREFERENCED_PARAMETER(afp_request);

	std::vector<std::shared_ptr<afp_volume>> afp_volume_list;
	const auto afp_num_volumes_active = g_volumes->active_volumes(&afp_volume_list);
	SYSTEMTIME sys_time;
	FILETIME file_time;
		
	// Make sure the session requesting parms is an authenticated
	// user (logged on).
	
	if (!connection->get_is_session_authenticated())
	{
		// Not authenticated, prevent access.
		return afpAccessDenied;
	}

	// Get the Windows system time for the first part of our buffer.
	GetSystemTime(&sys_time);
	SystemTimeToFileTime(&sys_time, &file_time);

	// Convert the Windows system time to local AFP time.
	const auto afp_time = afp_windows_to_mac_time(&file_time);

	// Stuff in the AFP server time value.
	afp_reply.push_num(afp_time);

	// Stuff in the number of volumes our server has shared.
	afp_reply.push_num(afp_num_volumes_active);

	// Stuff in all the volume names and their flags.
	for (const auto& volume : afp_volume_list)
	{
		if (!(volume->get_internal_flags() & flag_volume_indexing_complete))
		{
			// Not done indexing, can't show this one now.
			continue;
		}

		DBGWRITE(dbg_level_trace, L"Stuffing volume: %s\n", volume->get_share_name().c_str());

		afp_reply.push_num(volume->get_afp_flags());
		afp_reply.push_string(volume->get_share_name());
	}

	return AFP_OK;
}


/*
 * FPGetSrvrInfo()
 *
 * Description:
 *		Returns a block of information about our server.
 *
 * Returns: AFP error code
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPGetSrvrInfo(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi::dsi_connection* /*connection*/
)
{
	UNREFERENCED_PARAMETER(afp_request);

	WCHAR computer_name[MAX_COMPUTERNAME_LENGTH + 1];
	UINT32 size = MAX_COMPUTERNAME_LENGTH + 1;
	UINT16 flags = 0;
	UINT8 uam_count = UAM_COUNT;

	GetComputerName(computer_name, reinterpret_cast<LPDWORD>(&size));

	// Set the flags which tells what our server supports.
	flags |= kSupportsTCPIP;
	flags |= kSupportsSrvrNotification;
	flags |= kSupportsCopyFile;
	flags |= kSupportsSrvrMsgs;
	flags |= kSupportsExtSleep;
	//flags |= kSupportsReconnect;

	afp_reply.advance_pos_ptr(sizeof(UINT16)); //Machine type offset
	afp_reply.advance_pos_ptr(sizeof(UINT16)); //Afp version count offset
	afp_reply.advance_pos_ptr(sizeof(UINT16)); //UAM count offset

	afp_reply.push_num<UINT16>(0); //Volume icon and mask offset
	afp_reply.push_num(flags); //Server flags

	afp_reply.push_string(computer_name);
	afp_reply.advance_if_odd();

	afp_reply.push_num<INT16>(0); //server sig offset
	afp_reply.push_num<INT16>(0); //address count offset
	afp_reply.push_num<INT16>(0); //directory names offset
	afp_reply.push_num<INT16>(0); //UTF-8 server name offset

	// Set the machine type string and offset
	afp_reply.set_parameter_offset_value(offset_machine_type, static_cast<INT16>(afp_reply.get_data_length()));
	afp_reply.push_string(AFP_MACHINE_TYPE);

	// AFP versions
	afp_reply.set_parameter_offset_value(offset_afp_version_count, static_cast<INT16>(afp_reply.get_data_length()));
	afp_reply.push_num<INT8>(AFP_VERSION_COUNT);
	afp_reply.push_string(AFP_22_VERSION_STR);
#ifdef AFP_VERSION_30
	afp_reply.push_string(AFP_30_VERSION_STR);
	afp_reply.push_string(AFP_31_VERSION_STR);
	afp_reply.push_string(AFP_32_VERSION_STR);
#endif

	// UAMs supported

	// If we only allow encrypted logon (e.g. no clear text), then we
	// need to reduce the count of UAM we're sending over.
	if (g_force_encrypted_logon)
	{
		--uam_count;
	}

	// If no guest support, reduce the count yet again as above.
	if (!g_allow_guests)
	{
		--uam_count;
	}

	afp_reply.set_parameter_offset_value(offset_uam_count, static_cast<INT16>(afp_reply.get_data_length()));
	afp_reply.push_num(uam_count);

	if (g_allow_guests)
	{
		afp_reply.push_string(UAM_NONE_STR);
	}

	if (!g_force_encrypted_logon)
	{
		afp_reply.push_string(UAM_CLEAR_TEXT);
	}

	// The DHCAST128 UAM is Apple's SSL encrypted password scheme, it
	// is always passed as supported, no matter what.
	afp_reply.push_string(UAM_DHCAST128);

	return NO_ERROR;
}


/*
 * FPZzzz()
 *
 * Description:
 *		The client is going to sleep, we need to signal this so
 *		we don't kill the client when we don't hear from it.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPZzzz(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi::dsi_connection* connection
)
{
	UNREFERENCED_PARAMETER(afp_reply);

	DBGWRITE(dbg_level_info, L"Enter...\n");

	afp_request.advance_pos_ptr(sizeof(INT16));

	// ReSharper disable once CppTooWideScope
	const UINT32 flags = afp_request.pull_num<UINT32>();

	// Set the client sleeping flag so we don't kill it when we
	// don't hear from it to preserve session parameters.
	switch (flags)
	{
	case 1:
		DBGWRITE(dbg_level_info, "Setting client to sleep\n");
		connection->set_sleep_state(true);
		break;

	case 2:
		DBGWRITE(dbg_level_info, "Setting client to awake\n");
		connection->set_sleep_state(false);
		break;

	default:
		DBGWRITE(dbg_level_error, "Invalid FpZzzz flag: %d\n", flags);
		return afpParmErr;
	}

	return AFP_OK;
}
