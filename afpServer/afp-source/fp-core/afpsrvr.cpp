// ReSharper disable CppStringLiteralToCharPointerConversion
// ReSharper disable CppParameterMayBeConstPtrOrRef
// ReSharper disable CppClangTidyClangDiagnosticGnuZeroVariadicMacroArguments
#include "StdAfx.h"
#include "afpsrvr.h"

#include "afp_volumes.h"
#include "volume_array.h"
#include "dsi/dsi_connection.h"

extern volume_array g_volume_array;
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

	const UINT8 afp_num_volumes_active = g_volumes->active_volumes();
	SYSTEMTIME sys_time;
	FILETIME file_time;
		
	// Make sure the session requesting parms is an authenticated
	// user (logged on).
	
	if (!connection->get_is_session_authenticated())
	{
		//
		//Not authenticated, prevent access.
		//
		return (afpAccessDenied);
	}

	//
	//Get the Windows system time for the first part of our buffer.
	//
	GetSystemTime(&sys_time);
	SystemTimeToFileTime(&sys_time, &file_time);

	//
	//Convert the Windows system time to local AFP time.
	//
	const auto afp_time = afp_windows_to_mac_time(&file_time);

	//
	//Stuff in the AFP server time value.
	//
	afp_reply.push_num(afp_time);

	//
	//Stuff in the number of volumes our server has shared.
	//
	afp_reply.push_num(afp_num_volumes_active);

	//
	//Stuff in all the volume names and their flags.
	//
	for (auto it = g_volume_array.begin(); it < g_volume_array.end(); ++it)
	{
		AFP_VOLUME_INFO* afp_volume_info = it->get();

		//
		//First, we check to make sure the volume has been fully indexed. If not
		//we cannot offer it to the clients.
		//
		if (!(afp_volume_info->wVolumeFlags & FLAG_VOLUME_INDEXINGCOMPLETE))
		{
			//
			//Not done indexing, can't show this one now.
			//
			continue;
		}

		DBGWRITE(dbg_level_all, L"Stuffing volume: %s\n", afp_volume_info->szShareName);

		afp_reply.push_num(afp_volume_info->bAFPFlags);
		afp_reply.push_pascal_string(afp_volume_info->szShareName);
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

	unichar computer_name[MAX_COMPUTERNAME_LENGTH + 1];
	UINT32 size = MAX_COMPUTERNAME_LENGTH + 1;
	UINT16 flags = 0;
	UINT8 uam_count = UAM_COUNT;

	GetComputerName(computer_name, reinterpret_cast<LPDWORD>(&size));

	//
	//Set the flags which tells what our server supports.
	//
	flags |= kSupportsTCPIP;
	flags |= kSupportsSrvrNotification;
	flags |= kSupportsCopyFile;
	flags |= kSupportsSrvrMsgs;
	flags |= kSupportsReconnect;

	afp_reply.advance_pos_ptr(sizeof(int16)); //Machine type offset
	afp_reply.advance_pos_ptr(sizeof(int16)); //Afp version count offset
	afp_reply.advance_pos_ptr(sizeof(int16)); //UAM count offset

	afp_reply.push_num<UINT16>(0); //Volume icon and mask offset
	afp_reply.push_num(flags); //Server flags

	afp_reply.push_pascal_string(computer_name);
	afp_reply.advance_if_odd();

	auto* address_count_offset = reinterpret_cast<UINT16*>(afp_reply.get_current_pos_ptr());
	afp_reply.advance_pos_ptr(sizeof(UINT16));

	//
	//Set the machine type string and offset
	//
	afp_reply.add_parameter_offset(SRVRINFO_OFFSET_MACHTYPE, static_cast<int16>(afp_reply.get_data_length()));
	afp_reply.push_pascal_string(UAM_NONE_STR);

	//AFP versions
	afp_reply.add_parameter_offset(SRVRINFO_OFFSET_AFPVERSCOUNT, static_cast<int16>(afp_reply.get_data_length()));
	afp_reply.push_int8(AFP_VERSION_COUNT);
	afp_reply.push_pascal_string(AFP_22_VERSION_STR);
#ifdef AFP_VERSION_30
	afp_reply.push_pascal_string(AFP_30_VERSION_STR);
	afp_reply.push_pascal_string(AFP_31_VERSION_STR);
	afp_reply.push_pascal_string(AFP_32_VERSION_STR);
#endif

	//UAMs supported
	afp_reply.add_parameter_offset(SRVRINFO_OFFSET_UAMCOUNT, static_cast<int16>(afp_reply.get_data_length()));

	//
	//If we only allow encrypted logon (e.g. no clear text), then we
	//need to reduce the count of UAM we're sending over.
	//
	if (g_force_encrypted_logon)
	{
		--uam_count;
	}

	//
	//If no guest support, reduce the count yet again as above.
	//
	if (!g_allow_guests)
	{
		--uam_count;
	}

	afp_reply.push_num(uam_count);

	if (g_allow_guests)
	{
		afp_reply.push_pascal_string(UAM_NONE_STR);
	}

	if (!g_force_encrypted_logon)
	{
		afp_reply.push_pascal_string(UAM_CLEAR_TEXT);
	}

	//
	//The DHCAST128 UAM is Apple's SSL encrypted password scheme, it
	//is always passed as supported, no matter what.
	//
	afp_reply.push_pascal_string(UAM_DHCAST128);

	//Network Address: Note, there is no reason for us to pass
	//back address since we only support connections over TCP/IP.
	//This feature is only for connection where the initial connection
	//is done over AppleTalk.

	*address_count_offset = static_cast<int16>(afp_reply.get_data_length());
	afp_reply.push_num<UINT8>(0);

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

	DBGWRITE(dbg_level_info, L"[AFP:FpZzzz]Enter...\n");

	afp_request.advance_pos_ptr(sizeof(int16));

	// Nab the flag parameter, this is a reserved field by
	// Apple and is ignored now.
	// ReSharper disable once CppAssignedValueIsNeverUsed
	// ReSharper disable once CppDeclaratorNeverUsed
	uint32 afp_flag = afp_request.pull_num<UINT32>();

	// Set the client sleeping flag so we don't kill it when we
	// don't hear from it.
	connection->set_sleep_state(true);

	return AFP_OK;
}
