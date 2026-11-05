#!/usr/bin/env bash
#
# attach-usb-and-port.sh - Wire a USB NTFS drive and AFP port 548 into an
# existing WinBoat docker-compose installation.
#
# Patches ~/.local/share/winboat-app/docker-compose.yml (idempotently):
#   * devices:  bind-mounts the USB partition at a STABLE container path
#               /dev/afpusb1 (host by-id path resolved from the volume label)
#   * ARGUMENTS: appends QEMU args exporting it as a virtio-blk disk, so
#               Windows mounts it natively (NTFS ADS = required for AFP)
#   * USER_PORTS: adds 548 so dockur/windows forwards container -> Windows guest
#   * ports:      publishes 0.0.0.0:548 so Mac clients on the LAN can reach it
#
# The compose change only takes effect when the container is recreated
# (docker compose down && up -d). Shut Windows down first (WinBoat UI -> stop,
# or shutdown inside the guest).
#
# Usage:
#   ./attach-usb-and-port.sh [--label NAME] [--port N] [--up] [--dry-run]
#     --label NAME   NTFS volume label to look up (default: MacFiles)
#     --port N       AFP port to publish (default: 548)
#     --up           Recreate the container afterwards (docker compose up -d)
#     --dry-run      Show the patched file instead of writing it
#
set -Eeuo pipefail

LABEL="MacFiles"
PORT=548
CONTAINER_PATH="/dev/afpusb1"
DO_UP=0
DRY_RUN=0

grn() { printf '\033[32m%s\033[0m\n' "$*"; }
ylw() { printf '\033[33m%s\033[0m\n' "$*"; }
die() { printf '\033[31mFATAL: %s\033[0m\n' "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
    case "$1" in
        --label)   LABEL="${2:?}"; shift;;
        --port)    PORT="${2:?}"; shift;;
        --up)      DO_UP=1;;
        --dry-run) DRY_RUN=1;;
        -h|--help) sed -n '3,25p' "$0"; exit 0;;
        *)         die "unknown option $1";;
    esac
    shift
done

# ------------------------------------------------------- docker invocation --
# Primary path: docker works directly once the docker group is active in this
# session (log out/in after setup-winboat-host.sh). Fallbacks, in order:
#   sg docker        - shadow's setuid helper (not shipped on all Arch systems)
#   sudo docker      - prompts for password in an interactive terminal
# NB: 'newgrp docker cmd' is deliberately NOT used: on at least one Arch/CachyOS
# configuration it exits 0 WITHOUT executing cmd (verified empirically 2026-11-05).
dock() {
    if docker info >/dev/null 2>&1; then
        docker "$@"
    elif command -v sg >/dev/null; then
        local q=""; local a
        for a in "$@"; do q+="$(printf '%q ' "$a")"; done
        sg docker "docker $q"
    else
        sudo docker "$@"
    fi
}

# ------------------------------------------------------------ locate compose --
COMPOSE=""
for c in "${XDG_DATA_HOME:-$HOME/.local/share}/winboat-app/docker-compose.yml" \
         "$HOME/.winboat/docker-compose.yml"; do
    [ -f "$c" ] && COMPOSE="$c" && break
done
[ -n "$COMPOSE" ] || die "No WinBoat docker-compose.yml found. Create the Windows VM in the WinBoat GUI first."
grn "compose file: $COMPOSE"

# ------------------------------------------------ locate USB partition by-id --
part=$(lsblk -nrpo NAME,FSTYPE,LABEL | awk -v l="$LABEL" '$2=="ntfs" && $3==l {print $1; exit}')
[ -n "$part" ] || die "No NTFS partition with label '${LABEL}' found. Plug it in (do not mount it)."

mnt=$(lsblk -nro MOUNTPOINT "$part" | tr -d '[:space:]')
[ -z "$mnt" ] || die "${part} is mounted at ${mnt}. Unmount it first: sudo umount ${mnt}  (the Windows VM must own it exclusively)."

