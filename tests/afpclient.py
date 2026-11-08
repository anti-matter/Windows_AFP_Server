#!/usr/bin/env python3
"""Minimal DSI / AFP client shared by the end-to-end tests in this folder.

Raw sockets on purpose: no external dependencies, and the tests can then
exercise malformed or unusual requests the way a real (or hostile) client
would. Wire-format notes, all verified against this server:

* DSI header (big-endian): flags(1) cmd(1) requestID(2) errorCode(4)
  dataSize(4) totalDataOffset(4). Replies with flags == 0 are unsolicited
  server requests (tickle / attention) and must be skipped.
* The AFP payload starts with the AFP command byte. Preamble handling is
  per command: e.g. FPOpenFork has NO pad byte after the command - the
  fork-type byte follows immediately (this server consumes the command
  byte itself, then reads fork).
* FPWrite carries its data in the same DSI payload: totalDataOffset
  points at the data, counted from the start of the AFP payload.
* FPRead returns the data as the entire AFP reply payload.
* kLongNames (path type 2) paths are NUL-terminated pstrings.

Common defaults come from environment variables (see README.md).
"""

import os
import socket
import struct
import subprocess

# --- DSI command codes ---
DSI_CMD_QUIT = 1
DSI_CMD_COMMAND = 2
DSI_CMD_OPEN_SESSION = 4

# --- AFP command codes used by the tests ---
AFP_CLOSE_VOL = 2
AFP_CLOSE_FORK = 4
AFP_LOGIN = 18
AFP_LOGIN_CONT = 19
AFP_LOGOUT = 20
AFP_GET_VOL_LIST = 16
AFP_GET_VOL_PARMS = 17
AFP_OPEN_VOL = 24
AFP_OPEN_FORK = 26
AFP_READ = 27
AFP_WRITE = 33
AFP_GET_EXT_ATTR = 69
AFP_LIST_EXT_ATTRS = 72

# --- AFP open modes (access bits in FPOpenFork's "mode" field) ---
K_READ_MODE = 1
K_WRITE_MODE = 2

# AFP dates are seconds since the Mac epoch (1904-01-01 UTC);
# 2_082_844_800 is the offset to the Unix epoch.
MAC_EPOCH_TO_UNIX = 2_082_844_800

DEFAULT_HOST = os.environ.get("AFP_HOST", "127.0.0.1")
DEFAULT_PORT = int(os.environ.get("AFP_PORT", "548"))
DEFAULT_VOLUME = os.environ.get("AFP_VOLUME", "Dev")
# SSH target/path of the exported share, used by tests that need to seed
# or inspect files directly on the Windows server (dev-guest workflow).
SSH_TARGET = os.environ.get("AFP_SSH_TARGET", "michaelc@127.0.0.1")
SSH_PORT = os.environ.get("AFP_SSH_PORT", "2222")
SHARE_DIR = os.environ.get("AFP_SHARE_DIR", r"D:\Shares\Dev")


def pstr(s: str) -> bytes:
    """Length-prefixed Mac Roman string."""
    b = s.encode("mac_roman")
    return bytes([len(b)]) + b


def pad_even(n: int) -> bytes:
    """One NUL byte when n is odd, so the AFP payload reaches even length."""
    return b"\x00" if n % 2 else b""


