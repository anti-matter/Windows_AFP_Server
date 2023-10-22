// ReSharper disable CppClangTidyClangDiagnosticGnuZeroVariadicMacroArguments
// ReSharper disable CppParameterMayBeConstPtrOrRef
#include "StdAfx.h"
#include "afp.h"
#include "afpdesktop.h"
#include "afpdirectory.h"
#include "afp_session.h"
#include "fdparms.h"

using namespace dsi;

/*
 * FPOpenDT()
 *
 * Description:
 *		Open the desktop database for access.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPOpenDT(
	afp_net_buffer& afp_request,
	afp_net_buffer& afp_reply,
	dsi_connection* connection
)
{
	UINT16 afp_dt_ref_num = 0;

	//
	//First WORD is afp command and padding byte.
	//
	afp_request.advance_pos_ptr(sizeof(int16));

	const auto afp_volume_id = afp_request.pull_num<UINT16>();
	const AFPERROR afp_error = connection->get_afp_session()->open_desk(afp_volume_id, &afp_dt_ref_num);

	if (AFP_SUCCESS(afp_error))
	{
		afp_reply.push_num<UINT16>(afp_dt_ref_num);
	}

	return afp_error;
}


/*
 * FPCloseDT()
 *
 * Description:
 *		Closes an open desktop database.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPCloseDT(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi_connection* connection
)
{
	UNREFERENCED_PARAMETER(afp_reply);

	afp_request.advance_pos_ptr(sizeof(int16));

	const auto afp_dt_ref_num = afp_request.pull_num<UINT16>();
	const AFPERROR afp_error = connection->get_afp_session()->close_desk(afp_dt_ref_num);

	return afp_error;
}


/*
 * FPAddIcon()
 *
 * Description:
 *		Adds an icon to the desktop database.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPAddIcon(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi_connection* connection
)
{
	UNREFERENCED_PARAMETER(afp_reply);

	DESKTOP_ENTRY icon_entry;
	AFPERROR afp_error = AFP_OK;

	// The first work contains the command and padding byte.
	afp_request.advance_pos_ptr(sizeof(int16));

	const auto afp_dt_ref_num = afp_request.pull_num<UINT16>();

	// Grab the file type and creator that will be associated with
	// this icon. Note that in MacOS X, they appear to be doing away
	// from creators and types.
	icon_entry.fileCreator = afp_request.pull_num<UINT32>();
	icon_entry.fileType = afp_request.pull_num<UINT32>();

	// Next bytes contains the icon type we'll be storing. We don't
	// care what it is, its for the client to know.
	icon_entry.iconType = afp_request.pull_num<INT8>();

	// A padding byte, skip it.
	afp_request.advance_pos_ptr(sizeof(int8));

	// Get the next 2 parameters; We don't know or care what the tag is and
	// we get the size of the data we'll be storing.
	icon_entry.entryTag = afp_request.pull_num<UINT32>();
	icon_entry.dataSize = afp_request.pull_num<UINT16>();
	icon_entry.entryType = ENTRY_TYPE_ICON;

	// Advance in the buffer to the offset where the icon data is
	// being stored.

	if (icon_entry.dataSize > MAX_DESKTOP_DATA_SIZE)
	{
		// Nothing to do here but bail out!
		return afpParmErr;
	}

	// Now copy the actual icon data into our buffer to store.
	afp_request.pull_raw_data(icon_entry.data, icon_entry.dataSize);

	// Now call the desktop routine to do the rest of the work.
	OPEN_DESKTOP* pDesktop = connection->get_afp_session()->find_desk_file(afp_dt_ref_num);

	if (pDesktop != nullptr)
	{
		afp_error = AFPAddEntry(&icon_entry, pDesktop);
	}

	return afp_error;
}


/*
 * FPGetIcon()
 *
 * Description:
 *		Retrieve and icon from the database.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPGetIcon(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi_connection* connection
)
{
	DESKTOP_ENTRY search_entry;
	DESKTOP_ENTRY icon_entry;

	afp_request.advance_pos_ptr(sizeof(UINT16));

	const auto afp_dt_ref_num = afp_request.pull_num<UINT16>();

	search_entry.fileCreator = afp_request.pull_num<UINT32>();
	search_entry.fileType = afp_request.pull_num<UINT32>();
	search_entry.iconType = afp_request.pull_num<INT8>();

	afp_request.advance_pos_ptr(sizeof(INT8));

	const auto afp_length = afp_request.pull_num<UINT16>();
	search_entry.entryType = ENTRY_TYPE_ICON;

	const auto desktop = connection->get_afp_session()->find_desk_file(afp_dt_ref_num);

	if (desktop == nullptr)
	{
		return afpParmErr;
	}

	const auto afp_error = AFPFindEntry(&search_entry, DONT_USE_INDEX, desktop, &icon_entry);

	if (AFP_SUCCESS(afp_error))
	{
		// We found the bitmap and the caller actually has memory
		// reserved for the bitmap.

		// As per the spec, even if the client didn't reserve enough
		// space, we still copy what we can.
		const auto afp_copy_size = min(afp_length, icon_entry.dataSize);
		afp_reply.push_raw_data(icon_entry.data, afp_copy_size);
	}

	return afp_error;
}


/*
 * FPGetIconInfo()
 *
 * Description:
 *		Retrieve about an icon in the database.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPGetIconInfo(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi_connection* connection
)
{
	DESKTOP_ENTRY search_entry;
	DESKTOP_ENTRY found_entry;

	afp_request.advance_pos_ptr(sizeof(UINT16));
	const auto afp_dt_ref_num = afp_request.pull_num<UINT16>();

	search_entry.fileCreator = afp_request.pull_num<UINT32>();
	const auto afp_icon_index = afp_request.pull_num<UINT16>();
	search_entry.entryType = ENTRY_TYPE_ICON;

	const auto desktop = connection->get_afp_session()->find_desk_file(afp_dt_ref_num);

	if (desktop == nullptr)
	{
		return afpParmErr;
	}

	const auto afp_error = AFPFindEntry(&search_entry, afp_icon_index, desktop, &found_entry);

	if (AFP_SUCCESS(afp_error))
	{
		afp_reply.push_num(found_entry.entryTag);
		afp_reply.push_raw_data(&found_entry.fileType, sizeof(FILETYPE));
		afp_reply.push_num(found_entry.iconType);
		afp_reply.push_num<INT8>(0);
		afp_reply.push_num(found_entry.dataSize);
	}

	return afp_error;
}


/*
 * FPAddComment()
 *
 * Description:
 *		Add a comment for a file or directory to the database.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPAddComment(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi_connection* connection
)
{
	UNREFERENCED_PARAMETER(afp_reply);

	DESKTOP_ENTRY new_entry;
	p_afp_index afp_entry = nullptr;

	DBGWRITE(dbg_level_all, L"[afp:FPAddComment]Enter\n");

	afp_request.advance_pos_ptr(sizeof(int16));

	const auto afp_dt_ref_num = afp_request.pull_num<UINT16>();
	new_entry.dirID = afp_request.pull_num<UINT32>();
	new_entry.pathType = afp_request.pull_num<INT8>();

	auto afp_error = afp_request.get_pascal_string(
		new_entry.pathname,
		sizeof(new_entry.pathname), 
		true,
		new_entry.pathType,
		nullptr);

	if (afp_error != AFP_OK)
	{
		return (afpParmErr);
	}

	afp_request.advance_if_odd();
	afp_request.get_pascal_string(new_entry.data, sizeof(new_entry.data));

	new_entry.entryType = ENTRY_TYPE_CMNT;
	new_entry.dataSize = static_cast<UINT16>(strlen(new_entry.data));

	const auto desktop = connection->get_afp_session()->find_desk_file(afp_dt_ref_num);

	if (desktop == nullptr)
	{
		return afpParmErr;
	}

	afp_error = AFPIndexFindEntryWithVolIDAndPath(
		desktop->volID,
		new_entry.dirID,
		new_entry.pathname,
		&afp_entry);

	if (afp_error != AFP_OK || GetFileAttributes(afp_entry->sz_path) == AFP_MAX_DWORD)
	{
		DBGWRITE(dbg_level_info, L"[afp:FPAddComment]Path not found!\n");
		afp_error = afpObjectNotFound;
	}
	else
	{
		afp_error = AFPAddEntry(&new_entry, desktop);
	}

	return afp_error;
}


/*
 * FPGetComment()
 *
 * Description:
 *		Get a comment for a file or directory from the database.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPGetComment(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi_connection* connection
)
{
	DESKTOP_ENTRY search_entry;
	DESKTOP_ENTRY found_entry;
	p_afp_index afp_entry = nullptr;

	DBGWRITE(dbg_level_all, L"[afp:FPGetComment]Enter\n");

	afp_request.advance_pos_ptr(sizeof(int16));

	const auto afp_dt_refnum = afp_request.pull_num<UINT16>();
	search_entry.dirID = afp_request.pull_num<UINT32>();
	search_entry.pathType = afp_request.pull_num<INT8>();
	search_entry.entryType = ENTRY_TYPE_CMNT;

	auto afp_error = afp_request.get_pascal_string(search_entry.pathname, sizeof(search_entry.pathname), true,
		search_entry.pathType, nullptr);

	if (AFP_FAILED(afp_error))
	{
		return (afpParmErr);
	}

	const auto desktop = connection->get_afp_session()->find_desk_file(afp_dt_refnum);

	if (desktop == nullptr)
	{
		return afpParmErr;
	}

	afp_error = AFPIndexFindEntryWithVolIDAndPath(
		desktop->volID,
		search_entry.dirID,
		search_entry.pathname,
		&afp_entry);

	if (AFP_FAILED(afp_error) || GetFileAttributes(afp_entry->sz_path) == AFP_MAX_DWORD)
	{
		DBGWRITE(dbg_level_info, L"[afp:FPGetComment]Path not found!\n");
		afp_error = afpObjectNotFound;
	}
	else
	{
		afp_error = AFPFindEntry(&search_entry, DONT_USE_INDEX, desktop, &found_entry);

		if (AFP_SUCCESS(afp_error))
		{
			afp_reply.push_pascal_string(found_entry.data);
		}
	}

	return afp_error;
}


/*
 * FPRemoveComment()
 *
 * Description:
 *		Remove a comment for a file or directory from the database.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPRemoveComment(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi_connection* connection
)
{
	UNREFERENCED_PARAMETER(afp_reply);

	DESKTOP_ENTRY search_entry;

	afp_request.advance_pos_ptr(sizeof(int16));

	const auto afp_dt_refnum = afp_request.pull_num<UINT16>();
	search_entry.dirID = afp_request.pull_num<UINT32>();
	search_entry.pathType = afp_request.pull_num<INT8>();
	search_entry.entryType = ENTRY_TYPE_CMNT;

	auto afp_error = afp_request.get_pascal_string(search_entry.pathname, sizeof(search_entry.pathname), true,
		search_entry.pathType, nullptr);

	if (AFP_FAILED(afp_error))
	{
		return afpParmErr;
	}

	const auto desktop = connection->get_afp_session()->find_desk_file(afp_dt_refnum);

	if (desktop == nullptr)
	{
		return afpParmErr;
	}

	afp_error = AFPRemoveEntry(&search_entry, desktop);

	return afp_error;
}


/*
 * FPGetAPPL()
 *
 * Description:
 *		Get an APPL entry associated with a file type from the database.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPGetAPPL(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi_connection* connection
)
{
	WIN32_FILE_ATTRIBUTE_DATA winAttributes;
	DESKTOP_ENTRY search_entry, found_entry;
	p_afp_index afpEntry = nullptr;

	afp_request.advance_pos_ptr(sizeof(int16));
	const auto afp_dt_refnum = afp_request.pull_num<UINT16>();

	afp_request.pull_raw_data(&search_entry.fileCreator, sizeof(FILECREATOR));

	const auto afp_index = afp_request.pull_num<UINT16>();
	const auto afp_bitmap = afp_request.pull_num<UINT16>();

	search_entry.entryType = ENTRY_TYPE_APPL;

	const auto desktop = connection->get_afp_session()->find_desk_file(afp_dt_refnum);
	if (desktop == nullptr)
	{
		return afpParmErr;
	}

	auto afp_error = AFPFindEntry(
		&search_entry,
		afp_index,
		desktop,
		&found_entry
	);

	if (AFP_SUCCESS(afp_error))
	{
		afp_error = AFPIndexFindEntryWithVolIDAndPath(
			desktop->volID,
			found_entry.dirID,
			found_entry.pathname,
			&afpEntry
		);

		if (AFP_SUCCESS(afp_error))
		{
			const uint16 afp_volume_id = desktop->volID;

			if (!GetFileAttributesEx(afpEntry->sz_path, GetFileExInfoStandard, &winAttributes))
			{
				return afpObjectNotFound;
			}

			afp_reply.push_num(afp_bitmap);
			afp_reply.push_num(found_entry.entryTag);

			afp_error = AFPGetObjectParms(
				afp_reply,
				afp_bitmap,
				afp_volume_id,
				found_entry.dirID,
				connection,
				afpEntry,
				winAttributes
			);
		}
	}

	return afp_error;
}


/*
 * FPAddAPPL()
 *
 * Description:
 *		Add an APPL entry associated with a file type to the database.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPAddAPPL(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi_connection* connection
)
{
	UNREFERENCED_PARAMETER(afp_reply);

	DESKTOP_ENTRY newEntry;

	afp_request.advance_pos_ptr(sizeof(int16));

	const auto afp_dt_refnum = afp_request.pull_num<UINT16>();

	newEntry.dirID = afp_request.pull_num<UINT32>();
	newEntry.fileCreator = afp_request.pull_num<UINT32>();
	newEntry.entryTag = afp_request.pull_num<UINT32>();
	newEntry.pathType = afp_request.pull_num<INT8>();
	newEntry.entryType = ENTRY_TYPE_APPL;
	newEntry.dataSize = 0;

	auto afp_error = afp_request.get_pascal_string(newEntry.pathname, sizeof(newEntry.pathname), true, newEntry.pathType,
		nullptr);
	if (AFP_FAILED(afp_error))
	{
		return afpParmErr;
	}

	const auto desktop = connection->get_afp_session()->find_desk_file(afp_dt_refnum);
	if (desktop == nullptr)
	{
		return afpParmErr;
	}

	afp_error = AFPAddEntry(&newEntry, desktop);

	return afp_error;
}


/*
 * FPRemoveAPPL()
 *
 * Description:
 *		Remove an APPL entry associated with a file type to the database.
 *
 * Returns: AFPERROR
 */

