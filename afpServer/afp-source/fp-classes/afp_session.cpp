#include "StdAfx.h"
#include "afp_session.h"
#include "auto_lock.h"
#include "afp_open_file.h"
#include <algorithm>

extern LPSESSION g_sessions_list;
extern CRITICAL_SECTION g_cs_session_list;


/*
 * afp_session()
 *
 * Description:
 *		Initializer for the AFPSession class. 
 *
 * Returns: None
 */

afp_session::afp_session(const LPSESSION dsi_session)
    : m_token_(GetTickCount())
      , afp_id_(nullptr)
      , afp_id_size_(0)
      , m_afp_time_stamp_(0)
      , m_dsi_session_(dsi_session)
{
    InitializeCriticalSection(&m_mutex_);
}


/*
 * ~afp_session()
 *
 * Description:
 *		Destructor for the AFPSession class. 
 *
 * Returns: None
 */

afp_session::~afp_session()
{
    EnterCriticalSection(&m_mutex_);

    std::for_each(m_open_desks_.begin(), m_open_desks_.end(), [=](const std::unique_ptr<OPEN_DESKTOP>& dt)
    {
        close_desk(dt->dtRefNum);
    });

    std::for_each(m_afp_open_files_.begin(), m_afp_open_files_.end(), [=](const std::unique_ptr<afp_open_file>& of)
    {
        close_file(of->get_file_ref_num());
    });

    std::for_each(m_open_volumes_.begin(), m_open_volumes_.end(), [=](const AFP_VOLUME_INFO* ov)
    {
        close_volume(ov->wVolID);
    });

    LeaveCriticalSection(&m_mutex_);

    delete [] afp_id_;
    DeleteCriticalSection(&m_mutex_);
}


/*
 * associate_open_file()
 *
 * Description:
 *		A file has been opened, keep track of it in this session. 
 *
 * Returns: None
 */

void afp_session::associate_open_file(afp_open_file* open_file)
{
    auto_lock lock(&m_mutex_);

    ASSERT(open_file != nullptr);
    DBGWRITE(dbg_level_all, L"[AFPSession:SessionAssociateAFPOpenFile]opening fileobject %x\n", open_file);

    std::unique_ptr<afp_open_file> AFPOpenFileObject(open_file);
    m_afp_open_files_.push_back(std::move(AFPOpenFileObject));
}


/*
 * close_file()
 *
 * Description:
 *		Close and open file. 
 *
 * Returns: None
 */

AFPERROR afp_session::close_file(const int16 file_reference)
{
    auto_lock lock(&m_mutex_);

    const auto it =
        std::find_if(m_afp_open_files_.begin(), m_afp_open_files_.end(), [=](const std::unique_ptr<afp_open_file>& f)
    {
        return f->get_file_ref_num() == file_reference;
    });

    if (it != m_afp_open_files_.end())
    {
        m_afp_open_files_.erase(it);
        return AFP_OK;
    }

    return afpParmErr;
}


/*
 * find_open_file()
 *
 * Description:
 *		Find an open file object within the global list tracking all
 *		open files. 
 *
 * Returns: The found file object or nullptr.
 */

afp_open_file* afp_session::find_open_file(const int16 file_reference)
{
    auto_lock lock(&m_mutex_);

    const auto it =
        std::find_if(m_afp_open_files_.begin(), m_afp_open_files_.end(), [=](const std::unique_ptr<afp_open_file>& f)
    {
        return f->get_file_ref_num() == file_reference;
    });

    if (it != m_afp_open_files_.end())
    {
        return it->get();
    }

    DBGWRITE(dbg_level_error, L"ERROR: Can't find open file!\n");

    return nullptr;
}


/*
 * open_file()
 *
 * Description:
 *		Create a new open file object. 
 *
 * Returns: The new file object or nullptr.
 */

afp_open_file* afp_session::open_file(
    afp_session* session,
    const uint16 vol_id,
    const uint32 dir_id,
    const int16 access_mode,
    const int8 fork_to_open,
    const p_afp_index entry,
    const WCHAR* win_path
)
{
	const auto new_file = new afp_open_file(
        vol_id,
        dir_id,
        access_mode,
        fork_to_open,
        entry,
        win_path
    );

    if (new_file == nullptr)
    {
        DBGWRITE(dbg_level_error, L"Failed to allocate new object!\n");
        return (nullptr);
    }

    session->associate_open_file(new_file);

    return new_file;
}


/*
 * open_volume()
 *
 * Description:
 *		Open a volume for access by this afp session. 
 *
 * Returns: AFP error code.
 */

AFPERROR afp_session::open_volume(const uint16 volume_id)
{
    auto_lock lock(&m_mutex_);
    AFP_VOLUME_INFO* volume_info;

    if (!AFPGetVolumeInfoBlock(nullptr, volume_id, &volume_info))
    {
        // We failed to find a block corresponding to the
        // supplied volume ID.

        DBGWRITE(dbg_level_error, L"Bad volume ID\n");
        return afpParmErr;
    }

    m_open_volumes_.push_back(volume_info);

    // Incrementing the ref count tells our routines that a
    // user has the volume open and is accessing it's contents.
    volume_info->dwRefCount++;

    DBGWRITE(dbg_level_all, L"Volume %s opened\n", volume_info->szShareName);

    return AFP_OK;
}


