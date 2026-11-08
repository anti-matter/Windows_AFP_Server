#!/usr/bin/env python3
"""End-to-end line-conversion test (fp-source/line-conversion).

For extensions on the configured conversion list (default .p .c .cp .cpp
.h .hpp .txt) the server converts line endings 1:1 between the AFP wire
(classic Mac CR) and disk (Unix LF). This test:

1. Seeds a file with LF content directly on the Windows share (scp).
2. FPReads it as AFP data -> bytes must come back as CR.
3. FPWrites CR content -> the file on disk must contain LF.
4. Repeats with an uppercase .TXT name: regression test for the old
   case-sensitive extension match that skipped README.TXT & friends.

Requires direct file access on the server (scp/ssh) to seed fixtures and
verify on-disk bytes - see README.md for the AFP_SSH_* / AFP_SHARE_DIR
environment variables. Exit codes: 0 pass, 1 fail, 2 environment problem.
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import afpclient as afp  # noqa: E402

MAC_TEXT = b"hello\rworld\rconv\r"   # what the Mac client sees on the wire
LF_TEXT = b"hello\nworld\nconv\n"    # what must live on disk

SEED_LOCAL = os.path.join("/tmp", "afp_lc_seed.txt")


def seed(name):
    """Upload LF content and open the ACL so the AFP guest can write."""
    with open(SEED_LOCAL, "wb") as f:
        f.write(LF_TEXT)
    remote = f"{afp.SHARE_DIR}\\{name}"
    afp.ssh_guest(f'del "{remote}" 2>nul')
    afp.scp_to_guest(SEED_LOCAL, f"{afp.SHARE_DIR}/{name}")
    afp.ssh_guest(f'icacls "{remote}" /grant Everyone:(R,W) >nul')
    time.sleep(3)  # let the change-notify indexer notice the new file
    if afp.guest_read_bytes(remote) != LF_TEXT:
        return None
    return remote


def check(name, fails):
    remote = seed(name)
    if remote is None:
        print(f"ERROR: could not seed {remote} via ssh "
              f"({afp.SSH_TARGET}:{afp.SSH_PORT}, share {afp.SHARE_DIR})")
        print("Set AFP_SSH_TARGET / AFP_SSH_PORT / AFP_SHARE_DIR, see README.md")
        sys.exit(2)

    d = afp.guest_session()
    try:
        vid = afp.open_volume(d)

        ref = afp.open_fork(d, vid, name, afp.K_READ_MODE)
        err, data = afp.read_fork(d, ref, 200)
        afp.close_fork(d, ref)
        ok = err == 0 and data == MAC_TEXT
        print(f"{name}: FPRead -> {data!r} {'OK (CR)' if ok else 'BAD'}")
        if not ok:
            fails.append(f"{name}: read should return CR text, got err={err} {data!r}")

        ref = afp.open_fork(d, vid, name, afp.K_READ_MODE | afp.K_WRITE_MODE)
        err, _ = afp.write_fork(d, ref, MAC_TEXT)
        afp.close_fork(d, ref)
        if err != 0:
            fails.append(f"{name}: FPWrite err {err}")
        else:
            on_disk = afp.guest_read_bytes(remote)
            ok = on_disk == LF_TEXT
            print(f"{name}: after AFP CR write, disk holds {on_disk!r} "
                  f"{'OK (LF)' if ok else 'BAD'}")
            if not ok:
                fails.append(f"{name}: disk should hold LF text, got {on_disk!r}")
    finally:
        afp.ssh_guest(f'del "{remote}" 2>nul')
        d.close()


def main():
    fails = []
    check("lc_test.txt", fails)        # lowercase: baseline
    check("UPPER_TEST.TXT", fails)     # uppercase ext: case-insensitive match
    print("RESULT:", "FAIL: " + "; ".join(fails) if fails else "PASS")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