// ReSharper disable once CppInconsistentNaming
AFPERROR FPRemoveAPPL(
	IN afp_net_buffer& afp_request,
	IN afp_net_buffer& afp_reply,
	IN dsi_connection* connection
)
{
	UNREFERENCED_PARAMETER(afp_reply);

	DESKTOP_ENTRY search_entry;

	afp_request.advance_pos_ptr(sizeof(int16));

	const auto afp_dt_refnum = afp_request.pull_num<UINT16>();
	search_entry.dirID = afp_request.pull_num<UINT32>();
	search_entry.fileCreator = afp_request.pull_num<UINT32>();
	search_entry.pathType = afp_request.pull_num<INT8>();
	search_entry.entryType = ENTRY_TYPE_APPL;

	auto afp_error = afp_request.get_pascal_string(search_entry.pathname, sizeof(search_entry.pathname), true,
		search_entry.pathType, nullptr);
	if (AFP_FAILED(afp_error))
	{
		return afpParmErr;
	}

	// Although the spec says to, we don't check to make sure that
	// the actual application really exists on the volume. Why? Because
	// when you move or delete a file, the client always calls this
	// AFTER it deletes or moves it.

	const auto desktop = connection->get_afp_session()->find_desk_file(afp_dt_refnum);
	if (desktop == nullptr)
	{
		return afpParmErr;
	}

	afp_error = AFPRemoveEntry(&search_entry, desktop);

	return afp_error;
}
