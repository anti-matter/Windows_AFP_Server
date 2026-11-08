#!/usr/bin/env python3
"""Volume-date epoch check (guards the 1904 Mac epoch fix).

All AFP dates are seconds since 1904-01-01. Before the fix the server
sent seconds-since-2000, which made every date land ~96 years in the
past. This test reads the volume create/modify dates via FPGetVolParms
(bitmap 0x000C) and asserts they decode (1904 epoch) to plausible modern
dates, not 1930s ones.
"""

import datetime
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import afpclient as afp  # noqa: E402

SANITY_MIN = datetime.datetime(2000, 1, 1, tzinfo=datetime.timezone.utc)  # pre-fix values decode to ~1930


def decode(mac_seconds):
    return datetime.datetime.fromtimestamp(mac_seconds - afp.MAC_EPOCH_TO_UNIX,
                                           datetime.timezone.utc)


def main():
    d = afp.guest_session()
    try:
        vid = afp.open_volume(d)
        # FPGetVolParms: pad, volID(2), bitmap(2) = create(0x4)|modify(0x8)
        e, rp = d.afp(afp.AFP_GET_VOL_PARMS, b"\x00" + struct.pack(">hH", vid, 0x000C))
        if e != 0:
            print(f"FAIL: FPGetVolParms err {e}")
            return 1

        # reply payload: bitmap(2) + attributes in bitmap order
        # (this server does not echo the command byte for this reply)
        if len(rp) < 10:
            print(f"FAIL: short FPGetVolParms reply ({len(rp)}B): {rp.hex()}")
            return 1
        _bitmap, created, modified = struct.unpack(">HII", rp[0:10])
        now = datetime.datetime.now(datetime.timezone.utc) + datetime.timedelta(days=1)
        fails = []
        for label, value in (("create", created), ("modify", modified)):
            dt = decode(value)
            print(f"{label}: raw={value} -> {dt}")
            if value == 0:
                fails.append(f"{label} date is 0 (AFP 'no date')")
            elif not (SANITY_MIN <= dt <= now):
                fails.append(f"{label} date {dt} implausible "
                             f"(wrong epoch? pre-fix this decoded to ~1930)")
        print("RESULT:", "FAIL: " + "; ".join(fails) if fails else "PASS")
        return 1 if fails else 0
    finally:
        d.afp(afp.AFP_CLOSE_VOL, struct.pack(">Bh", 0, vid))
        d.afp(afp.AFP_LOGOUT)
        d.close()


if __name__ == "__main__":
    sys.exit(main())
