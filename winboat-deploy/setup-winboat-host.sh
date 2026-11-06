#!/usr/bin/env bash
#
# setup-winboat-host.sh - Provision a CachyOS host to run the Personal AFP
# Server inside a WinBoat (QEMU/KVM + Docker) Windows guest.
#
# What it does (all steps idempotent):
#   1. Verifies prerequisites (CachyOS/Arch, KVM, /dev/fuse, disk space)
#   2. Installs docker, docker-compose, freerdp via pacman
#   3. Enables/starts the docker service and adds the user to the docker group
#   4. Installs WinBoat (AUR winboat-bin via paru/yay; AppImage fallback)
#   5. Stages the AFP server artifacts (MSI + setup-guest.ps1) into ~/afp-deploy
#   6. Installs a udev rule so the desktop never automounts the USB drive that
#      will be handed exclusively to the Windows VM (prevents NTFS corruption)
#
# Usage:
#   ./setup-winboat-host.sh [-y] [--label NAME] [--skip-winboat] [--no-aur] [--no-udev]
#     -y, --yes          Non-interactive: assume yes to pacman/paru prompts
#     --label NAME       Volume label of the USB drive to protect (default: MacFiles)
#     --skip-winboat     Do not install WinBoat itself
#     --no-aur           Skip AUR, go straight to the AppImage install path
#     --no-udev          Skip the udisks automount-guard rule
#
# Run as your normal desktop user. Uses sudo where needed (will prompt).
# After this script: create the Windows VM once via the WinBoat GUI wizard,
# then run ./attach-usb-and-port.sh to wire the USB disk and AFP port into it.
#
set -Eeuo pipefail

# ---------------------------------------------------------------- constants --
readonly MSI_URL="https://github.com/anti-matter/Windows_AFP_Server/releases/download/v2.1.0/AfpServerInstaller.msi"
readonly MSI_SHA256="30048227b842b301bf3cf33fff5f20f516f89f60bfeec46f590fcbc1f6e8fb4f"
readonly DEPLOY_DIR="${HOME}/afp-deploy"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly SCRIPT_DIR

USB_LABEL="MacFiles"
ASSUME_YES=0
INSTALL_WINBOAT=1
ALLOW_AUR=1
UDEV_GUARD=1

# ------------------------------------------------------------------ helpers --
red()   { printf '\033[31m%s\033[0m\n' "$*"; }
grn()   { printf '\033[32m%s\033[0m\n' "$*"; }
ylw()   { printf '\033[33m%s\033[0m\n' "$*"; }
step()  { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
die()   { red "FATAL: $*" >&2; exit 1; }

SUDO_USER_NAME="${SUDO_USER:-$(id -un)}"
pac_flags=()
pm_flags=()
confirm() { # confirm "prompt" -> 0/1
    [ "$ASSUME_YES" = 1 ] && return 0
    read -rp "$1 [y/N] " a
    [[ "$a" =~ ^[Yy]$ ]]
}

usage() { sed -n '3,27p' "$0"; exit 0; }

# ------------------------------------------------------------------- params --
while [ $# -gt 0 ]; do
    case "$1" in
        -y|--yes)         ASSUME_YES=1; pac_flags+=(--noconfirm); pm_flags+=(--noconfirm);;
        --label)          USB_LABEL="${2:?--label needs a value}"; shift;;
        --skip-winboat)   INSTALL_WINBOAT=0;;
        --no-aur)         ALLOW_AUR=0;;
        --no-udev)        UDEV_GUARD=0;;
        -h|--help)        usage;;
        *)                die "Unknown option: $1 (try --help)";;
    esac
    shift
done

# ------------------------------------------------------------ prereq checks --
step "1/6 Checking prerequisites"

[ "$(id -u)" != 0 ] || die "Run as your normal user, not root (the script calls sudo as needed)."

if [ -r /etc/os-release ]; then
    . /etc/os-release
    case "${ID:-}" in
        cachyos|arch) grn "  distro: ${PRETTY_NAME:-$ID}";;
        *)            ylw "  WARNING: not CachyOS/Arch (${PRETTY_NAME:-unknown}); pacman/AUR steps may not apply.";;
    esac
fi

[ -e /dev/kvm ] || die "/dev/kvm not present. Enable virtualization (VT-x/AMD-V) in BIOS and ensure kvm_amd/kvm_intel modules are loaded."
[ -r /dev/kvm ] && [ -w /dev/kvm ] || ylw "  WARNING: /dev/kvm not user-writable (usually fine via docker; WinBoat needs it in-container)."
[ -c /dev/fuse ] || ylw "  WARNING: /dev/fuse missing; AppImage path may need libfuse."

