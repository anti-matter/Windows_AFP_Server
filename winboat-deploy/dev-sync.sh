#!/usr/bin/env bash
#
# dev-sync.sh - Linux side of the AFP-server dev loop (WinBoat guest builds).
#
# Pushes the working tree to the guest-visible shared folder, triggers the
# in-guest watcher (dev-loop.ps1, installed by setup-dev.ps1), waits for the
# build result, and - on success - DSI-probes the freshly restarted server.
#
# Usage:  ~/afp-deploy/dev-sync.sh [--no-wait] [--timeout SECS] [--probe-host IP]
#
set -Eeuo pipefail

DEPLOY_DIR="${HOME}/afp-deploy"
DEV="$DEPLOY_DIR/dev"
REQUEST="$DEV/build.request"
RESULT="$DEV/build.result"
WAIT=1
TIMEOUT=900
PROBE_HOST="127.0.0.1"

while [ $# -gt 0 ]; do
    case "$1" in
        --no-wait)     WAIT=0;;
        --timeout)     TIMEOUT="$2"; shift;;
        --probe-host)  PROBE_HOST="$2"; shift;;
        -h|--help)     sed -n '3,10p' "$0"; exit 0;;
        *)             echo "unknown option: $1" >&2; exit 1;;
    esac
    shift
done

REPO_DIR="${AFP_REPO:-$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel 2>/dev/null || true)}"
[ -n "$REPO_DIR" ] || { echo "not inside a git repo - run the repo copy or set AFP_REPO=/path/to/repo" >&2; exit 1; }
[ -d "$REPO_DIR/afpServer" ] || { echo "AFP_REPO does not look like this project: $REPO_DIR" >&2; exit 1; }

mkdir -p "$DEV/repo"

echo "==> syncing $REPO_DIR -> $DEV/repo"
rsync -a --delete \
      --exclude '.git/' --exclude '.vs/' --exclude 'out/' \
      --exclude 'Release/' --exclude 'Debug/' --exclude 'x64/' --exclude 'obj/' \
      --exclude 'PersonalAfpServer/' --exclude '*.zip' --exclude '*.log' \
      "$REPO_DIR/" "$DEV/repo/"

rm -f "$RESULT"
touch "$REQUEST"
echo "==> build requested (guest watcher polls every 5 s)"

[ "$WAIT" = 0 ] && exit 0

echo "==> waiting up to ${TIMEOUT}s for build.result ..."
deadline=$(( $(date +%s) + TIMEOUT ))
while [ ! -f "$RESULT" ]; do
    if [ "$(date +%s)" -ge "$deadline" ]; then
        echo "TIMEOUT waiting for the guest watcher."
        echo "check: is the VM running and AfpDevLoop registered? (schtasks /Query /TN AfpDevLoop in the guest)"
        exit 1
    fi
    sleep 2
done

# PowerShell 5.1 Set-Content -Encoding UTF8 prepends a BOM; strip it
sed -i '1s/^\xEF\xBB\xBF//' "$RESULT"
echo "================ guest build result ================"
cat "$RESULT"
echo "===================================================="
grep -q '^STATUS=PASS' "$RESULT" || { echo "BUILD FAILED (see tail above)"; exit 1; }

# quick AFP sanity check against the restarted server (it boots a few seconds
# after the watcher writes the result, so retry for ~45 s)
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
