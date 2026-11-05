# WinBoat deployment: Personal AFP Server on CachyOS

Scripts to run the Windows AFP server inside a WinBoat (QEMU/KVM + Docker,
dockur/windows) Windows guest on a CachyOS host, with the shared volumes
hosted on a dedicated **NTFS USB drive** passed through to the guest.

Why the pass-through: the AFP volume format stores resource forks and Finder
info in NTFS Alternate Data Streams (`:Afp_AfpResource`, `AFP_AfpInfo`), which
only exist when Windows itself mounts the disk. WinBoat's normal shared-folder
(virtio-fs) and any Linux-side mount of the same drive **cannot** host AFP
volumes — and dual-mounting NTFS corrupts it.

## Files

| File | Runs on | Purpose |
|---|---|---|
| `setup-winboat-host.sh` | CachyOS host | Installs docker/compose/freerdp, WinBoat (AUR or AppImage), docker group, stages `~/afp-deploy`, installs a udev guard that stops the desktop automounting the USB drive |
| `attach-usb-and-port.sh` | CachyOS host | Patches WinBoat's compose (backed up, idempotent, validated): USB partition → stable in-container path `/dev/afpusb1` → virtio-blk disk; forwards + publishes TCP 548 for LAN Mac clients |
| `setup-guest.ps1` | Windows guest (elevated) | Firewall rule TCP 548, creates `Shares\MacintoshHD` on the USB drive (found by volume **label**), silent MSI install, HKCU share seed, logon autostart task |

## Flow

```text
host:  ./setup-winboat-host.sh            # packages, groups, winboat, artifacts
gui:   WinBoat wizard -> create Windows 11 VM (English!, shared folder ON)
host:  ./attach-usb-and-port.sh --up      # with the VM stopped; then start it
guest: elevated PowerShell: & <home-drive>:\afp-deploy\setup-guest.ps1
host:  nc -zvw3 <host-ip> 548             # and point a Mac client at afp://<host-ip>
```

Details:

1. **Host provisioning** — `setup-winboat-host.sh [-y] [--label MacFiles] [--no-aur] [--skip-winboat] [--no-udev]`.
   Verifies KVM/disk, installs packages, adds you to `docker`, installs
   `winboat-bin` (paru/yay, AppImage fallback), downloads
   `AfpServerInstaller.msi` (v2.1.0, sha256-verified) into `~/afp-deploy`,
   and drops a udisks rule so the DE never automounts the USB volume.
   Log out/in afterwards for the docker group.
2. **Create the VM in WinBoat's GUI** (the one manual step; WinBoat has no CLI):
   Windows 11, ≥ 8 GB RAM, keep the *shared folder* option enabled (that is how
   the guest sees `~/afp-deploy`). Use **English** — the server hardcodes the
   local `Guest` account name for guest access.
3. **Attach USB + port** — shut the VM down, then
   `./attach-usb-and-port.sh [--label MacFiles] [--port 548] [--up] [--dry-run]`.
   It resolves the NTFS partition by label to its `/dev/disk/by-id` path, adds
   a `devices:` bind-mount (`/dev/afpusb1`), appends the `-drive`/`virtio-blk`
   QEMU args to `ARGUMENTS`, adds `548` to `USER_PORTS` (dockur's slirp
   hostfwd into the guest) and a `0.0.0.0:548` port publish. Re-running is a
   no-op; a timestamped backup of the compose is written next to it.
4. **Inside Windows** — from the mapped home drive, elevated PowerShell:
   `Set-ExecutionPolicy -Scope Process Bypass -Force; & X:\afp-deploy\setup-guest.ps1`
   Then create local user accounts for AFP logins (`net user mac * /add`) —
   Mac clients authenticate via `LogonUser` against the guest's local SAM.
5. **Autostart** — `setup-guest.ps1` registers a highest-privileges scheduled
   task (`PersonalAfpServer`) at logon; the guest auto-logs in, so starting
   the WinBoat container is all that's needed thereafter.

## Operational notes

- The USB drive (`MacFiles`) is owned **exclusively by the Windows guest**
  while the container runs. Never mount it on Linux at the same time.
  The udev guard prevents accidental DE automounts; check `lsblk` if unsure.
- Compose lives at `~/.local/share/winboat-app/docker-compose.yml`
  (older WinBoat: `~/.winboat/`). WinBoat's own Config UI rewrites some fields;
  if you change RAM/CPU there and lose the AFP bits, re-run
  `attach-usb-and-port.sh` — it is idempotent.
- AFP has no discovery protocol here (the server implements none): clients
  connect by address, `afp://<linux-host-ip>` — NAT/forwarding of TCP 548 is
  all that's required.
- Mac clients see one volume, `MacintoshHD` → `MacFiles:\Shares\MacintoshHD`.
  Add more volumes in the server's tray UI (full-desktop session) — its tray
  icon is not visible in RemoteApp seamless mode.
