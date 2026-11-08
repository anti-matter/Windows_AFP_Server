#!/usr/bin/env python3
"""Account-type permission regression test.

Exercises the AFP permission model end-to-end for the three account
types a client can connect as - a standard Windows user, a Windows
administrator and the guest - covering the fixes for:

* folders created by an administrator must be owned by the creator,
  not BUILTIN\\Administrators (Finder showed "Administrators" and
  grayed out the whole Sharing panel);
* kAccess_Owner must be reported to the folder's owner and to
  administrators (owner-equivalent), and NOT to other users;
* the primary group of a local account (the machine-local group whose
  literal name is "None") must be reported as the built-in Users
  group, never as "None";
* FSetDirParms owner/group/access write-backs must actually apply to
  the Windows DACL and read back unchanged (they used to be stubs);
* the unsupported unix-privs bitmap bit must never be echoed;
* MapName (command 22) must round-trip with MapID;
* Windows must keep enforcing DACL writes: standard users may change
  security only on objects they own, guests never.

Fixtures are created on the share and removed afterwards. Requires a
standard-user and an administrator account on the server (defaults
match the dev guest setup; see README.md).

Exit codes: 0 pass, 1 fail, 2 environment problem.
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import afpclient as afp  # noqa: E402

AFP_CREATE_DIR = 6
AFP_MAP_ID = 21
AFP_MAP_NAME = 22
AFP_SET_FILE_DIR_PARMS = 29
AFP_GET_FILE_DIR_PARMS = 34

AFP_ACCESS_DENIED = -5000
AFP_ITEM_NOT_FOUND = -5012
AFP_OBJECT_EXISTS = -5017

STD_USER = os.environ.get("AFP_TEST_USER", "mac")
STD_PW = os.environ.get("AFP_TEST_PW", "AfpTest1")
ADM_USER = os.environ.get("AFP_TEST_ADMIN_USER", "afptestadmin")
ADM_PW = os.environ.get("AFP_TEST_ADMIN_PW", "AfpTest1")

DIR_STD = "apttest_std"
DIR_ADM = "apttest_adm"

# dirAccess rights layout (see afp.h kAccess_*):
# bits 0-2 owner, 8-10 group, 16-18 world, 24-26 "user actual", 31 Owner.
ACCESS_FULL = 0x87070707          # everything, owner flag set
ACCESS_NO_GROUP_WRITE = 0x87070307  # G* = read+execute only
ACCESS_OWNER_BIT = 0x80000000

failures = 0


def check(ok, label, detail=""):
    global failures
    print(f"  {'ok  ' if ok else 'FAIL'}: {label}" + (f"  [{detail}]" if detail else ""))
    if not ok:
        failures += 1


def login(d, user, pw):
    body = afp.pstr("AFP2.2") + afp.pstr("Cleartxt passwrd") + afp.pstr(user)
    body += afp.pad_even(1 + len(body))
    body += (pw.encode("mac_roman") + b"\0" * 8)[:8]
    return d.afp(afp.AFP_LOGIN, body)[0]


def sess(user=None, pw=None):
    """Connected session (guest when user is None) with volume opened."""
    d = afp.DSI()
    d.open_session()
    err = login(d, user, pw) if user else d.guest_login() or 0
    if err:
        raise RuntimeError(f"login as {user or 'guest'} failed: {err}")
    return d, afp.open_volume(d)


def create_dir(d, vid, name):
    req = (b"\x00" + struct.pack(">hI", vid, 2) + bytes([2])
           + afp.pstr(name) + b"\x00")
    req += afp.pad_even(1 + len(req))
    return d.afp(AFP_CREATE_DIR, req)[0]


def get_dir_info(d, vid, name, dir_bitmap=0x1C00):
    """Returns (err, ownerID, groupID, access, returned_dir_bitmap)."""
    req = (b"\x00" + struct.pack(">hI", vid, 2) + struct.pack(">HH", 0, dir_bitmap)
           + bytes([2]) + afp.pstr(name) + b"\x00")
    req += afp.pad_even(1 + len(req))
    err, rp = d.afp(AFP_GET_FILE_DIR_PARMS, req)
    if err or not rp:
        return err, None, None, None, None
    dbm = struct.unpack(">H", rp[2:4])[0]
    off = 6
    out = {}
    for bit in (0x0400, 0x0800, 0x1000):
        if dbm & bit:
            out[bit] = struct.unpack(">I", rp[off:off + 4])[0]
            off += 4
    return err, out.get(0x0400), out.get(0x0800), out.get(0x1000), dbm


def set_dir_parms(d, vid, name, bitmap, params):
    """FSetDirParms with a security-field bitmap and packed params."""
    req = (b"\x00" + struct.pack(">hI", vid, 2) + struct.pack(">H", bitmap)
           + bytes([2]) + afp.pstr(name))
    # The server does not consume a path terminating NUL here; the even
    # pad doubles as one (matching how shipped clients frame this call).
    req += afp.pad_even(1 + len(req))
    req += params
    return d.afp(AFP_SET_FILE_DIR_PARMS, req)[0]


def set_access(d, vid, name, mask):
    return set_dir_parms(d, vid, name, 0x1000, struct.pack(">I", mask))


def set_owner(d, vid, name, owner_id):
    return set_dir_parms(d, vid, name, 0x0400, struct.pack(">I", owner_id))


def map_id(d, kind, id_):
    """FPMapID: [subfunction][id] (no pad); reply is a pstring name."""
    err, rp = d.afp(AFP_MAP_ID, bytes([kind]) + struct.pack(">I", id_))
    if err or not rp:
        return err, None
    n = rp[0]
    return 0, rp[1:1 + n].decode("mac_roman", errors="replace")


def map_name(d, kind, name):
    """FPMapName (no pad byte after the command); reply is the UINT32 id."""
    payload = bytes([kind]) + afp.pstr(name)
    err, rp = d.afp(AFP_MAP_NAME, payload + afp.pad_even(1 + len(payload)))
    if err or not rp or len(rp) < 4:
        return err, None
    return 0, struct.unpack(">I", rp[0:4])[0]


def cleanup():
    for name in (DIR_STD, DIR_ADM, "apttest_guest"):
        afp.ssh_guest(f'rmdir /s /q "{afp.SHARE_DIR}\\{name}" 2>nul')


def main():
    # Environment sanity: both accounts must exist and log in.
    try:
        s, vid_s = sess(STD_USER, STD_PW)
        a, vid_a = sess(ADM_USER, ADM_PW)
        g, vid_g = sess()
    except Exception as exc:  # noqa: BLE001
        print(f"ERROR: {exc}")
        print(f"Set up accounts {STD_USER} (standard) and {ADM_USER} "
              f"(Administrators) on the server, or override AFP_TEST_USER[_PW] / "
              f"AFP_TEST_ADMIN_USER[_PW]; see README.md")
        return 2

    try:
        cleanup()  # stale fixtures from an interrupted run

        print(f"== fixture creation ({STD_USER} standard / {ADM_USER} admin) ==")
        e_std = create_dir(s, vid_s, DIR_STD)
        check(e_std in (0, AFP_OBJECT_EXISTS), f"{STD_USER} can create a folder",
              f"err={e_std}")
        e_adm = create_dir(a, vid_a, DIR_ADM)
        check(e_adm in (0, AFP_OBJECT_EXISTS), f"{ADM_USER} can create a folder",
              f"err={e_adm}")
        e_guest = create_dir(g, vid_g, "apttest_guest")
        check(e_guest == AFP_ACCESS_DENIED, "guest cannot create on the share",
              f"err={e_guest}")

        print("== ownership and primary group of new folders ==")
        for tag, d, vid, owner_name in (("std", s, vid_s, STD_USER),
                                        ("adm", a, vid_a, ADM_USER)):
            e, own, grp, _, _ = get_dir_info(d, vid, DIR_STD if tag == "std" else DIR_ADM)
            check(e == 0, f"{tag}: dir parms readable")
            if e:
                continue
            _, own_name = map_id(d, 1, own) if own else (0, None)
            check((own_name or "").lower() == owner_name.lower(),
                  f"{tag}: folder owner maps to its creator ({owner_name})",
                  f"got {own_name!r}")
            _, grp_name = map_id(d, 2, grp) if grp else (0, None)
            check(bool(grp_name) and grp_name.lower() != "none",
                  f"{tag}: primary group is not \"None\"", f"got {grp_name!r}")

        print("== kAccess_Owner visibility per account type ==")
        e, _, _, acc, _ = get_dir_info(s, vid_s, DIR_STD)
        check(e == 0 and acc & ACCESS_OWNER_BIT, f"{STD_USER} sees owner bit on own folder",
              f"acc={acc:#010x}")
        e, _, _, acc, _ = get_dir_info(a, vid_a, DIR_STD)
        check(e == 0 and acc & ACCESS_OWNER_BIT,
              f"{ADM_USER} sees owner-equivalent bit on {STD_USER}'s folder",
              f"acc={acc:#010x}")
        e, _, _, acc, _ = get_dir_info(s, vid_s, DIR_ADM)
        check(e == 0 and not acc & ACCESS_OWNER_BIT,
              f"{STD_USER} sees no owner bit on {ADM_USER}'s folder",
              f"acc={acc:#010x}")
        e, _, _, acc, _ = get_dir_info(g, vid_g, DIR_STD)
        check(e == 0 and not acc & ACCESS_OWNER_BIT,
              f"guest sees no owner bit on {STD_USER}'s folder",
              f"acc={acc:#010x}")

        print("== unix-privs bit must not be echoed ==")
        e, _, _, _, dbm = get_dir_info(s, vid_s, DIR_STD, dir_bitmap=0x9C00)
        check(e == 0 and not dbm & 0x8000, "kFPDirUnixPrivs cleared in reply bitmap",
              f"dbm={dbm:#06x}")

        print("== FSetDirParms access set / revoke roundtrip ==")
        e = set_access(s, vid_s, DIR_STD, ACCESS_FULL)
        check(e == 0, f"{STD_USER} sets full access on own folder", f"err={e}")
        e, _, _, acc, _ = get_dir_info(s, vid_s, DIR_STD)
        check(acc == ACCESS_FULL, "owner readback matches the set value",
              f"acc={acc and hex(acc)}")
        e, _, _, acc_g, _ = get_dir_info(g, vid_g, DIR_STD)
        check(acc_g == 0x07070700, "guest view shows world+group RWE",
              f"acc={acc_g and hex(acc_g)}")

        e = set_access(s, vid_s, DIR_STD, ACCESS_NO_GROUP_WRITE)
        check(e == 0, f"{STD_USER} revokes group write", f"err={e}")
        e, _, _, acc, _ = get_dir_info(s, vid_s, DIR_STD)
        check(acc == ACCESS_NO_GROUP_WRITE, "revoked group write reads back",
              f"acc={acc and hex(acc)}")
        e, _, _, acc_g, _ = get_dir_info(g, vid_g, DIR_STD)
        check(acc_g == 0x07070300, "guest view shows group without write",
              f"acc={acc_g and hex(acc_g)}")

        e = set_access(s, vid_s, DIR_STD, ACCESS_FULL)
        check(e == 0 and get_dir_info(s, vid_s, DIR_STD)[3] == ACCESS_FULL,
              "access restored to full")

        print("== cross-account DACL enforcement ==")
        e = set_access(s, vid_s, DIR_ADM, ACCESS_FULL)
        check(e == AFP_ACCESS_DENIED, f"{STD_USER} cannot change security of a foreign folder",
              f"err={e}")
        _, own_adm = map_name(s, 1, STD_USER)
        e = set_owner(s, vid_s, DIR_ADM, own_adm or 1)
        check(e == AFP_ACCESS_DENIED, f"{STD_USER} cannot take ownership of a foreign folder",
              f"err={e}")
        e = set_access(g, vid_g, DIR_STD, ACCESS_FULL)
        check(e == AFP_ACCESS_DENIED, "guest cannot change security", f"err={e}")
        e = set_access(a, vid_a, DIR_STD, ACCESS_FULL)
        check(e == 0, f"{ADM_USER} can change security of {STD_USER}'s folder",
              f"err={e}")

        print("== MapName / MapID roundtrip ==")
        e, id_std = map_name(s, 1, STD_USER)
        check(e == 0 and id_std is not None, f"MapName user {STD_USER}", f"err={e}")
        e, name_back = map_id(s, 1, id_std)
        check((name_back or "").lower() == STD_USER.lower(), "MapID maps it back",
              f"got {name_back!r}")
        e, id_adm = map_name(s, 1, ADM_USER.upper())  # spelling must not matter
        e2, name_back = map_id(s, 1, id_adm) if id_adm else (e, None)
        check(e == 0 and e2 == 0 and (name_back or "").lower() == ADM_USER.lower(),
              f"MapName is case-insensitive for {ADM_USER}")
        e, _ = map_name(s, 1, "no_such_account_zzz")
        check(e == AFP_ITEM_NOT_FOUND, "MapName rejects unknown accounts", f"err={e}")
        e, _ = map_name(s, 1, DIR_STD)  # a folder name is not an account
        check(e == AFP_ITEM_NOT_FOUND, "MapName rejects non-accounts", f"err={e}")
    finally:
        cleanup()
        for d, vid in [(s, vid_s), (a, vid_a), (g, vid_g)]:
            d.afp(afp.AFP_CLOSE_VOL, struct.pack(">Bh", 0, vid))
            d.afp(afp.AFP_LOGOUT)
            d.close()

    if failures:
        print(f"FAIL: {failures} permission check(s) failed")
        return 1
    print("PASS: AFP permission model verified for standard/admin/guest accounts")
    return 0


if __name__ == "__main__":
    sys.exit(main())