free_gb=$(df -BG --output=avail "${HOME}" | tail -1 | tr -dc '0-9')
[ "${free_gb:-0}" -ge 40 ] || ylw "  WARNING: only ${free_gb}GB free in ${HOME}; WinBoat VM needs 32GB+."

command -v pacman >/dev/null || die "pacman not found - this script targets CachyOS/Arch."
grn "  OK ($(nproc) CPUs, $(free -g | awk '/^Mem:/{print $2}')GB RAM, ${free_gb}GB free at ${HOME})"

# ----------------------------------------------------------- sudo keep-alive --
step "2/6 Privileges"
sudo -v
# keep sudo alive in the background while the script runs
( while true; do sudo -n true; sleep 60; kill -0 "$$" 2>/dev/null || exit; done ) &
SUDO_KEEPALIVE_PID=$!
trap 'kill "$SUDO_KEEPALIVE_PID" 2>/dev/null || true' EXIT
grn "  sudo OK (target user: ${SUDO_USER_NAME})"

# ------------------------------------------------------------ pacman pkgs ---
step "3/6 Installing packages (docker, docker-compose, freerdp)"
pkgs=()
command -v docker          >/dev/null || pkgs+=(docker)
command -v docker >/dev/null && ! docker compose version >/dev/null 2>&1 && pkgs+=(docker-compose)
# freerdp 3.x ships as xfreerdp3 on Arch/CachyOS
{ command -v xfreerdp >/dev/null || command -v xfreerdp3 >/dev/null; } || pkgs+=(freerdp)
command -v rsync           >/dev/null || pkgs+=(rsync)
if [ "${#pkgs[@]}" -gt 0 ]; then
    echo "  To install: ${pkgs[*]}"
    confirm "Install with pacman?" && sudo pacman -S "${pac_flags[@]}" "${pkgs[@]}" || ylw "  skipped (packages will be required later)"
else
    grn "  all packages already installed"
fi
command -v docker >/dev/null || die "docker is required."

# ------------------------------------------------- docker service + group --
step "4/6 Docker service and user group"
if ! systemctl is-active --quiet docker; then
    confirm "Start + enable the docker service?" && sudo systemctl enable --now docker || die "docker service is not running and was not started."
fi
grn "  docker service: $(systemctl is-active docker)"
if ! id -nG "$SUDO_USER_NAME" | grep -qw docker; then
    echo "  Adding ${SUDO_USER_NAME} to the docker group..."
    sudo usermod -aG docker "$SUDO_USER_NAME"
    ylw "  Group added. It takes effect at next login; commands can already use 'sg docker -c \"...\"'."
else
    grn "  ${SUDO_USER_NAME} is already in the docker group"
fi
if ! docker info >/dev/null 2>&1; then
    ylw "  'docker' is not usable in already-open shells yet (new group)."
    ylw "  Fix: log out and back in. Until then, scripts fall back to 'sudo docker'."
fi

# ----------------------------------------------------------- install winboat --
step "5/6 WinBoat"
winboat_present() { command -v winboat >/dev/null 2>&1 || [ -x /opt/winboat/winboat ]; }
if [ "$INSTALL_WINBOAT" = 1 ]; then
    if winboat_present; then
        grn "  winboat already installed: $(command -v winboat 2>/dev/null || echo /opt/winboat/winboat)"
    elif [ "$ALLOW_AUR" = 1 ] && command -v paru >/dev/null; then
        echo "  Installing winboat-bin from AUR via paru..."
        paru -S "${pm_flags[@]}" --needed winboat-bin
    elif [ "$ALLOW_AUR" = 1 ] && command -v yay >/dev/null; then
        echo "  Installing winboat-bin from AUR via yay..."
        yay -S "${pm_flags[@]}" --needed winboat-bin
    else
        echo "  Installing WinBoat AppImage from GitHub releases..."
        url=$(curl -fsSL https://api.github.com/repos/winboat-org/winboat/releases/latest \
              | grep -o '"browser_download_url": *"[^"]*\.AppImage"' \
              | head -1 | sed 's/.*"\(http[^"]*\)"/\1/')
        [ -n "$url" ] || die "Could not find an AppImage asset in the latest WinBoat release."
        mkdir -p "${HOME}/.local/bin"
        curl -fL --progress-bar -o "${HOME}/.local/bin/winboat.appimage" "$url"
        chmod +x "${HOME}/.local/bin/winboat.appimage"
        ln -sf "${HOME}/.local/bin/winboat.appimage" "${HOME}/.local/bin/winboat"
        grn "  installed: ${HOME}/.local/bin/winboat (ensure ~/.local/bin is on PATH)"
    fi
