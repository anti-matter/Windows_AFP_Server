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
| `attach-usb-and-port.sh` | CachyOS host | Patches WinBoat's compose (backed up, idempotent, validated): USB partition → stable in-container path `/dev/afpusb1` → virtio-blk disk; forwards + publishes TCP 548 for LAN Mac clients; pins guest DNS to the relay below and (re)starts the relay container |
| `dns-dbus-forwarder.py` | host (docker: `afp-dns-relay`) | Plaintext UDP :53 on the compose network → systemd-resolved via D-Bus. Needed because the ISP blackholes plaintext DNS, DoT *and* DoH (SNI MITM); only resolved's encrypted session survives, and Docker/dnsmasq speak only plaintext (see below) |
| `setup-guest.ps1` | Windows guest (elevated) | Firewall rule TCP 548, creates `Shares\MacintoshHD` on the USB drive (found by volume **label**), silent MSI install, HKCU share seed, logon autostart task |
| `guest-fix-share-drive.ps1` | Windows guest (task) | Logon guard: re-asserts the canonical USB drive letter, repairs HKCU share path, restarts server if it drifted |
| `setup-dev.ps1` | Windows guest / real box (elevated) | One-time dev-loop setup: VS Build Tools (Desktop C++), .NET SDK + WiX v7 (+ UI extension), inbound firewall rule for TCP 548 (`-NoFirewall` skips), repoints autostart at `C:\dev\server` (`-Undo` reverts); `-NoWatcher` skips the legacy `AfpDevLoop` watcher (use on SSH-driven machines) |
| `dev-loop.ps1` | Windows guest (task, retired) | Legacy SMB-polling build watcher — fallback only, replaced by SSH loop |
| `dev-sync.sh` | host (Linux) | Legacy dev-loop driver for the watcher above — fallback only, see `ssh-dev.sh` |
| `setup-ssh.ps1` | Windows guest (elevated) | One-time SSH setup: sshd automatic + firewall, installs the host's public key (`host_id_ed25519.pub`) into `administrators_authorized_keys`, retires the `AfpDevLoop` watcher |
| `ssh-dev.sh` | host (Linux) | **Primary dev loop**: tar-over-SSH sync (manifest prunes deletions), remote `guest-build.ps1` with live output, MSI via scp, DSI-probe; `--msi`/`--msi-install`/`--probe-host` |
| `guest-build.ps1` | Windows guest (SSH remote-exec, `C:\dev`) | One-shot build: MSBuild Release\|x86 → stage `C:\dev\server` → restart (task, S4U fallback if headless) → optional WiX MSI |

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

## Development loop (edit on Linux, test in seconds)

The server is MSVC-only, so builds run **inside the VM**. The loop is driven
over **SSH** (guest OpenSSH server; the WinBoat container forwards
`127.0.0.1:2222` -> guest 22 via `USER_PORTS`), which keeps the guest's
console session and the SMB share free for real clients and manual GUI
debugging. Incremental MSBuild, no git push, no CI round trip; GitHub Actions
remains the authoritative build check for merges to `master`.

```text
Linux: edit code -> winboat-deploy/ssh-dev.sh
         tar-over-ssh -> C:\dev\repo          (build outputs kept = incremental)
         repo.manifest  -------------------->  prunes sources deleted on the host
         remote-exec C:\dev\guest-build.ps1   (compiler output streams live)
                                                 MSBuild Release|x86
                                                 stage + swap C:\dev\server
                                                 restart server (task; S4U fallback
                                                 when no console user is logged on)
       compiler errors land in your Linux terminal; PASS ends with a live DSIGetStatus probe
```

Setup once:

1. Build tools (elevated PowerShell in the guest):
   `Set-ExecutionPolicy -Scope Process Bypass -Force; & '\\host.lan\Data\setup-dev.ps1'`
   (~15-30 min: VS Build Tools, .NET SDK, WiX).
2. SSH access: copy the Linux host's `~/.ssh/id_ed25519.pub` to the share as
   `host_id_ed25519.pub`, then in the guest:
   `& '\\host.lan\Data\setup-ssh.ps1'` (enables sshd at boot, installs the key
   in `administrators_authorized_keys`, retires the old watcher task). It can
   also be piped over a password-auth SSH session if the share is unavailable.

Day-to-day: `./ssh-dev.sh` (`--msi`, `--msi-install`, `--probe-host <lan-ip>`).
Interactive guest debugging: `ssh -p 2222 michaelc@127.0.0.1` (elevated token).
Point Mac clients at `afp://<linux-ip>` — the dev build serves the same HKCU
shares/USB volume as the production one. To go back to the MSI build: run
`setup-dev.ps1 -Undo` in the guest. Notes: `C:\dev\repo` build outputs are
preserved across syncs; `.git`/artifacts are never synced; the dev server runs
from `C:\dev\server` while the MSI install stays untouched in Program Files.

