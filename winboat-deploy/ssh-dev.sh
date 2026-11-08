#!/usr/bin/env bash
#
# ssh-dev.sh - Linux side of the AFP-server dev loop, over SSH.
#
# Successor to dev-sync.sh: instead of dropping a request on the \\host.lan
# SMB share for the AfpDevLoop watcher to poll, this drives the guest directly
# through OpenSSH (guest port 22, hostfwd'd by the WinBoat container to
# 127.0.0.1:2222). That frees the guest's interactive session and the SMB
# share for real AFP/SMB clients.
#
# Flow: tar the working tree over SSH into C:\dev\repo (build outputs stay,
# so MSBuild is incremental; a manifest prunes deleted sources) -> run
# C:\dev\guest-build.ps1 remotely, streaming its output live -> on --msi,
# scp the finished installer to ~/afp-deploy/dev/out/ -> DSI-probe the
# restarted server.
#
# Usage:  ssh-dev.sh [--msi] [--msi-install] [--timeout SECS] [--probe-host IP]
# Env:    AFP_GUEST = user@host[:port] (default: michaelc@127.0.0.1, which for
#         loopback means the WinBoat hostfwd on 2222; other hosts default to
#         the real SSH port 22), AFP_REPO = repo path
#
set -Eeuo pipefail

DEPLOY_DIR="${HOME}/afp-deploy"
OUT_DIR="$DEPLOY_DIR/dev/out"
GUEST="${AFP_GUEST:-michaelc@127.0.0.1}"
GUEST_PORT=""
case "$GUEST" in
    *:*) GUEST_PORT="${GUEST##*:}"; GUEST="${GUEST%@:*}";;
esac
if [ -z "$GUEST_PORT" ]; then
    case "$GUEST" in
        *@localhost|*@127.0.0.1|*@::1) GUEST_PORT=2222;;  # WinBoat hostfwd
        *)                             GUEST_PORT=22;;    # real Windows box
    esac
fi
TIMEOUT=900
PROBE_HOST="127.0.0.1"
MSI=0
MSI_INSTALL=0
CONFIGURATION="Release"

while [ $# -gt 0 ]; do
    case "$1" in
        --msi)          MSI=1;;
        --msi-install)  MSI=1; MSI_INSTALL=1;;
        --debug)        CONFIGURATION="Debug";;
        --timeout)      TIMEOUT="$2"; shift;;
        --probe-host)   PROBE_HOST="$2"; shift;;
        -h|--help)      sed -n "3,20p" "$0"; exit 0;;
        *)              echo "unknown option: $1" >&2; exit 1;;
    esac
    shift
done

REPO_DIR="${AFP_REPO:-$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel 2>/dev/null || true)}"
[ -n "$REPO_DIR" ] || { echo "not inside a git repo - run the repo copy or set AFP_REPO=/path/to/repo" >&2; exit 1; }
[ -d "$REPO_DIR/afpServer" ] || { echo "AFP_REPO does not look like this project: $REPO_DIR" >&2; exit 1; }

# one shared connection for the whole run (handshake once, reuse everywhere)
CM="$HOME/.ssh/cm-afpguest"
SSH=(ssh -p "$GUEST_PORT" -o BatchMode=yes -o ConnectTimeout=5
     -o ControlMaster=auto -o ControlPath="$CM" -o ControlPersist=300 "$GUEST")

read_ver() { sed -nE "s/.*#define[[:space:]]+$1[[:space:]]+([0-9]+).*/\1/p" "$REPO_DIR/afpServer/version.h"; }
VER="$(read_ver VERSION_MAJOR).$(read_ver VERSION_MINOR).$(read_ver VERSION_REVISION)"
case "$VER" in
    [0-9]*.[0-9]*.[0-9]*) ;;
    *) echo "could not parse version from afpServer/version.h (got '$VER')" >&2; exit 1;;
esac

echo "==> connecting to $GUEST (port $GUEST_PORT)"
"${SSH[@]}" "ver" >/dev/null \
    || { echo "SSH to the guest failed. Is Windows up, is sshd running, and is 127.0.0.1:$GUEST_PORT forwarded (USER_PORTS in ~/.winboat/docker-compose.yml)?" >&2; exit 1; }

# ---- 1) sync sources (tar over ssh; guest has bsdtar, no rsync) -----------
TAR_EXCLUDES=(--exclude='./.git' --exclude='./.vs' --exclude='./out'
              --exclude='*/Release' --exclude='*/Debug' --exclude='*/x64'
              --exclude='*/obj' --exclude='*/ipch' --exclude='./PersonalAfpServer'
              --exclude='*.zip' --exclude='*.log')
echo "==> syncing $REPO_DIR -> ${GUEST}:C:/dev/repo"
"${SSH[@]}" 'cmd /c if not exist C:\dev\repo mkdir C:\dev\repo'
# -m on the guest extract: extracted files get a CURRENT guest mtime.
# tar carries Linux mtimes that can land older than the guest's existing
# .obj files (timestamp handling across the wire), which makes MSBuild's
# incremental check silently skip changed sources. Fresh mtimes keep the
# incremental build honest (worst case it rebuilds a bit more).
tar cf - "${TAR_EXCLUDES[@]}" -C "$REPO_DIR" . \
    | "${SSH[@]}" 'tar xmf - -C C:/dev/repo' \
    || { echo "source sync failed" >&2; exit 1; }

