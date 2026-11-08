#!/usr/bin/env python3
"""Mount-flow stress test: repeated full Finder-like mount sequences
(DSIOpenSession -> guest login -> FPGetVolList -> FPOpenVol/FPCloseVol
for every advertised volume -> FPLogOut). Reports per-round latency and
exits nonzero if any round fails or stalls.

Usage: afp_stress.py [rounds]   (host/port via AFP_HOST/AFP_PORT)
"""

import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import afpclient as afp  # noqa: E402


def mount_flow(round_no):
    t0 = time.time()
    d = afp.DSI()
    try:
        d.open_session()
        d.guest_login()
        err, vols = d.afp(afp.AFP_GET_VOL_LIST)
        if err != 0:
            return f"round {round_no}: FPGetVolList err {err}", False
        n = vols[4] if len(vols) > 4 else -1
        names = []
        i = 5
        for _ in range(max(n, 0)):
            l = vols[i + 1]
            names.append(vols[i + 2:i + 2 + l].decode("mac_roman"))
            i += 2 + l
        for vname in names:
            try:
                vid = afp.open_volume(d, vname)
                d.afp(afp.AFP_CLOSE_VOL, struct.pack(">Bh", 0, vid))
            except RuntimeError as ex:
                return f"round {round_no}: {ex}", False
        d.afp(afp.AFP_LOGOUT)
        return f"round {round_no}: OK vols={names} {(time.time() - t0) * 1000:.0f}ms", True
    finally:
        d.close()


def main():
    rounds = int(sys.argv[1]) if len(sys.argv) > 1 else 10
    fails = 0
    for r in range(rounds):
        try:
            msg, ok = mount_flow(r + 1)
        except Exception as ex:
            msg, ok = f"round {r + 1}: STALL/FAIL {type(ex).__name__}: {ex}", False
        fails += 0 if ok else 1
        print(msg, flush=True)
        time.sleep(0.3)
    print(f"done: {fails} failures of {rounds}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
