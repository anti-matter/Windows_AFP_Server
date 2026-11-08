# AFP end-to-end tests

Python 3 (stdlib only — no pip installs) clients that talk DSI/AFP directly
to a **running server** and verify protocol behavior end-to-end. They are
regression tests for fixes that unit tests can't reach; see each file's
docstring for what it guards.

## Prerequisites

- An AFP server reachable over TCP (default `127.0.0.1:548`) with guest
  access enabled (built-in *No User Authent* login) and a writable share.
- The dev workflow targets the Windows dev guest from
  [`winboat-deploy/README.md`](../winboat-deploy/README.md); the tests that
  seed/inspect files on disk do so over SSH into that guest.

## Configuration (environment variables)

| Variable | Default | Meaning |
|---|---|---|
| `AFP_HOST` | `127.0.0.1` | AFP server address (DSI, TCP 548) |
| `AFP_PORT` | `548` | AFP server port |
| `AFP_VOLUME` | `Dev` | Volume (share) name used by the tests — must be writable by the guest account |
| `AFP_SSH_TARGET` | `michaelc@127.0.0.1` | SSH target for server-side fixture setup (`user@host`) |
| `AFP_SSH_PORT` | `2222` | SSH port (WinBoat guest forwards 2222) |
| `AFP_SHARE_DIR` | `D:\Shares\Dev` | Windows path of `AFP_VOLUME`, matching the volume's share config |

Fixtures the tests create are deleted afterwards; the tests grant
`Everyone:(R,W)` on their fixture files only.

## The tests

| Test | Guards | Needs SSH to server |
|---|---|---|
| `afp_lineconv_test.py` | Line-ending conversion (wire CR ↔ disk LF) for configured extensions, incl. uppercase `.TXT` (case-insensitive extension match regression) | yes |
| `afp_stress.py` | Full Finder-like mount flow repeated N times: session, login, vol list, open/close every volume — no stalls or errors | no |
| `afp_dhx_probe.py` | DHCAST128 login hardening: zero/short/degenerate DH keys and a short `FPLoginCont` blob must produce AFP errors, never a crash (server liveness checked at the end) | no |
| `afp_xattr_test.py` | `FPListExtAttrs` enumerates named NTFS streams (`FILE_STREAM_INFORMATION` parsing) | yes |
| `afp_voldate.py` | AFP dates use the Mac 1904 epoch (`FPGetVolParms`) — pre-fix they decoded ~96 years in the past | no |

## Running

```sh
# from this directory, against the default dev guest
python3 afp_stress.py 10
python3 afp_voldate.py
python3 afp_dhx_probe.py
python3 afp_lineconv_test.py
python3 afp_xattr_test.py

# against another server/share
AFP_HOST=192.168.1.50 AFP_VOLUME=Public AFP_SHARE_DIR='E:\Public' python3 afp_voldate.py
```

Exit codes: `0` pass, `1` test failure, `2` environment problem (fixture
seeding or connectivity — the test did not run).

`afpclient.py` is the shared minimal DSI/AFP client (raw sockets, wire
format notes in its docstring); import it from new tests rather than
copy-pasting the framing code.