else
    ylw "  skipped (--skip-winboat)"
fi

# ------------------------------------------------- stage AFP server files --
step "6/6 AFP server artifacts -> ${DEPLOY_DIR}"
mkdir -p "$DEPLOY_DIR"
if [ ! -f "${DEPLOY_DIR}/AfpServerInstaller.msi" ] \
   || ! echo "${MSI_SHA256}  ${DEPLOY_DIR}/AfpServerInstaller.msi" | sha256sum --check --status 2>/dev/null; then
    echo "  downloading AfpServerInstaller.msi (v2.1.0)..."
    curl -fL --progress-bar -o "${DEPLOY_DIR}/AfpServerInstaller.msi.part" "$MSI_URL"
    echo "${MSI_SHA256}  ${DEPLOY_DIR}/AfpServerInstaller.msi.part" | sha256sum --check - || {
        rm -f "${DEPLOY_DIR}/AfpServerInstaller.msi.part"; die "MSI download failed checksum - aborting."; }
    mv "${DEPLOY_DIR}/AfpServerInstaller.msi.part" "${DEPLOY_DIR}/AfpServerInstaller.msi"
fi
grn "  MSI in place (sha256 verified)"
for f in setup-guest.ps1 guest-fix-share-drive.ps1 attach-usb-and-port.sh dev-sync.sh setup-dev.ps1 dev-loop.ps1; do
    if [ -f "${SCRIPT_DIR}/${f}" ]; then
        cp -f "${SCRIPT_DIR}/${f}" "${DEPLOY_DIR}/${f}"
        case "$f" in *.sh) chmod +x "${DEPLOY_DIR}/${f}";; esac
        grn "  ${f} staged"
    else
        ylw "  ${f} not found next to this script - copy it manually"
    fi
done
# Microsoft's .NET SDK installer script, staged locally: setup-dev.ps1 uses it
# from the share because some guests cannot resolve the dot.net shortlink.
if curl -fsSL --max-time 60 https://dot.net/v1/dotnet-install.ps1 -o "${DEPLOY_DIR}/dotnet-install.ps1"; then
    grn "  dotnet-install.ps1 staged"
else
    ylw "  could not fetch dotnet-install.ps1 - setup-dev.ps1 will retry from the guest"
fi

# --------------------------------------- udisks automount guard (optional) --
if [ "$UDEV_GUARD" = 1 ]; then
    rule="/etc/udev/rules.d/99-afp-usb-nomount.rules"
    echo "  Installing automount guard for USB volumes labelled '${USB_LABEL}' -> ${rule}"
    printf 'ENV{ID_FS_LABEL}=="%s", ENV{UDISKS_IGNORE}="1"\n' "$USB_LABEL" | sudo tee "$rule" >/dev/null
    sudo udevadm control --reload-rules
    grn "  desktop file managers will no longer auto-mount '${USB_LABEL}' (the Windows VM must own it exclusively)"
fi

# ------------------------------------------------------------------ summary --
cat <<EOF

$(grn 'Host provisioning complete.')

Next steps:
  1. REBOOT (simplest) so the docker group applies. NB: a plain logout/login may
     NOT be enough - the systemd *user manager* keeps the old group set and can
     survive logout if any user service pins it (lingering, ollama, etc.). All
     launcher-spawned apps (incl. WinBoat) inherit from it. Check with:
       grep -qw 955 /proc/$(pgrep -u $USER -x systemd --newest | head -1)/status
     or just run:  docker info   in a new terminal.
  2. Start WinBoat and create the Windows VM in the GUI wizard:
       - Windows 11, RAM >= 8 GB, CPU >= 4, disk >= 64 GB
       - KEEP the "shared folder" option enabled (maps your home into the guest)
       - Language: English (the AFP server hardcodes the local 'Guest' account name)
  3. Plug in the USB drive (label: ${USB_LABEL}) - do NOT mount it on Linux.
  4. With the VM shut down (stop it in WinBoat), run:
       ${DEPLOY_DIR}/attach-usb-and-port.sh
     then start the VM again from WinBoat.
  5. Inside Windows, from the mapped home drive, in an ELEVATED PowerShell:
       Set-ExecutionPolicy -Scope Process Bypass -Force
       & Z:\afp-deploy\setup-guest.ps1        (drive letter for your home may vary)
  6. Verify from Linux:  nc -zvw3 <this-machine-ip> 548
EOF