# Prefer colon-free stable symlinks: docker/compose split 'devices' entries on
# ':' and by-id names like '...-0:0-part1' contain colons and BREAK parsing.
BYID=""
for link in /dev/disk/by-label/* /dev/disk/by-uuid/*; do
    [ -e "$link" ] || continue
    if [ "$(realpath "$link" 2>/dev/null)" = "$(realpath "$part")" ]; then BYID="$link"; break; fi
done
if [ -n "$BYID" ]; then
    grn "stable path:  ${BYID}"
else
    BYID="$part"
    ylw "WARNING: no colon-free stable symlink (by-label/by-uuid) for ${part};"
    ylw "         using raw device path - if the drive re-enumerates (sdb...), re-run this script."
fi

# ------------------------------------------------------- container lifecycle --
CONTAINER="WinBoat"
if dock ps --format '{{.Names}}' 2>/dev/null | grep -qx "$CONTAINER"; then
    if [ "$DRY_RUN" = 1 ]; then
        : # read-only preview, container state irrelevant
    elif [ "$DO_UP" = 1 ]; then
        ylw "Container '${CONTAINER}' is running and will be STOPPED to apply changes."
    else
        die "Container '${CONTAINER}' is running. Stop it in WinBoat first (shut down Windows), or rerun with --up to stop+recreate it automatically."
    fi
fi

# ------------------------------------------------------------- patch compose --
tmp=$(mktemp)
cp "$COMPOSE" "$tmp"
export AFP_PORT="$PORT"

# 1) devices: stable bind-mount of the partition (replace any prior entry for
#    the container path, so re-running after a drive change self-heals)
sed -i "\#- \"[^\"]*:${CONTAINER_PATH}\"#d" "$tmp"
awk -v item="      - \"${BYID}:${CONTAINER_PATH}\"" \
    '{print} /^    devices:$/ {print item}' "$tmp" > "${tmp}.2"
mv "${tmp}.2" "$tmp"

# 2) ARGUMENTS: virtio-blk export of the container path.
#    First strip any previously-appended afpusb chunk (self-heals flag changes
#    and re-runs), then append the canonical one.
#    cache=none + aio=native: required pairing (native AIO demands O_DIRECT) and
#    avoids double-caching a removable device through the Linux page cache.
#    NB: match only spaces/tabs before the optional closing quote - \s would
#    eat the line's newline and splice the following YAML line onto this one.
sed -i '/ARGUMENTS/ s/ -drive file=\/dev\/afpusb1[^ ]* -device virtio-blk-pci,drive=afpusb//g' "$tmp"
perl -i -pe 'if (/ARGUMENTS/ && !/afpusb/) { s/[ \t]*(["\x27]?)(\n?)$/ -drive file=\/dev\/afpusb1,if=none,id=afpusb,format=raw,cache=none,aio=native -device virtio-blk-pci,drive=afpusb$1$2/ }' "$tmp"

# 3) USER_PORTS: dockur/windows forwards these container ports into the guest
perl -i -pe 'BEGIN { $p = $ENV{AFP_PORT} } if (/USER_PORTS/ && !/(?<!\d)\Q$p\E(?!\d)/) { s/[ \t]*(["\x27]?)(\n?)$/\,$p$1$2/ }' "$tmp"

# 4) ports: publish on all interfaces (LAN-facing AFP) - insert after header
if ! grep -q "\"${PORT}:${PORT}/tcp\"" "$tmp"; then
    awk -v item="      - \"${PORT}:${PORT}/tcp\"" \
        '{print} /^    ports:$/ {print item}' "$tmp" > "${tmp}.2"
    mv "${tmp}.2" "$tmp"
fi

# 5) shared folder: dockur/windows exposes the container's /shared dir (with
#    HOME env set) as a drive inside Windows. If WinBoat's shared-folder option
#    was never enabled, mount ~/afp-deploy there so the guest can reach the MSI
#    and setup-guest.ps1. (Harmless if the image ignores it.)
if ! grep -q ":/shared" "$tmp"; then
    awk -v item="      - \"${HOME}/afp-deploy:/shared\"" \
        '{print} /^    volumes:$/ {print item}' "$tmp" > "${tmp}.2"
    mv "${tmp}.2" "$tmp"
fi

echo "-------- diff (current -> patched) --------"
diff -u "$COMPOSE" "$tmp" || true
echo "-------------------------------------------"

if [ "$DRY_RUN" = 1 ]; then
    ylw "--dry-run: nothing written."
    rm -f "$tmp"; exit 0
fi

backup="${COMPOSE}.bak.$(date +%Y%m%d-%H%M%S)"
cp "$COMPOSE" "$backup"
mv "$tmp" "$COMPOSE"
grn "compose patched (backup: ${backup})"

# validate
dock compose -f "$COMPOSE" config -q || die "Patched compose does not validate. Restore with: cp ${backup} ${COMPOSE}"

# ------------------------------------------------------------ restart if --up --
if [ "$DO_UP" = 1 ]; then
    echo "Recreating container..."
    # stop_grace_period (120s) in the compose file governs the ACPI shutdown window;
    # 'down --stop-timeout' no longer exists in compose v5.x.
    dock compose -f "$COMPOSE" down
    dock compose -f "$COMPOSE" up -d
    grn "container recreated."
else
    echo
    echo "Now: start the VM from WinBoat (compose up happens there)."
fi

cat <<EOF

Verify after Windows boots:
  * Linux:  docker exec WinBoat cat /proc/net/tcp | grep -i ':0224' || nc -zvw3 127.0.0.1 ${PORT}
  * Linux:  nc -zvw3 <this-host-ip> ${PORT}
  * Windows: the ${LABEL} disk appears in Disk Management; drive letter is
    assigned automatically (setup-guest.ps1 finds it by volume LABEL).
NOTE: never mount ${LABEL} on the Linux host while the VM is running.
EOF