Windows gotchas learned the hard way: sshd **kills its entire process tree**
when a session's channel closes — anything spawned from SSH (even `start /B`)
dies with it; only Task-Scheduler-launched processes survive, hence the
one-shot `AfpDevStart` S4U fallback task in `guest-build.ps1`. The autostart
`PersonalAfpServer` task is `InteractiveToken` and needs a console session:
guest auto-logon is enabled via `Winlogon/AutoAdminLogon`; if it ever resets,
the S4U fallback keeps the dev loop working headless.

The old SMB-polling loop (`dev-sync.sh` + `dev-loop.ps1` watcher) is retired
in favor of SSH but kept in the repo as a fallback if the guest's OpenSSH
ever breaks (`schtasks` registration via `setup-dev.ps1` re-enables it).

## Dev loop on a real Windows machine

Same loop, no WinBoat: any Win10/11 x86_64 box on the LAN (or routed from the
Linux host) with admin rights.

1. Copy `winboat-deploy\` to the box (SMB, USB — anything; SSH doesn't exist
   there yet) and run, from an **elevated** PowerShell in that directory:
   ```powershell
   Set-ExecutionPolicy -Scope Process Bypass -Force
   .\setup-dev.ps1 -NoWatcher        # VS Build Tools + .NET SDK + WiX (~15-30 min);
                                     # also allows inbound TCP 548 (idempotent,
                                     # -NoFirewall to skip)
   ```
   (`-NoWatcher` skips the legacy `\\host.lan` watcher; without it this is the
   same one-time setup as the guest.)
2. Copy the Linux host's `~/.ssh/id_ed25519.pub` to the box as
   `host_id_ed25519.pub`, still elevated: `.\setup-ssh.ps1`
   (installs OpenSSH Server if missing, sshd automatic, firewall TCP 22, key
   into `administrators_authorized_keys`).
3. From Linux: `AFP_GUEST=<user>@<box-ip> ./ssh-dev.sh` (port 22 is implied;
   add `--probe-host <box-ip>` so the probe targets the box instead of the
   WinBoat port-forward). The same flags apply (`--msi`, `--msi-install`).
   TCP 548 was opened in step 1, so Mac clients can point straight at
   `afp://<box-ip>`.

Notes: without a `PersonalAfpServer` autostart task, every build restarts the
server through the S4U fallback task (`AfpDevStart`) — same as headless
WinBoat; it does not survive a reboot until you register an autostart task or
log on and run the server once. First run has empty HKCU share config — start
the tray app once interactively and add volumes. Build outputs land in
`C:\dev\repo\afpServer\Release` exactly as in the guest; the box is x64 but
MSBuild targets Win32/x86, so the vendored 32-bit OpenSSL works unchanged.

## MSI builds via the loop

```bash
./ssh-dev.sh --msi            # also builds AfpServerInstaller-<version.h>.msi
./ssh-dev.sh --msi-install    # ...and installs it in the guest (major upgrade)
```

The MSI lands in `~/afp-deploy/dev/out/` with its SHA-256 in the result. The
version comes from `afpServer/version.h`; bump it before building a release
(same-version reinstalls are not a major upgrade). `--msi-install` exercises
the real `MajorUpgrade` path — verified upgrading a vdproj-era 2.2.1 install
in place, including event-log/PerfMon re-registration via the deferred
custom action. Requirements in the guest (installed by `setup-dev.ps1`):
.NET SDK + `dotnet tool install --global wix` + `WixToolset.UI.wixext`
(extension auto-fetched on first build if missing).

## DNS relay (`afp-dns-relay`)

The upstream network (Tele2-style ISP) blocks **all** plaintext DNS to public
resolvers (UDP *and* TCP/53), drops DoT application data on 853, and MITMs DoH
endpoints by SNI (bogus rcodes, fake "SSL Corp" certs). Only the host's
systemd-resolved session works. Docker snapshots resolv.conf at container
start and neither Docker's embedded resolver nor the guest's dnsmasq can speak
anything encrypted — so guest DNS dies silently (browsers dead, SMB/AFP fine).

Fix: `dns-dbus-forwarder.py` runs in a host-side container on the compose
network at `172.18.0.250` and forwards queries to systemd-resolved over D-Bus
(the API `resolvectl` uses). Compose pins `dns: [172.18.0.250]` and
`attach-usb-and-port.sh` rebuilds/relaunches it. If guest DNS ever breaks:
`docker logs afp-dns-relay`, then `docker exec WinBoat getent hosts aka.ms`.
