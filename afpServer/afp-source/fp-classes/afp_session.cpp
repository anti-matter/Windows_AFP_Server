#include "StdAfx.h"
#include <algorithm>
#include "afp_session.h"
#include "auto_lock.h"
#include "afp_open_file.h"
#include "afp_volumes.h"
#include "dsi/dsi_connections.h"

extern std::unique_ptr<afp_volumes> g_volumes;

/*
 * afp_session()
 *
 * Description:
 *		Initializer for the AFPSession class. 
 *
 * Returns: None
 */

afp_session::afp_session(dsi::dsi_connection* dsi_session)
    : afp_id_(nullptr)
      , afp_id_size_(0)
      , m_token_(GetTickCount())
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

    std::ranges::for_each(m_open_desks_, [=, this](const std::unique_ptr<OPEN_DESKTOP>& dt)
    {
        close_desk(dt->dtRefNum);
    });

    std::ranges::for_each(m_afp_open_files_, [=, this](const std::unique_ptr<afp_open_file>& of)
    {
        if (of != nullptr)
        {
            close_file(of->get_file_ref_num());
        }
    });

    std::ranges::for_each(m_open_volumes_, [=, this](const std::shared_ptr<afp_volume>& ov)
    {
        close_volume(ov->get_volume_id());
    });

    LeaveCriticalSection(&m_mutex_);
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
        std::ranges::find_if(m_afp_open_files_, [=](const std::unique_ptr<afp_open_file>& f)
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
        std::ranges::find_if(m_afp_open_files_, [=](const std::unique_ptr<afp_open_file>& f)
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

afp_open_file* open_file(
    afp_session* session,
    const uint16 vol_id,
    const uint32 dir_id,
    const int16 access_mode,
    const int8 fork_to_open,
    afp_index* entry,
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
    const std::shared_ptr<afp_volume> volume = g_volumes->find(volume_id);

    if (volume == nullptr)
    {
        DBGWRITE(dbg_level_error, L"Bad volume ID\n");
        return afpParmErr;
    }

    m_open_volumes_.push_back(volume);

    DBGWRITE(dbg_level_all, L"Volume %s opened\n", volume->get_share_name().c_str());

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
        std::find_if(m_open_volumes_.begin(), m_open_volumes_.end(), [=](const afp_volume* v)
        {
	        const auto result = v->get_volume_id() == volume_id;
            return result;
        });

    if (it != m_open_volumes_.end())
    {
        DBGWRITE(dbg_level_all, L"Volume %s was closed\n", (*it)->get_share_name().c_str());

        m_open_volumes_.erase(it);
        return AFP_OK;
    }

    return afpParmErr;
}


/*
 * find_open_volume()
 *
 * Description:
 *		If this session has the volume open, it will return a pointer
 *		to the volume info block. 
 *
 * Returns: Ptr to the block or nullptr
 */

std::shared_ptr<afp_volume> afp_session::find_open_volume(const uint16 volume_id)
{
    auto_lock lock(&m_mutex_);

    const auto it =
        std::find_if(m_open_volumes_.begin(), m_open_volumes_.end(), [=](const afp_volume* v)
    {
        return v->get_volume_id() == volume_id;
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
        std::ranges::find_if(m_open_desks_, [=](const std::unique_ptr<OPEN_DESKTOP>& odt)
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
        std::ranges::find_if(m_open_desks_, [=](const std::unique_ptr<OPEN_DESKTOP>& odt)
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

void afp_session::set_session_id(std::unique_ptr<char[]>& id, const uint32 id_size)
{
    if (afp_id_ != nullptr)
    {
        afp_id_ = nullptr;
    }

    afp_id_ = std::move(id);
    afp_id_size_ = id_size;
}
