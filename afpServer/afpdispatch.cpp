#include <stdafx.h>
#include <afpdispatch.h>
#include <afpdesktop.h>
#include <afpdirectory.h>
#include <afpsrvr.h>
#include <afpuser.h>
#include <afpvolumes.h>
#include <afpxattr.h>


using afp_func = AFPERROR(*)(afp_net_buffer& afp_request, afp_net_buffer& afp_reply, dsi::dsi_connection* connection);
using afp_function_table = struct
{
	afp_func func;
};

afp_function_table afp_table[] = {
	{fp_unimplemented}, //0
	{FPByteRangeLock},
	{FPCloseVol},
	{fp_unimplemented}, //afpDirClose
	{FPCloseFork},
	{FPCopyFile},
	{FPCreateDir},
	{FPCreateFile},
	{FPDelete},
	{FPEnumerate},
	{FPFlush},
	{FPFlushFork},
	{fp_unimplemented}, //afpGetDirParms
	{fp_unimplemented}, //afpGetFileParms
	{FPGetForkParms},
	{FPGetSrvrInfo},
	{FPGetSrvrParms},
	{FPGetVolParms},
	{FPLogin},
	{FPContLogin},
	{FPLogout},
	{FPMapID},
	{fp_unimplemented}, //afpMapName
	{FPMoveAndRename},
	{FPOpenVol},
	{fp_unimplemented}, //afpOpenDir
	{FPOpenFork},
	{FPRead},
	{FPRename},
	{FPSetFileDirParms},
	{FPSetFileDirParms},
	{FPSetForkParms},
	{fp_unimplemented}, //afpSetVolParms
	{FPWrite},
	{FPGetFileDirParms},
	{FPSetFileDirParms},
	{fp_unimplemented}, //afpChangePwd
	{fp_unimplemented}, //afpGetUserInfo
	{FPGetSrvrMsg},
	{fp_unimplemented}, //afpCreateID
	{fp_unimplemented}, //afpDeleteID
	{FPResolveID},
	{fp_unimplemented}, //afpExchangeFiles
	{fp_unimplemented}, //43
	{fp_unimplemented}, //44
	{fp_unimplemented}, //45
	{fp_unimplemented}, //46
	{fp_unimplemented}, //47
	{FPOpenDT},
	{FPCloseDT},
	{fp_unimplemented}, //50
	{FPGetIcon},
	{FPGetIconInfo},
	{FPAddAPPL},
	{FPRemoveAPPL},
	{FPGetAPPL},
	{FPAddComment},
	{FPRemoveComment},
	{FPGetComment},
#ifdef AFP_VERSION_30
	{FPByteRangeLock}, //afpByteRangeLockExt
	{FPRead},
	{FPWrite},
	{fp_unimplemented}, //afpGetAuthMethods
	{FPLogin},
	{FPGetSessionToken},
	{FPDisconnectOldSession},
	{FPEnumerate},
	{fp_unimplemented}, //afpCatSearchExt
	{FPEnumerate},
	{FPGetExtAttribute},
	{FPSetExtAttribute},
	{FPRemoveExtAttribute},
	{FPListExtAttribute}
#endif
};


/*
 * fp_unimplemented()
 *
 * Description:
 *		Function used to catch unimplemented afp functions.
 *
 * Returns: afpCallNotSupported
 */

AFPERROR fp_unimplemented(
	IN afp_net_buffer& afp_request,
	// ReSharper disable once CppParameterMayBeConstPtrOrRef
	IN afp_net_buffer& afp_reply,
	IN const LPSESSION dsi_session
)
{
	UNREFERENCED_PARAMETER(afp_request);
	UNREFERENCED_PARAMETER(afp_reply);
	UNREFERENCED_PARAMETER(dsi_session);

	DBGWRITE(dbg_level_error, L"Call not implemented (%d)\n", afp_request.pull_num<INT8>());
	return (afpCallNotSupported);
}


/*
 * fp_dispatch_command()
 *
 * Description:
 *		Dispatch the incoming afp command to the correct FPXXXXX API
 *		implementation function.
 *
 * Returns: AFP error code
 */

AFPERROR fp_dispatch_command(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi::dsi_connection* connection
)
{
	const auto afp_command = afp_request.get_num<INT8>();
	AFPERROR afp_error = afpCallNotSupported;

	// Whether we're authenticated or not is how we determine
	// what afp commands we allow the client to execute.
	if (connection->get_user_token() != nullptr && connection->get_is_session_authenticated())
	{
		if (afp_command <= afpListExtAttr)
		{
			afp_error = afp_table[afp_command].func(afp_request, afp_reply, connection);
		}
		else
		{
			switch (afp_command)
			{
			case afpAddIcon:
				afp_error = FPAddIcon(afp_request, afp_reply, connection);
				break;

			case afpZzzz:
				afp_error = FPZzzz(afp_request, afp_reply, connection);
				break;

			default:
				break;
			}
		}
	}
	else
	{
		switch (afp_command)
		{
		case afpGetSInfo:
			afp_error = FPGetSrvrInfo(afp_request, afp_reply, connection);
			break;

		case afpLoginExt:
		case afpLogin:
			afp_error = FPLogin(afp_request, afp_reply, connection);
			break;

		case afpContLogin:
			afp_error = FPContLogin(afp_request, afp_reply, connection);
			break;

		default:
			DBGWRITE(dbg_level_info, L"Call not implemented (%d)\n", afp_command);
			afp_error = afpAccessDenied;
			break;
		}
	}

	return afp_error;
}