# manifest of what the tree SHOULD contain (same exclusions as tar) -
# guest-build.ps1 prunes repo files missing from it, so deleted sources
# really disappear. (find/tar exclude syntaxes coincide for these patterns.)
MANIFEST="$(mktemp)"
trap 'rm -f "$MANIFEST"' EXIT
( cd "$REPO_DIR" && find . -type f \
     ! -path './.git/*' ! -path './.vs/*' ! -path './out/*' \
     ! -path '*/Release/*' ! -path '*/Debug/*' ! -path '*/x64/*' \
     ! -path '*/obj/*' ! -path '*/ipch/*' ! -path './PersonalAfpServer/*' \
     ! -name '*.zip' ! -name '*.log' \
     | sed 's|^\./||' | tr -d '\r' ) > "$MANIFEST"
scp -P "$GUEST_PORT" -q -o ControlPath="$CM" \
    "$MANIFEST" "$GUEST:C:/dev/repo.manifest"

# keep the remote build script current (it IS the protocol between the two)
GUEST_BUILD_SRC=""
for c in "$(dirname "${BASH_SOURCE[0]}")/guest-build.ps1" \
         "$DEPLOY_DIR/guest-build.ps1" \
         "$(git -C "$REPO_DIR" rev-parse --show-toplevel 2>/dev/null)/winboat-deploy/guest-build.ps1"; do
    [ -f "$c" ] && { GUEST_BUILD_SRC="$c"; break; }
done
[ -n "$GUEST_BUILD_SRC" ] || { echo "guest-build.ps1 not found next to ssh-dev.sh or in $DEPLOY_DIR" >&2; exit 1; }
scp -P "$GUEST_PORT" -q -o ControlPath="$CM" \
    "$GUEST_BUILD_SRC" "$GUEST:C:/dev/guest-build.ps1"

# ---- 2) remote build (streams compiler output live) -----------------------
ARGS="-Version $VER -Configuration $CONFIGURATION"
[ "$MSI" = 1 ] && ARGS="$ARGS -Msi"
[ "$MSI_INSTALL" = 1 ] && ARGS="$ARGS -MsiInstall"
EXTRA=""
if [ "$MSI" = 1 ]; then EXTRA=", MSI yes"; fi
[ "$CONFIGURATION" = "Debug" ] && EXTRA="$EXTRA, DEBUG build"
echo "==> building on guest (version $VER$EXTRA) - Ctrl-C detaches, guest build continues"
RESULT="$(mktemp)"
trap 'rm -f "$MANIFEST" "$RESULT"' EXIT
set +e
timeout "$TIMEOUT" "${SSH[@]}" \
    "powershell -NoProfile -ExecutionPolicy Bypass -File C:\\dev\\guest-build.ps1 $ARGS" 2>&1 \
    | tee "$RESULT" | tr -d '\r'
RC=${PIPESTATUS[0]}
set -e
echo "================ guest build result ================"
grep -q '^STATUS=PASS' "$RESULT" || { echo "BUILD FAILED (exit $RC, see tail above)"; exit 1; }

mkdir -p "$OUT_DIR"
if [ "$MSI" = 1 ]; then
    MSI_NAME="$(sed -nE 's/^MSI=(.+)$/\1/p' "$RESULT" | tr -d '\r' | tail -1)"
    if [ -n "$MSI_NAME" ]; then
        scp -P "$GUEST_PORT" -q -o ControlPath="$CM" "$GUEST:C:/dev/out/$MSI_NAME" "$OUT_DIR/"
        echo "==> MSI: $OUT_DIR/$MSI_NAME"
        sha256sum "$OUT_DIR/$MSI_NAME"
        ls -la "$OUT_DIR/$MSI_NAME" | awk '{print "    size:", $5, "bytes"}'
        if grep -q 'MSI_INSTALL=OK' "$RESULT"; then
            echo "==> MSI was installed into the guest (major upgrade); verify in 'Apps & features'."
        fi
    else
        echo "==> WARNING: build passed but result had no MSI= line" >&2
    fi
fi

# ---- 3) quick AFP sanity check against the restarted server ---------------
python3 - "$PROBE_HOST" <<'EOF'
import socket, struct, sys, time
host = sys.argv[1]
req = struct.pack('>BBHI', 0x00, 0x03, 1, 0) + struct.pack('>II', 0, 0)
deadline = time.time() + 45
err = None
while time.time() < deadline:
    try:
        s = socket.create_connection((host, 548), timeout=5)
        s.sendall(req)
        hdr = b''
        while len(hdr) < 16:
            c = s.recv(16 - len(hdr))
            if not c: break
            hdr += c
        s.close()
        if len(hdr) == 16:
            dlen = struct.unpack('>I', hdr[8:12])[0]
            print(f"==> AFP server on {host}:548 answers DSIGetStatus (statusLen={dlen}) - dev build is LIVE")
            sys.exit(0)
        err = "connected, no DSI reply"
    except Exception as e:
        err = f"{type(e).__name__}: {e}"
    time.sleep(3)
print(f"==> WARNING: no DSI answer from {host}:548 within 45 s ({err}). "
      f"Check the server in the guest (schtasks /Run /TN PersonalAfpServer).")
sys.exit(1)
EOF
