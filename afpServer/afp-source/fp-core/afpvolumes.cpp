// ReSharper disable CppClangTidyClangDiagnosticGnuZeroVariadicMacroArguments
// ReSharper disable CppParameterMayBeConstPtrOrRef
#include "StdAfx.h"
#include "afpvolumes.h"
#include "afp_volumes.h"

extern std::unique_ptr<afp_volumes> g_volumes;

/*
 * FPOpenVol()
 *
 * Description:
 *		Opens a volume so a workstation can access it.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPOpenVol(
    IN afp_net_buffer& afp_request,
    IN afp_net_buffer& afp_reply,
    IN dsi::dsi_connection* connection
)
{
    unichar afp_volume_name[MAX_VOLNAME_LEN + 1];
    const auto afp_session = connection->get_afp_session();
    UINT16 afp_volume_id = 0;
    AFPERROR afp_error = AFP_OK;

    afp_request.advance_pos_ptr(sizeof(UINT16));
    const auto afp_bitmap = afp_request.pull_num<UINT16>();

    if (AFP_FAILED(afp_request.get_pascal_string(afp_volume_name, sizeof(afp_volume_name))))
    {
        return afpParmErr;
    }

    DBGWRITE(dbg_level_all, L"Opening volume: %s\n", afp_volume_name);

    // Get the volume ID for the volume to be opened.
    if (!g_volumes->map_name_to_id(afp_volume_name, &afp_volume_id))
    {
        DBGWRITE(dbg_level_warning, L"Requested volume doesn't exist! (%s)\n", afp_volume_name);
        return afpParmErr;
    }

    // Vol passwords not supported in AFP2.2+ ??
#if 0
    afpError = AFPCheckVolumePassword(afpVolumeID, &pswd);

    if (AFP_FAILED(afpError))
    {
        DBGWRITE(DbgLevelWarning, L"Bad volume password!!\n");
        return( afpError );
    }
#endif

    // Get the volume parameters requested in the open call via the bitmap.
    afp_error = AFPGetVolumeParameters(afp_volume_id, afp_bitmap, afp_reply);

    if (AFP_SUCCESS(afp_error))
    {
        // Now call the volume routine to actually flag the vol as open.
        // Note, we don't error check here since we wouldn't get here with
        // bad data (see above).
        afp_error = afp_session->open_volume(afp_volume_id);
    }
    else
    {
        DBGWRITE(dbg_level_warning, L"AFPGetVolumeParameters() failed (%d)\n", afp_error);
        afp_reply.rewind();
    }

    return afp_error;
}


/*
 * FPCloseVol()
 *
 * Description:
 *		Closes an open afp volume.
 *
 * Returns: AFPERROR
 */

AFPERROR FPCloseVol(
    IN afp_net_buffer& afp_request,
    IN afp_net_buffer& afp_reply,
    IN dsi::dsi_connection* connection
)
{
    UNREFERENCED_PARAMETER(afp_reply);

    afp_request.advance_pos_ptr(sizeof(int16));
    const auto afp_volume_id = afp_request.pull_num<INT16>();
    const auto afp_session = connection->get_afp_session();

    return afp_session->close_volume(afp_volume_id);
}


/*
 * FPFlush()
 *
 * Description:
 *		Flushes the volume. For Windows, we don't do anything here since Windows
 *		doesn't have a flush concept.
 *
 * Returns: AFPERROR
 */

 // ReSharper disable once CppInconsistentNaming
AFPERROR FPFlush(
    IN afp_net_buffer& afp_request,
    IN afp_net_buffer& afp_reply,
    IN dsi::dsi_connection* connection
)
{
    UNREFERENCED_PARAMETER(afp_reply);

    afp_request.advance_pos_ptr(sizeof(int16));
    const auto afp_volume_id = afp_request.pull_num<INT16>();

    if (!connection->get_afp_session()->session_has_volume_open(afp_volume_id))
    {
        return afpParmErr;
    }

    return AFP_OK;
}


/*
 * FPGetVolParms()
 *
 * Description:
 *		Returns the requested volume parameters for a specified volume.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPGetVolParms(
    IN afp_net_buffer& afp_request,
    IN afp_net_buffer& afp_reply,
    IN dsi::dsi_connection* connection
)
{
    DBGWRITE(dbg_level_all, L"FPGetVolParms...\n");

    afp_request.advance_pos_ptr(sizeof(int16));
    const auto afp_volume_id = afp_request.pull_num<INT16>();

    // The client must have previously opened the volume we are operating on.
    if (!connection->get_afp_session()->session_has_volume_open(afp_volume_id))
    {
        // The volume has not been opened, we need to return error.
        DBGWRITE(dbg_level_info, L"Bad volume ID passed by client!\n");
        return (afpParmErr);
    }

    // Extract the bitmap that requests vol data from the buffer.
    const auto afp_bitmap = afp_request.pull_num<UINT16>();

    // Get the volume parameters requested in the open call via the bitmap.
    const AFPERROR afp_error = AFPGetVolumeParameters(afp_volume_id, afp_bitmap, afp_reply);

    if (AFP_FAILED(afp_error))
    {
        DBGWRITE(dbg_level_info, L"AFPGetVolumeParameters() failed (%d)\n", afp_error);
        afp_reply.rewind();
    }

    return afp_error;
}
