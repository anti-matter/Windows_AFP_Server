#pragma once

#include "afphelp.h"
#include "dsi/dsi_connection.h"

class afp_desktop;
struct afp_index;
class afp_volume;
class afp_open_file;

class afp_session  // NOLINT(cppcoreguidelines-special-member-functions)
{
public:
    explicit afp_session(dsi::dsi_connection* dsi_session);
	~afp_session();

	static afp_open_file* open_file(
		afp_session* session,
		INT16 vol_id,
		INT32 dir_id,
		INT16 access_mode,
		INT8 fork_to_open,
		afp_index* entry,
		const WCHAR* win_path
	);

	void associate_open_file(afp_open_file* open_file);
	AFPERROR close_file(INT16 file_reference);
	afp_open_file* find_open_file(INT16 file_reference);

	AFPERROR open_volume(INT16 volume_id);
	AFPERROR close_volume(INT16 volume_id);
	std::shared_ptr<afp_volume> find_open_volume(INT16 volume_id);
	BOOL session_has_volume_open(INT16 inVolumeID) { return find_open_volume(inVolumeID) != nullptr; }

	INT16 open_desk(INT16 volume_id);
	AFPERROR close_desk(INT16 desk_ref_num);
	afp_desktop* get_desktop(INT16 desk_ref_num);

    [[nodiscard]] UINT32 get_token() const { return m_token_; }

    [[nodiscard]] INT16 get_num_files_open() const { return static_cast<INT16>(m_afp_open_files_.size()); }
    [[nodiscard]] INT16 get_num_volumes_open() const { return static_cast<INT16>(m_open_volumes_.size()); }
    [[nodiscard]] INT16 get_num_desks_open() const { return static_cast<INT16>(m_open_desks_.size()); }

	void set_session_id(std::unique_ptr<char[]>& id, UINT32 id_size);
    [[nodiscard]] char* get_afp_id() const { return afp_id_.get(); }
    [[nodiscard]] UINT32 get_afp_id_size() const { return afp_id_size_; }
	void set_afp_time_stamp(UINT32 inTime) { m_afp_time_stamp_ = inTime; }
    [[nodiscard]] UINT32 get_afp_time_stamp() const { return m_afp_time_stamp_; }

    [[nodiscard]] dsi::dsi_connection* get_dsi_session() const { return m_dsi_session_; }

private:

	std::recursive_mutex m_mutex_;

    std::vector<std::unique_ptr<afp_open_file>> m_afp_open_files_;
	std::vector<std::shared_ptr<afp_volume>> m_open_volumes_;
	std::vector<INT16> m_open_desks_;

	// These are used so the AFP client can reference this session in
	// the event of a client crash or disconnection.
	std::unique_ptr<char[]> afp_id_;
	UINT32 afp_id_size_;

	UINT32 m_token_;
	UINT32 m_afp_time_stamp_;

	dsi::dsi_connection* m_dsi_session_;
};