/*
 * close_volume()
 *
 * Description:
 *		Closes a volume from access. 
 *
 * Returns: AFP error code.
 */

AFPERROR afp_session::close_volume(const uint16 volume_id)
{
    auto_lock lock(&m_mutex_);

    const auto it =
        std::find_if(m_open_volumes_.begin(), m_open_volumes_.end(), [=](AFP_VOLUME_INFO* v)
        {
	        const auto result = v->wVolID == volume_id;
            if (result)
            {
                v->dwRefCount--;
            }

            return result;
        });

    if (it != m_open_volumes_.end())
    {
        DBGWRITE(dbg_level_all, L"[AFPSession::close_volume]Volume %s was closed\n", (*it)->szShareName);

        m_open_volumes_.erase(it);
        return AFP_OK;
    }

    return afpParmErr;
}


/*
 * find_open_volume_block()
 *
 * Description:
 *		If this session has the volume open, it will return a pointer
 *		to the volume info block. 
 *
 * Returns: Ptr to the block or nullptr
 */

AFP_VOLUME_INFO* afp_session::find_open_volume_block(const uint16 volume_id)
{
    auto_lock lock(&m_mutex_);

    const auto it =
        std::find_if(m_open_volumes_.begin(), m_open_volumes_.end(), [=](const AFP_VOLUME_INFO* v)
    {
        return v->wVolID == volume_id;
    });

    if (it != m_open_volumes_.end())
    {
        return *it;
    }

    return nullptr;
}


/*
 * open_desk()
 *
 * Description:
 *		Open a desktop database for access.
 *
 * Returns: AFP error code.
 */

AFPERROR afp_session::open_desk(const uint16 volume_id, uint16* desktop_ref_num)
{
    auto new_open_desk = std::make_unique<OPEN_DESKTOP>();
    const auto afp_error = AFPOpenDeskTopDB(volume_id, new_open_desk.get());

    if (AFP_SUCCESS(afp_error))
    {
        auto_lock lock(&m_mutex_);

        *desktop_ref_num = new_open_desk->dtRefNum;
        m_open_desks_.push_back(std::move(new_open_desk));
    }

    return afp_error;
}


/*
 * close_desk()
 *
 * Description:
 *		Close a desktop database from access.
 *
 * Returns: AFP error code.
 */

AFPERROR afp_session::close_desk(const uint16 desk_ref_num)
{
    auto_lock lock(&m_mutex_);

    const auto it =
        std::find_if(m_open_desks_.begin(), m_open_desks_.end(), [=](const std::unique_ptr<OPEN_DESKTOP>& odt)
        {
            return odt->dtRefNum == desk_ref_num;
        });

    if (it != m_open_desks_.end())
    {
        AFPCloseDeskTopDB(it->get());

        m_open_desks_.erase(it);
        return AFP_OK;
    }

    return afpParmErr;
}


/*
 * find_desk_file()
 *
 * Description:
 *		Returns a pointer to the OPEN_DESKTOP associated with the
 *		dtRefNum passed.
 *
 * Returns: AFP error code.
 */

OPEN_DESKTOP* afp_session::find_desk_file(const uint16 desk_ref_num)
{
    auto_lock lock(&m_mutex_);

    const auto it =
        std::find_if(m_open_desks_.begin(), m_open_desks_.end(), [=](const std::unique_ptr<OPEN_DESKTOP>& odt)
        {
            return odt->dtRefNum == desk_ref_num;
        });

    if (it != m_open_desks_.end())
    {
        return it->get();
    }

    return nullptr;
}


/*
 * set_session_id()
 *
 * Description:
 *		Sets the AFP ID for this session.
 *
 * Returns: none
 */

void afp_session::set_session_id(int8* id, const uint32 id_size)
{
	delete afp_id_;

	afp_id_ = id;
    afp_id_size_ = id_size;
}


/*
 * find_session_with_id()
 *
 * Description:
 *		Finds a session with the specified afp ID as set by the client.
 *
 * Returns: none
 */

afp_session* afp_session::find_session_with_id(const int8* afp_id, const uint32 id_size)
{
    auto session = g_sessions_list;

    auto_lock lock(&g_cs_session_list);

    while (session)
    {
	    const auto afp_object = static_cast<afp_session*>(session->afpSessionObject);

        if (afp_object != nullptr)
        {
	        const auto afp_object_id = afp_object->get_afp_id();

            if (afp_object_id != nullptr &&
                id_size == afp_object->get_afp_id_size() &&
                !memcmp(afp_id, afp_object_id, id_size))
            {
                return afp_object;
            }
        }

        session = static_cast<LPSESSION>(session->next);
    }

    return nullptr;
}


/*
 * find_session_by_token()
 *
 * Description:
 *		Finds a session with the specified afp token.
 *
 * Returns: none
 */

afp_session* afp_session::find_session_by_token(const int32 token)
{
    auto session = g_sessions_list;

    auto_lock lock(&g_cs_session_list);

    while (session)
    {
	    const auto session_object = static_cast<afp_session*>(session->afpSessionObject);

        if (session_object != nullptr)
        {
            if (session_object->get_token() == static_cast<uint32>(token))
            {
                return session_object;
            }
        }

        session = static_cast<LPSESSION>(session->next);
    }

    return nullptr;
}
