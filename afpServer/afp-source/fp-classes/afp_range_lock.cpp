// ReSharper disable CppClangTidyClangDiagnosticGnuZeroVariadicMacroArguments
#include "StdAfx.h"
#include "afp_range_lock.h"

#include "afp_open_file.h"
#include "afp_file_index.h"

namespace
{
    // A single committed byte-range lock. start/end are inclusive byte
    // offsets into the fork.
    struct byte_range_lock
    {
        afp_session* session;
        const afp_open_file* file;
        // File identity shared by every session's open file for the same
        // object (the open files hold owning references, so the pointer
        // stays unique and stable for as long as any lock references it).
        const afp_index* entry;
        // kDataFork / kRsrcFork: the two forks of a file lock independently.
        INT8 fork;
        INT64 start;
        INT64 end;
    };

    std::mutex g_range_lock_mutex;
    std::vector<std::unique_ptr<byte_range_lock>> g_range_locks;

    bool ranges_overlap(const INT64 start_a, const INT64 end_a, const INT64 start_b, const INT64 end_b)
    {
        return start_a <= end_b && start_b <= end_a;
    }

    //
    // Inclusive end offset for a (non-negative, non-zero) length, clamped
    // so a client-supplied huge length cannot overflow the signed range.
    //
    INT64 saturating_range_end(const INT64 offset, const INT64 length)
    {
        if (length - 1 > INT64_MAX - offset)
        {
            return INT64_MAX;
        }

        return offset + length - 1;
    }
}

/*
 * afp_range_lock::lock()
 *
 * Description:
 *		Records a byte-range lock on behalf of the session through the
 *		given open fork. The range may overlap locks the SAME session
 *		already holds; overlap with another session's lock on the same
 *		file fork is rejected with afpRangeOverlap.
 *
 * Returns: AFPERROR
 */

AFPERROR afp_range_lock::lock(
    afp_session* session,
    const afp_open_file* file,
    const INT64 offset,
    const INT64 length)
{
    if (session == nullptr || file == nullptr)
    {
        return afpMiscErr;
    }

    if (offset < 0 || length <= 0)
    {
        DBGWRITE(dbg_level_info, L"Range lock rejected: offset=%lld length=%lld\n",
                 static_cast<long long>(offset), static_cast<long long>(length));
        return afpParmErr;
    }

    const INT64 range_end = saturating_range_end(offset, length);
    const auto entry = file->get_entry();
    const INT8 fork = file->get_fork();

    std::lock_guard<std::mutex> guard(g_range_lock_mutex);

    for (const auto& held : g_range_locks)
    {
        // A session never conflicts with itself.
        if (held->session == session)
        {
            continue;
        }

        // Different file object or different fork: independent ranges.
        if (held->entry != entry || held->fork != fork)
        {
            continue;
        }

        if (ranges_overlap(offset, range_end, held->start, held->end))
        {
            DBGWRITE(dbg_level_info,
                     L"Range lock conflict: [%lld-%lld] overlaps another session's lock [%lld-%lld]\n",
                     static_cast<long long>(offset), static_cast<long long>(range_end),
                     static_cast<long long>(held->start), static_cast<long long>(held->end));
            return afpRangeOverlap;
        }
    }

    auto new_lock = std::make_unique<byte_range_lock>();
    new_lock->session = session;
    new_lock->file = file;
    new_lock->entry = entry;
    new_lock->fork = fork;
    new_lock->start = offset;
    new_lock->end = range_end;

    g_range_locks.push_back(std::move(new_lock));

    DBGWRITE(dbg_level_info, L"Range locked: [%lld-%lld]\n",
             static_cast<long long>(offset), static_cast<long long>(range_end));

    return AFP_OK;
}


/*
 * afp_range_lock::unlock()
 *
 * Description:
 *		Releases the open fork's locks intersecting the given range.
 *		Locks only partially covered are shrunk; a lock carved in the
 *		middle is split in two.
 *
 * Returns: AFPERROR (afpRangeNotLocked when nothing intersected)
 */

