#pragma once

#include "afp.h"

class afp_session;
class afp_open_file;

//
// Server-side byte-range locking as implemented by FpByteRangeLock /
// FpByteRangeLockExt (modeled after the fp_rangelock class in MacFile for
// BeOS/Haiku, with partial-unlock shrink/split handling).
//
// Locks are tracked in server memory only, not with OS file locks: their
// job is to coordinate AFP clients against each other, and AFP I/O does
// not consult them (matching classic AppleShare behavior).
//
// A lock is owned by the open fork (afp_open_file) that took it: closing
// the fork, or the owning session going away, releases every lock taken
// through it (see afp_open_file's destructor). Overlap is checked against
// the locks of OTHER sessions on the same file fork; a session may freely
// overlap locks it already holds.
//
class afp_range_lock
{
public:
    //
    // Locks [offset, offset + length - 1] through the given open fork.
    // Returns AFP_OK, afpParmErr for a bad range, or afpRangeOverlap if
    // another session already locks part of the range.
    //
    static AFPERROR lock(
        afp_session* session,
        const afp_open_file* file,
        INT64 offset,
        INT64 length);

    //
    // Releases this open fork's locks that intersect
    // [offset, offset + length - 1]. Locks only partially covered by the
    // request are shrunk (or split in two when the request carves out
    // their middle). Returns afpRangeNotLocked when no lock of this fork
    // intersects the range.
    //
    static AFPERROR unlock(
        afp_session* session,
        const afp_open_file* file,
        INT64 offset,
        INT64 length);

    //
    // Releases every lock taken through the given open fork. Called from
    // the afp_open_file destructor, so fork close / session teardown /
    // server shutdown all drop the session's locks.
    //
    static void release_locks_for(const afp_open_file* file);
};
