#!/usr/bin/env python3
"""
dns-dbus-forwarder - plaintext UDP DNS on :53  ->  systemd-resolved via D-Bus.

Why this exists: the upstream network (Tele2-style ISP) blackholes plaintext
DNS (UDP/TCP 53) to every public resolver, kills DoT application data on 853,
and MITMs DoH endpoints by SNI (fake "SSL Corp" certificates, bogus rcodes).
The host itself still resolves fine because systemd-resolved's encrypted
session works, but Docker's embedded resolver and the WinBoat container's
dnsmasq only speak plaintext UDP upstream - so the Windows guest silently
loses DNS (browsers dead; SMB/AFP and IP-literal traffic unaffected).

This forwarder bridges that gap: it answers plaintext DNS on the host and
forwards each query to the *working* resolver through the system D-Bus
(io.systemd.resolve1 Manager.ResolveHostname) - the same API resolvectl
uses, bypassing resolved's raw UDP stub (which SERVFAILs here).

Run (on the WinBoat compose network with a pinned IP; the image must
contain busctl - build from winboat-deploy/):
    docker build -t afp-dns-dbus-fwd:latest - <<'EOF'
    FROM python:3.12-alpine
    RUN apk add --no-cache dbus busctl
    EOF
    docker run -d --name afp-dns-relay --restart unless-stopped \
        --network winboat_default --ip 172.18.0.250 \
        -v /run/dbus:/run/dbus:ro \
        -v "$HOME/afp-deploy/dns-dbus-forwarder.py:/fwd.py:ro" \
        afp-dns-dbus-fwd:latest python3 -u /fwd.py

Then set the WinBoat container's compose `dns:` to 172.18.0.250; its dnsmasq
(guest-facing, 172.30.0.1) forwards everything on through the embedded
resolver to this relay. (attach-usb-and-port.sh does all of this.)
"""
import json
import os
import socket
import socketserver
import subprocess

LISTEN = (os.environ.get("DNS_LISTEN", "0.0.0.0"),
          int(os.environ.get("DNS_PORT", "53")))
BUSCTL_TIMEOUT = 6

TYPES = {"A": 1, "CNAME": 5, "AAAA": 28}


def resolve(name: str, qtype: int):
    """Return (rcode, answers) where answers is a list of (rtype, rdata_bytes)."""
    try:
        out = subprocess.run(
            ["busctl", "--json=short", "--timeout", str(BUSCTL_TIMEOUT), "call",
             "org.freedesktop.resolve1", "/org/freedesktop/resolve1",
             "org.freedesktop.resolve1.Manager", "ResolveHostname",
             "isit", "0", name, "0", "0"],
            capture_output=True, text=True, timeout=BUSCTL_TIMEOUT + 2)
    except subprocess.TimeoutExpired:
        return 2, []                                    # SERVFAIL
    if out.returncode != 0:
        err = out.stderr or ""
        if "No appropriate name" in err or "does not exist" in err:
            return 3, []                                # NXDOMAIN
        return 2, []                                    # SERVFAIL
    try:
        data = json.loads(out.stdout)["data"]
        addrs = data[0] if data and isinstance(data[0], list) else []
        canon = data[1] if len(data) > 1 and isinstance(data[1], str) else name
    except (ValueError, KeyError, IndexError):
        return 2, []
    answers = []
    want6 = qtype == TYPES["AAAA"]
    for entry in addrs:
        _ifindex, family, addr_bytes = entry
        raw = bytes(addr_bytes)
        if (family == 10) != want6:                     # AF_INET6 == 10
            continue
        if qtype in (TYPES["A"], TYPES["AAAA"]):
            answers.append((TYPES["AAAA" if want6 else "A"], raw))
        elif qtype == TYPES["CNAME"]:
            pass                                        # fall through to A below
        else:
            answers.append((TYPES["AAAA" if family == 10 else "A"], raw))
    if qtype == TYPES["CNAME"]:
        answers = [(TYPES["CNAME"], encode_name(canon))]
    if not answers and qtype not in (TYPES["AAAA"],):
        # asked for something exotic (SOA/MX/...): NOERROR-empty is fine
        return 0, answers
    if not answers:
        return 3, []                                    # no records of that type
    return 0, answers


def encode_name(name: str) -> bytes:
    return b"".join(bytes([len(l)]) + l.encode("ascii", "ignore")
                    for l in name.strip(".").split(".")) + b"\x00"


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        data = self.request[0]
        if len(data) < 17:
            return
        # Parse the question (single-label queries only, which is all we get)
        i = 12
        labels_start = i
        while i < len(data) and data[i]:
            i += 1 + data[i]
        if i + 4 >= len(data):
            return
        qtype = int.from_bytes(data[i + 1:i + 3], "big")
        question = data[12:i + 5]
        try:
            name = data[labels_start:i].split(b"\x00")[0]
            j, dotted = 0, []
            while j < len(name):
                ln = name[j]
                dotted.append(name[j + 1:j + 1 + ln].decode("ascii", "ignore"))
                j += 1 + ln
            dotted = ".".join(dotted)
        except Exception:
            return
        rcode, answers = resolve(dotted, qtype)

        flags = 0x8180 | rcode                          # QR|RD, RA
        resp = data[:2] + flags.to_bytes(2, "big") \
            + (1).to_bytes(2, "big") + len(answers).to_bytes(2, "big") \
            + b"\x00\x00" * 2 + question
        for rtype, rdata in answers:
            resp += b"\xc0\x0c" + rtype.to_bytes(2, "big") + b"\x00\x01" \
                + b"\x00\x00\x00\x3c" + len(rdata).to_bytes(2, "big") + rdata
        self.request[1].sendto(resp, self.client_address)


class Server(socketserver.ThreadingUDPServer):
    allow_reuse_address = True
    daemon_threads = True


if __name__ == "__main__":
    print(f"dns-dbus-forwarder listening on {LISTEN[0]}:{LISTEN[1]}", flush=True)
    Server(LISTEN, Handler).serve_forever()
