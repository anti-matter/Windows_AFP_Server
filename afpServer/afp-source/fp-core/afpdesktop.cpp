// ReSharper disable CppClangTidyClangDiagnosticGnuZeroVariadicMacroArguments
// ReSharper disable CppParameterMayBeConstPtrOrRef
#include "StdAfx.h"
#include "afp.h"
#include "afpdesktop.h"
#include "afpdirectory.h"
#include "afp_desktop.h"
#include "afp_session.h"
#include "afp_volumes.h"
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
	//
	//First WORD is afp command and padding byte.
	//
	afp_request.advance_pos_ptr(sizeof(int16));

	const auto afp_volume_id = afp_request.pull_num<UINT16>();
	const auto afp_dt_ref_num = connection->get_afp_session()->open_desk(afp_volume_id);

	if (afp_dt_ref_num == 0)
	{
		return afpParmErr;
	}

	afp_reply.push_num<UINT16>(afp_dt_ref_num);

	return AFP_OK;
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

	// The first work contains the command and padding byte.
	afp_request.advance_pos_ptr(sizeof(int16));

	const auto afp_dt_ref_num = afp_request.pull_num<UINT16>();

	afp_desk_item icon_entry;

	// Grab the file type and creator that will be associated with
	// this icon. Note that in MacOS X, they appear to be doing away
	// from creators and types.
	icon_entry.file_creator = afp_request.pull_num<UINT32>();
	icon_entry.file_type = afp_request.pull_num<UINT32>();

	// Next bytes contains the icon type we'll be storing. We don't
	// care what it is, its for the client to know.
    // ReSharper disable once CppAssignedValueIsNeverUsed
    icon_entry.icon_type = afp_request.pull_num<INT8>();

	// A padding byte, skip it.
	afp_request.advance_pos_ptr(sizeof(int8));

	// Get the next 2 parameters; We don't know or care what the tag is and
	// we get the size of the data we'll be storing.
	icon_entry.tag = afp_request.pull_num<UINT32>();
	icon_entry.data_size = afp_request.pull_num<UINT16>();
	icon_entry.icon_type = entry_type_icon;

	// Advance in the buffer to the offset where the icon data is
	// being stored.

	if (icon_entry.data_size > max_desktop_data_size)
	{
		// Nothing to do here but bail out!
		return afpParmErr;
	}

	// Now copy the actual icon data into our buffer to store.
	afp_request.pull_raw_data(icon_entry.data, icon_entry.data_size);

	// Now call the desktop routine to do the rest of the work.
	const auto desktop = connection->get_afp_session()->get_desktop(afp_dt_ref_num);
	if (desktop == nullptr)
	{
		return afpParmErr;
	}

    desktop->add(&icon_entry);

    return AFP_OK;
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
	afp_desk_item search_entry;

	afp_request.advance_pos_ptr(sizeof(UINT16));

	const auto afp_dt_ref_num = afp_request.pull_num<UINT16>();

	search_entry.file_creator = afp_request.pull_num<UINT32>();
	search_entry.file_type = afp_request.pull_num<UINT32>();
	search_entry.icon_type = afp_request.pull_num<INT8>();

	afp_request.advance_pos_ptr(sizeof(INT8));

	search_entry.data_size = afp_request.pull_num<UINT16>();

	const auto desktop = connection->get_afp_session()->get_desktop(afp_dt_ref_num);
	if (desktop == nullptr)
	{
		return afpParmErr;
	}

    const auto icon_entry = desktop->find(&search_entry);
	if (icon_entry == nullptr)
	{
		return afpItemNotFound;
	}

    // We found the bitmap and the caller actually has memory
    // reserved for the bitmap.

    // As per the spec, even if the client didn't reserve enough
    // space, we still copy what we can.
    const auto afp_copy_size = min(search_entry.data_size, icon_entry->data_size);
    afp_reply.push_raw_data(icon_entry->data, afp_copy_size);

    return AFP_OK;
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
	afp_desk_item search_entry;

	afp_request.advance_pos_ptr(sizeof(UINT16));
	const auto afp_dt_ref_num = afp_request.pull_num<UINT16>();

	search_entry.file_creator = afp_request.pull_num<UINT32>();
	const auto afp_icon_index = afp_request.pull_num<UINT16>();
	search_entry.item_type = entry_type_icon;

	const auto desktop = connection->get_afp_session()->get_desktop(afp_dt_ref_num);
	if (desktop == nullptr)
	{
		return afpParmErr;
	}

	const auto found_entry = desktop->find(&search_entry, afp_icon_index);
	if (found_entry == nullptr)
	{
		return afpItemNotFound;
	}

    afp_reply.push_num(found_entry->tag);
    afp_reply.push_raw_data(&found_entry->file_type, sizeof(FILE_TYPE));
    afp_reply.push_num(found_entry->icon_type);
    afp_reply.push_num<INT8>(0);
    afp_reply.push_num(found_entry->data_size);

    return AFP_OK;
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

	afp_desk_item desk_item;

	DBGWRITE(dbg_level_all, L"Enter\n");

	afp_request.advance_pos_ptr(sizeof(int16));

	const auto afp_dt_ref_num = afp_request.pull_num<UINT16>();
	desk_item.dir_id = afp_request.pull_num<UINT32>();
	desk_item.path_type = afp_request.pull_num<INT8>();

    const auto afp_error = afp_request.get_pascal_string(
        desk_item.path_name,
        sizeof(desk_item.path_name), 
        true,
        desk_item.path_type,
        nullptr);

	if (afp_error != AFP_OK)
	{
		return (afpParmErr);
	}

	afp_request.advance_if_odd();
	afp_request.get_pascal_string(desk_item.data, sizeof(desk_item.data));

	desk_item.item_type = entry_type_comment;
	desk_item.data_size = static_cast<UINT16>(strlen(desk_item.data));

	const auto desktop = connection->get_afp_session()->get_desktop(afp_dt_ref_num);
	if (desktop == nullptr)
	{
		return afpParmErr;
	}

	const auto volume = connection->get_afp_session()->find_open_volume(desktop->get_volume_id());
	if (volume == nullptr)
	{
		return afpParmErr;
	}

    const auto file_entry = volume->get_index()->find(desk_item.dir_id, desk_item.path_name);
	if (file_entry == nullptr)
	{
		return afpObjectNotFound;
	}

	if (GetFileAttributes(file_entry->sz_path.c_str()) == AFP_MAX_DWORD)
	{
		DBGWRITE(dbg_level_error, L"Path not found: %s\n", file_entry->sz_path.c_str());
		return afpObjectNotFound;
	}

	const auto wide_name = string_to_wide_string(desk_item.data);
	DBGWRITE(dbg_level_info, L"Adding comment, pathname=%s, comment=%s\n", desk_item.path_name, wide_name.c_str());

	desktop->add(&desk_item);

    return AFP_OK;
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
	afp_desk_item search_entry;

	DBGWRITE(dbg_level_all, L"Enter\n");

	afp_request.advance_pos_ptr(sizeof(int16));

	const auto afp_dt_refnum = afp_request.pull_num<UINT16>();
	search_entry.dir_id = afp_request.pull_num<UINT32>();
	search_entry.path_type = afp_request.pull_num<INT8>();
	search_entry.item_type = entry_type_comment;

    if (AFP_FAILED(afp_request.get_pascal_string(search_entry.path_name, sizeof(search_entry.path_name), true,
                                                             search_entry.path_type, nullptr)))
	{
		return afpParmErr;
	}

	const auto desktop = connection->get_afp_session()->get_desktop(afp_dt_refnum);
	if (desktop == nullptr)
	{
		return afpParmErr;
	}

	const auto volume = connection->get_afp_session()->find_open_volume(desktop->get_volume_id());
	if (volume == nullptr)
	{
		return afpParmErr;
	}

	const auto afp_entry = volume->get_index()->find(search_entry.dir_id, search_entry.path_name);
	if (afp_entry == nullptr)
	{
		return afpObjectNotFound;
	}

	if (GetFileAttributes(afp_entry->sz_path.c_str()) == AFP_MAX_DWORD)
	{
		DBGWRITE(dbg_level_error, L"Path not found: %s\n", afp_entry->sz_path.c_str());
		return afpItemNotFound;
	}

	const auto desk_item = desktop->find(&search_entry);
    if (desk_item == nullptr)
    {
		return afpItemNotFound;
    }

    afp_reply.push_pascal_string(desk_item->data);

    return AFP_OK;
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

	afp_desk_item search_entry;

	afp_request.advance_pos_ptr(sizeof(int16));

	const auto afp_dt_refnum = afp_request.pull_num<UINT16>();
	search_entry.dir_id = afp_request.pull_num<UINT32>();
	search_entry.path_type = afp_request.pull_num<INT8>();
	search_entry.item_type = entry_type_icon;

	if (AFP_FAILED(afp_request.get_pascal_string(search_entry.path_name, sizeof(search_entry.path_name), true,
		search_entry.path_type, nullptr)))
	{
		return afpParmErr;
	}

	const auto desktop = connection->get_afp_session()->get_desktop(afp_dt_refnum);
	if (desktop == nullptr)
	{
		return afpParmErr;
	}

	const auto volume = connection->get_afp_session()->find_open_volume(desktop->get_volume_id());
	if (volume == nullptr)
	{
		return afpParmErr;
	}

	return desktop->remove(&search_entry) ? AFP_OK : afpItemNotFound;
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
	afp_desk_item search_entry;

	DBGWRITE(dbg_level_all, L"Enter\n");

	afp_request.advance_pos_ptr(sizeof(int16));
	const auto afp_dt_refnum = afp_request.pull_num<UINT16>();

	afp_request.pull_raw_data(&search_entry.file_creator, sizeof(FILE_CREATOR));

	const auto item_index = afp_request.pull_num<UINT16>();
	const auto afp_bitmap = afp_request.pull_num<UINT16>();

	search_entry.item_type = entry_type_appl;

	const auto desktop = connection->get_afp_session()->get_desktop(afp_dt_refnum);
	if (desktop == nullptr)
	{
		return afpParmErr;
	}

	const auto desk_item = desktop->find(&search_entry, item_index);
	if (desk_item == nullptr)
	{
		return afpItemNotFound;
	}

	const auto volume = connection->get_afp_session()->find_open_volume(desktop->get_volume_id());
	if (volume == nullptr)
	{
		return afpParmErr;
	}

	const auto file_item = volume->get_index()->find(desk_item->dir_id, desk_item->path_name);
	if (file_item == nullptr)
	{
		return afpObjectNotFound;
	}

    if (!GetFileAttributesEx(file_item->sz_path.c_str(), GetFileExInfoStandard, &winAttributes))
    {
        return afpObjectNotFound;
    }

    afp_reply.push_num(afp_bitmap);
    afp_reply.push_num(desk_item->tag);

	DBGWRITE(dbg_level_all, L"Found APPL entry for: %s\n", file_item->sz_path.c_str());

    const auto afp_error = AFPGetObjectParms(
        afp_reply,
        afp_bitmap,
        desktop->get_volume_id(),
        desk_item->dir_id,
        connection,
		file_item,
        winAttributes
    );

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

	afp_desk_item new_entry;

	afp_request.advance_pos_ptr(sizeof(int16));

	const auto afp_dt_refnum = afp_request.pull_num<UINT16>();

	new_entry.dir_id = afp_request.pull_num<UINT32>();
	new_entry.file_creator = afp_request.pull_num<UINT32>();
	new_entry.tag = afp_request.pull_num<UINT32>();
	new_entry.path_type = afp_request.pull_num<INT8>();
	new_entry.item_type = entry_type_appl;
	new_entry.data_size = 0;

	if (AFP_FAILED(afp_request.get_pascal_string(new_entry.path_name, sizeof(new_entry.path_name), true, new_entry.path_type,
		nullptr)))
	{
		return afpParmErr;
	}

	const auto desktop = connection->get_afp_session()->get_desktop(afp_dt_refnum);
	if (desktop == nullptr)
	{
		return afpParmErr;
	}

	desktop->add(&new_entry);

	return AFP_OK;
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

	afp_desk_item search_entry;

	afp_request.advance_pos_ptr(sizeof(int16));

	const auto afp_dt_refnum = afp_request.pull_num<UINT16>();
	search_entry.dir_id = afp_request.pull_num<UINT32>();
	search_entry.file_creator = afp_request.pull_num<UINT32>();
	search_entry.path_type = afp_request.pull_num<INT8>();
	search_entry.item_type = entry_type_appl;

	if (AFP_FAILED(afp_request.get_pascal_string(search_entry.path_name, sizeof(search_entry.path_name), true,
		search_entry.path_type, nullptr)))
	{
		return afpParmErr;
	}

	// Although the spec says to, we don't check to make sure that
	// the actual application really exists on the volume. Why? Because
	// when you move or delete a file, the client always calls this
	// AFTER it deletes or moves it.

	const auto desktop = connection->get_afp_session()->get_desktop(afp_dt_refnum);
	if (desktop == nullptr)
	{
		return afpParmErr;
	}

	if (!desktop->remove(&search_entry))
	{
		return afpItemNotFound;
	}

	return AFP_OK;
}