AFPERROR afp_range_lock::unlock(
    afp_session* session,
    const afp_open_file* file,
    const INT64 offset,
    const INT64 length)
{
    if (session == nullptr || file == nullptr)
    {
        return afpMiscErr;
    }

    if (offset < 0 || length <= 0)
    {
        DBGWRITE(dbg_level_info, L"Range unlock rejected: offset=%lld length=%lld\n",
                 static_cast<long long>(offset), static_cast<long long>(length));
        return afpParmErr;
    }

    const INT64 range_end = saturating_range_end(offset, length);

    std::lock_guard<std::mutex> guard(g_range_lock_mutex);

    bool found = false;
    std::vector<std::unique_ptr<byte_range_lock>> additions;

    for (auto it = g_range_locks.begin(); it != g_range_locks.end();)
    {
        const byte_range_lock* held = it->get();

        // Unlock only releases locks taken through this same open fork.
        if (held->file != file || held->session != session)
        {
            ++it;
            continue;
        }

        if (!ranges_overlap(offset, range_end, held->start, held->end))
        {
            ++it;
            continue;
        }

        found = true;

        const INT64 held_start = held->start;
        const INT64 held_end = held->end;
        const INT8 fork = held->fork;
        const afp_index* entry = held->entry;

        // Fully covered: drop the lock entirely.
        if (offset <= held_start && range_end >= held_end)
        {
            it = g_range_locks.erase(it);
            continue;
        }

        // Unlock covers the left part of the lock: keep the right part.
        if (offset <= held_start)
        {
            auto keep = std::make_unique<byte_range_lock>();
            keep->session = session;
            keep->file = file;
            keep->entry = entry;
            keep->fork = fork;
            keep->start = range_end + 1;
            keep->end = held_end;
            additions.push_back(std::move(keep));

            it = g_range_locks.erase(it);
            continue;
        }

        // Unlock covers the right part of the lock: shrink to the left.
        if (range_end >= held_end)
        {
            auto keep = std::make_unique<byte_range_lock>();
            keep->session = session;
            keep->file = file;
            keep->entry = entry;
            keep->fork = fork;
            keep->start = held_start;
            keep->end = offset - 1;
            additions.push_back(std::move(keep));

            it = g_range_locks.erase(it);
            continue;
        }

        // Unlock carves the middle out of the lock: keep both halves.
        {
            auto left = std::make_unique<byte_range_lock>();
            left->session = session;
            left->file = file;
            left->entry = entry;
            left->fork = fork;
            left->start = held_start;
            left->end = offset - 1;
            additions.push_back(std::move(left));

            auto right = std::make_unique<byte_range_lock>();
            right->session = session;
            right->file = file;
            right->entry = entry;
            right->fork = fork;
            right->start = range_end + 1;
            right->end = held_end;
            additions.push_back(std::move(right));

            it = g_range_locks.erase(it);
            continue;
        }
    }

    // Merge the shrunk/split pieces back in after iteration (a push_back
    // inside the loop would invalidate the iterator).
    g_range_locks.insert(g_range_locks.end(),
                         std::make_move_iterator(additions.begin()),
                         std::make_move_iterator(additions.end()));

    if (!found)
    {
        DBGWRITE(dbg_level_info, L"Range unlock of an unlocked range: [%lld-%lld]\n",
                 static_cast<long long>(offset), static_cast<long long>(range_end));
        return afpRangeNotLocked;
    }

    DBGWRITE(dbg_level_info, L"Range unlocked: [%lld-%lld]\n",
             static_cast<long long>(offset), static_cast<long long>(range_end));

    return AFP_OK;
}


/*
 * afp_range_lock::release_locks_for()
 *
 * Description:
 *		Drops every lock taken through the given open fork. Invoked from
 *		the afp_open_file destructor, so FPCloseFork, session teardown
 *		and server shutdown all release the session's locks.
 *
 * Returns: (none)
 */

void afp_range_lock::release_locks_for(const afp_open_file* file)
{
    std::lock_guard<std::mutex> guard(g_range_lock_mutex);

    std::erase_if(g_range_locks, [file](const std::unique_ptr<byte_range_lock>& held)
    {
        return held->file == file;
    });
}