class DSI:
    """One DSI session over TCP."""

    def __init__(self, host=DEFAULT_HOST, port=DEFAULT_PORT, timeout=10):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.timeout = timeout
        self.request_id = 0

    def _recv_exact(self, n):
        buf = b""
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                raise ConnectionError("connection closed")
            buf += chunk
        return buf

    def expect(self, want_id):
        """Wait for the reply to want_id, skipping unsolicited requests."""
        self.sock.settimeout(self.timeout)
        while True:
            h = self._recv_exact(16)
            flags, _cmd, rid = struct.unpack(">BBH", h[:4])
            err, dlen = struct.unpack(">II", h[4:12])
            data = self._recv_exact(dlen) if dlen else b""
            if flags == 0:
                continue
            if rid != want_id:
                raise RuntimeError(f"reply id {rid} != {want_id}")
            if err >= 0x80000000:  # AFP errors are negative when signed
                err -= 1 << 32
            return err, data

    def call(self, dsi_cmd, payload=b"", data_offset=0):
        self.request_id += 1
        # DSI header: flags(1) cmd(1) requestID(2) dataOffset(4)* dataSize(4)
        # reserved(4).  *offset 4 is errorCode in replies; in requests it is
        # the DSI data offset (per Apple's DSI spec) - this server reads it
        # from here for every command, FPWrite included.
        hdr = struct.pack(">BBHIII", 0, dsi_cmd, self.request_id,
                          data_offset, len(payload), 0)
        self.sock.sendall(hdr + payload)
        return self.expect(self.request_id)

    def afp(self, afp_cmd, payload=b"", data_offset=0):
        return self.call(DSI_CMD_COMMAND, bytes([afp_cmd]) + payload, data_offset)

    def open_session(self):
        err, _ = self.call(DSI_CMD_OPEN_SESSION, bytes([1, 4]) + struct.pack(">I", 0x10000))
        if err != 0:
            raise RuntimeError(f"DSIOpenSession failed: {err}")

    def guest_login(self):
        """Clear-text login as the built-in guest (No User Authent)."""
        body = pstr("AFP2.2") + pstr("No User Authent")
        err, _ = self.afp(AFP_LOGIN, body + pad_even(1 + len(body)))
        if err != 0:
            raise RuntimeError(f"guest FPLogin failed: {err}")

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def guest_session(host=DEFAULT_HOST, port=DEFAULT_PORT):
    """Connected DSI session with DSIOpenSession + guest login completed."""
    d = DSI(host, port)
    try:
        d.open_session()
        d.guest_login()
    except Exception:
        d.close()
        raise
    return d


def open_volume(d, name=DEFAULT_VOLUME):
    """FPOpenVol with bitmap 0x0021 (attributes + volumeID); returns volume ID."""
    err, rp = d.afp(AFP_OPEN_VOL, b"\x00" + struct.pack(">H", 0x0021) + pstr(name))
    if err != 0:
        raise RuntimeError(f"FPOpenVol {name} failed: {err}")
    return struct.unpack(">h", rp[4:6])[0]


def open_fork(d, vid, name, access=K_READ_MODE, dir_id=2):
    """FPOpenFork on a volume-relative name in the root dir; returns fork ref."""
    req = bytes([0x00]) + struct.pack(">hIHHB", vid, dir_id, 0, access, 2) + pstr(name) + b"\x00"
    req += pad_even(1 + len(req))
    err, rp = d.afp(AFP_OPEN_FORK, req)
    if err != 0:
        raise RuntimeError(f"FPOpenFork {name} failed: {err}")
    return struct.unpack(">H", rp[2:4])[0]


def read_fork(d, ref, count, offset=0):
    """FPRead; returns (error, data) - the reply payload IS the data."""
    return d.afp(AFP_READ, b"\x00" + struct.pack(">hII", ref, offset, count))


def write_fork(d, ref, data, offset=0):
    """FPWrite; the data rides in the payload at data_offset from its start."""
    req = b"\x00" + struct.pack(">hII", ref, offset, len(data))
    doff = 1 + len(req)
    return d.afp(AFP_WRITE, req + data + pad_even(doff + len(data)), data_offset=doff)


def close_fork(d, ref):
    return d.afp(AFP_CLOSE_FORK, b"\x00" + struct.pack(">h", ref))


# ---------------------------------------------------------------------------
# Direct server-side file access (dev-guest workflow): seed fixtures and read
# raw bytes back so AFP-side conversions can be verified on disk.
# ---------------------------------------------------------------------------

def ssh_guest(cmd):
    """Run cmd.exe command line on the Windows server; returns CompletedProcess."""
    return subprocess.run(
        ["ssh", "-p", SSH_PORT, "-o", "BatchMode=yes", "-o", "ConnectTimeout=10",
         SSH_TARGET, cmd],
        capture_output=True, text=True, timeout=60)


def scp_to_guest(local_path, remote_path):
    subprocess.run(
        ["scp", "-P", SSH_PORT, "-o", "BatchMode=yes", local_path,
         f"{SSH_TARGET}:{remote_path}"],
        capture_output=True, timeout=60)


def guest_read_bytes(remote_path):
    """Raw bytes of a file on the Windows server, or None if unreadable."""
    r = ssh_guest(
        'powershell -NoProfile -Command '
        f'"[Convert]::ToBase64String([IO.File]::ReadAllBytes(\'{remote_path}\'))"')
    import base64
    try:
        return base64.b64decode("".join(r.stdout.split()))
    except Exception:
        return None
