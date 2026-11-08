#!/usr/bin/env python3
"""Colon-in-name regression test.

':' is the classic HFS/AFP path separator and is illegal in Macintosh
names, but a client can still put one on the wire (malformed or
non-standard clients). Raw, the character is dangerous on NTFS: a name
containing ':' that gets joined onto a folder addresses an ALTERNATE
DATA STREAM, so FPCreateFile "parent:child" used to silently create a
0-byte stream ON the parent folder - a ghost file invisible to both
Mac and Windows.

The server now does what Finder does when the user types ':' in a
name: it substitutes a dash. ':' is never a directory separator, so
it can never redirect an operation into another folder, to a rooted
drive path, or into a stream. Checks on the live server:

* FPCreateFile with a colon creates a plain file named with a dash,
  and leaves no ':name' alternate data stream on a same-named folder;
* colon names round-trip through FPOpenFork/Fread/Fwrite (the dashed
  name IS the name);
* colon paths do NOT resolve as directory paths (no traversal);
* rooted/UNC/parent-walk attempts through ':' stay inert and inside
  the share;
* enumeration shows the dashed name (no char-map restore of ':').

Fixtures are created on the share and removed afterwards.
Uses the standard test account (see afp_permissions_test.py).

Exit codes: 0 pass, 1 fail, 2 environment problem.
"""

import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import afpclient as afp             # noqa: E402
import afp_permissions_test as apt  # noqa: E402

AFP_CREATE_DIR = 6
AFP_CREATE_FILE = 7
AFP_ENUMERATE = 9
AFP_GET_FILE_DIR_PARMS = 34

DIR = "aptcolon"

failures = 0


def check(ok, label, detail=""):
    global failures
    print(f"  {'ok  ' if ok else 'FAIL'}: {label}" + (f"  [{detail}]" if detail else ""))
    if not ok:
        failures += 1


def parms(d, vid, name, parent=2, bitmap=0x0100):
    """FPGetFileDirParms; returns (err, dir_id_or_None)."""
    req = (b"\x00" + struct.pack(">hI", vid, parent) + struct.pack(">HH", 0, bitmap)
           + bytes([2]) + afp.pstr(name) + b"\x00")
    req += afp.pad_even(1 + len(req))
    err, rp = d.afp(AFP_GET_FILE_DIR_PARMS, req)
    if err or not rp:
        return err, None
    # A file reply carries the file bitmap + attributes, not a DirID.
    dir_id = struct.unpack(">I", rp[6:10])[0] if len(rp) >= 10 else None
    return 0, dir_id


def create_file(d, vid, name, parent=2):
    req = (b"\x00" + struct.pack(">hI", vid, parent) + bytes([2])
           + afp.pstr(name) + b"\x00")
    req += afp.pad_even(1 + len(req))
    return d.afp(AFP_CREATE_FILE, req)[0]


def open_fork(d, vid, name, access, parent=2):
    req = (b"\x00" + struct.pack(">hIHHB", vid, parent, 0, access, 2)
           + afp.pstr(name) + b"\x00")
    req += afp.pad_even(1 + len(req))
    err, rp = d.afp(afp.AFP_OPEN_FORK, req)
    if err or not rp:
        return err, None
    return 0, struct.unpack(">H", rp[2:4])[0]


def guest_cmd(cmd):
    """Run a command in the guest as its (admin) console user, for
    filesystem truth checks the AFP protocol cannot express."""
    return afp.ssh_guest(cmd).stdout or ""


