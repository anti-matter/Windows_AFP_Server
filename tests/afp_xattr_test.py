#!/usr/bin/env python3
"""Extended-attributes / alternate-data-stream regression test.

FPListExtAttrs must enumerate named NTFS streams: a file with a named
ADS ("xattrtest.txt:mynamedstream") has to come back with the stream
name - this guards the FILE_STREAM_INFORMATION parsing fix (correct
stream-name offset/length from the Win32 WIN32_FIND_STREAM_DATA info).

The fixture is created on the server via ssh when missing (needs the
AFP_SSH_* / AFP_SHARE_DIR environment, see README.md). Exit codes:
0 pass, 1 fail, 2 environment problem.
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import afpclient as afp  # noqa: E402

AFP_OBJECT_NOT_FOUND = -5018
FIXTURE = "xattrtest.txt"
STREAM = "mynamedstream"


def ensure_fixture():
    """Create file + named stream on the share; False if we can't."""
    remote = f"{afp.SHARE_DIR}\\{FIXTURE}"
    afp.ssh_guest(f'echo seed > "{remote}" & '
                  f'echo stream > "{remote}:{STREAM}" & '
                  f'icacls "{remote}" /grant Everyone:(R,W) >nul')
    # dir /r lists streams as "file:stream:$DATA"; plain "dir file:stream"
    # misleadingly reports File Not Found even when the stream exists.
    r = afp.ssh_guest(f'dir /r "{remote}"')
    return f":{STREAM}" in r.stdout


def main():
    if not ensure_fixture():
        print(f"ERROR: could not create fixture {FIXTURE}:{STREAM} via ssh "
              f"({afp.SSH_TARGET}:{afp.SSH_PORT}); set AFP_SSH_TARGET / "
              f"AFP_SSH_PORT / AFP_SHARE_DIR, see README.md")
        return 2

    d = afp.guest_session()
    try:
        vid = afp.open_volume(d)

        # FPListExtAttrs: pad, volID(2), dirID(4), bitmap(2)=0 names only,
        # reqCount(2)=0, startIndex(4)=0, maxReplySize(4), pathType(1), path
        req = (b"\x00" + struct.pack(">hIH", vid, 2, 0) + struct.pack(">H", 0)
               + struct.pack(">I", 0) + struct.pack(">I", 1024)
               + bytes([2]) + afp.pstr(FIXTURE) + b"\x00")
        req += afp.pad_even(len(req))
        e, rp = d.afp(afp.AFP_LIST_EXT_ATTRS, req)
        print(f"FPListExtAttrs {FIXTURE} -> err={e}")
        if e == AFP_OBJECT_NOT_FOUND:
            print("ERROR: fixture not found by the server (indexer lag? retry)")
            return 2
        if e != 0:
            print("FAIL: FPListExtAttrs errored")
            return 1

        # reply body: pad(2) + dataLen(4) + NUL-separated attribute names
        dl = struct.unpack(">I", rp[2:6])[0]
        names = [n.decode("mac_roman", errors="replace")
                 for n in rp[6:6 + dl].split(b"\x00") if n]
        print("attr names:", names)
        if STREAM not in names:
            print(f"FAIL: named stream '{STREAM}' missing from FPListExtAttrs")
            return 1
        print("PASS: named ADS stream enumerated via AFP")

        # FPCloseVol + logout, keep it tidy
        d.afp(afp.AFP_CLOSE_VOL, struct.pack(">Bh", 0, vid))
        d.afp(afp.AFP_LOGOUT)
        return 0
    finally:
        afp.ssh_guest(f'del "{afp.SHARE_DIR}\\{FIXTURE}" 2>nul')
        d.close()


if __name__ == "__main__":
    sys.exit(main())
