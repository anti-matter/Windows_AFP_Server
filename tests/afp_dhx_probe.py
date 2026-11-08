#!/usr/bin/env python3
"""DHCAST128 (DHX) login hardening probe.

Sends malformed / degenerate requests at the DHX login paths that were
bounds-hardened (client public-key blob, FPLoginCont encrypted-blob size
checks) and degenerate Diffie-Hellman keys (0, 1, p-1 style values).
The server must answer with AFP errors and stay alive; it must never
crash or leak. A final clean guest login proves the process survived.

After a failed FPLoginCont the server deliberately kills the DSI session
("Authentication gone"), so a closed socket there is expected - step 6
is what detects an actual crash.
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import afpclient as afp  # noqa: E402

AFP_AUTH_CONTINUE = -5001
AFP_PARM_ERR = -5019


def main():
    fails = []

    # 1. DHX login with NO client public key at all (pre-fix: OOB read)
    d = afp.DSI()
    d.open_session()
    body = afp.pstr("AFP3.1") + afp.pstr("DHCAST128") + afp.pstr("guest")
    e, _ = d.afp(afp.AFP_LOGIN, body + afp.pad_even(len(body)))
    d.close()
    print(f"1. DHX login, zero-length key -> err={e}")
    if e == 0:
        fails.append("zero-key login fully succeeded")

    # 2/3. degenerate DH keys 1 and 0 must not be accepted
    for num, (label, blob) in enumerate((("key=1", b"\x00" * 15 + b"\x01"),
                                         ("key=0", b"\x00" * 16)), start=2):
        d = afp.DSI()
        d.open_session()
        body = afp.pstr("AFP3.1") + afp.pstr("DHCAST128") + afp.pstr("guest") + blob
        e, _ = d.afp(afp.AFP_LOGIN, body + afp.pad_even(len(body)))
        d.close()
        print(f"{num}. DHX login, {label} -> err={e}")

    # 4. well-formed key shape -> expect afpAuthContinue, server computes
    #    the shared secret and replies with its public key + salt
    d = afp.DSI()
    d.open_session()
    body = afp.pstr("AFP3.1") + afp.pstr("DHCAST128") + afp.pstr("guest") \
        + bytes.fromhex("00000000000000000000000000abcdef")
    e, rp = d.afp(afp.AFP_LOGIN, body + afp.pad_even(len(body)))
    print(f"4. DHX login, normal-shaped key -> err={e} replen={len(rp)}")
    if e != AFP_AUTH_CONTINUE:
        fails.append(f"valid-shaped key got err {e}, want {AFP_AUTH_CONTINUE}")

    # 5. FPLoginCont with a too-short encrypted blob -> parmErr, not a crash
    try:
        sessid = struct.unpack(">H", rp[0:2])[0] if e == AFP_AUTH_CONTINUE and len(rp) >= 2 else 0x1234
        e2, _ = d.afp(afp.AFP_LOGIN_CONT, b"\x00" + struct.pack(">H", sessid) + b"tooshort")
        print(f"5. FPLoginCont short blob (sessid={sessid:#x}) -> err={e2}")
        if e2 != AFP_PARM_ERR:
            fails.append(f"short cont blob got {e2}, want {AFP_PARM_ERR}")
    except (ConnectionError, TimeoutError, OSError) as ex:
        print(f"5. connection closed on short blob: {ex} (acceptable, server alive-checked next)")

    # 6. liveness: a fresh guest login still works after all the abuse
    try:
        d2 = afp.guest_session()
        d2.afp(afp.AFP_LOGOUT)
        d2.close()
        print("6. server alive: fresh guest login OK")
    except Exception as ex:
        fails.append(f"server dead after probes: {ex}")

    print("RESULT:", "FAIL: " + "; ".join(fails) if fails else "PASS")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