def main():
    try:
        d, vid = apt.sess(apt.STD_USER, apt.STD_PW)
    except (RuntimeError, OSError) as exc:
        print(f"environment problem: {exc}")
        return 2

    share_root = os.environ.get("AFP_WIN_PATH", r"D:\Shares\Dev")

    print("== setup ==")
    guest_cmd(f'rmdir /s /q "{share_root}\\{DIR}" 2>nul')
    guest_cmd(f'del /q "{share_root}\\{DIR}-"* 2>nul')
    time.sleep(2)  # let the change notifier settle
    err = apt.create_dir(d, vid, DIR)
    check(err == 0, "create base dir", f"err={err}")

    print("== a colon in a name becomes a dash, never a stream ==")
    # "aptcolon:ghost" must NOT become a stream on the aptcolon folder;
    # it must land as the plain file "aptcolon-ghost".
    err = create_file(d, vid, f"{DIR}:ghost")
    check(err == 0, "FPCreateFile 'aptcolon:ghost'", f"err={err}")
    out = guest_cmd(f'dir /b "{share_root}" 2>nul | findstr /i "aptcolon-"')
    check("aptcolon-ghost" in out, "created as plain file 'aptcolon-ghost'",
          out.strip().replace("\r", " | "))
    out = guest_cmd(f'dir /r "{share_root}\\{DIR}" 2>nul | findstr /i "ghost:"')
    check("ghost:" not in out, "no ':ghost' alternate data stream on the folder",
          out.strip().replace("\r", " | "))

    print("== the dashed name is the name (round-trip) ==")
    err = create_file(d, vid, "w:ird.txt")
    check(err == 0, "create 'w:ird.txt'", f"err={err}")
    err, _ = parms(d, vid, "w-ird.txt")
    check(err == 0, "'w-ird.txt' resolves", f"err={err}")
    err, ref = open_fork(d, vid, "w:ird.txt", afp.K_WRITE_MODE)
    check(err == 0, "FPOpenFork WRITE via 'w:ird.txt' hits the same file", f"err={err}")
    if ref is not None:
        we, _ = afp.write_fork(d, ref, b"dash works\r")
        check(we == 0, "write through colon name", f"err={we}")
        afp.close_fork(d, ref)
    err, ref = open_fork(d, vid, "w-ird.txt", afp.K_READ_MODE)
    check(err == 0, "FPOpenFork READ via 'w-ird.txt'", f"err={err}")
    if ref is not None:
        err, data = afp.read_fork(d, ref, 64)
        check(err == 0 and b"dash works" in data, "content round-trip", f"err={err} data={data!r}")
        afp.close_fork(d, ref)

    print("== colon is NOT a directory separator ==")
    afp.ssh_guest(f'mkdir "{share_root}\\{DIR}\\sub" 2>nul')
    time.sleep(2)
    err, _ = parms(d, vid, f"{DIR}:sub")
    check(err != 0, f"'{DIR}:sub' does not resolve as a nested path", f"err={err}")
    err, _ = parms(d, vid, "sub", parent=2)
    check(err != 0, "(control: 'sub' at the root was never created)", f"err={err}")

    print("== rooted / UNC / parent-walk attempts through ':' stay inert ==")
    for probe in ("C:Windows\\win.ini", "::evil", "..:..:Windows", ":absolute"):
        err, _ = parms(d, vid, probe)
        check(err != 0, f"'{probe}' not found", f"err={err}")
    err = create_file(d, vid, "..:aptcolon_escape.txt")
    # The '..' pair is mapped by the existing illegal-name rules; either
    # way nothing may appear outside the share.
    out = guest_cmd("if exist D:\\Shares\\aptcolon_escape.txt (echo LEAK) else (echo no-leak)")
    check("no-leak" in out, "nothing created outside the share",
          f"create err={err} {out.strip()}")

    print("== enumeration shows the dashed name ==")
    req = (b"\x00" + struct.pack(">hI", vid, 2)
           + struct.pack(">HHHHH", 0x0040, 0x0040, 50, 0, 4000) + bytes([2]) + b"\x00")
    req += afp.pad_even(1 + len(req))
    err, rp = d.afp(AFP_ENUMERATE, req)
    check(err == 0 and b"w-ird.txt" in rp and b"w:ird.txt" not in rp,
          "volume enumerates 'w-ird.txt' (no ':' restore)", f"err={err}")

    print("== cleanup ==")
    guest_cmd(f'del /q "{share_root}\\w-ird.txt" "{share_root}\\aptcolon-ghost" 2>nul')
    guest_cmd(f'rmdir /s /q "{share_root}\\{DIR}" 2>nul')
    # The '..:escape' create lands as a char-mapped name; wildcards reach it.
    guest_cmd('powershell -NoProfile -Command "Get-ChildItem -LiteralPath '
              f'{share_root} | Where-Object Name -like *aptcolon_escape* | Remove-Item -Force"')

    print("FAILURES:", failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
