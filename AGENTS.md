# AGENTS.md

Guidance for AI coding agents (and humans) working on this repository.

## What this project is

**MacFile (Personal AFP Server) for Windows** — an Apple Filing Protocol (AFP 3.2) file
server for Windows, written in C++. It lets vintage Macintosh clients
(System 7–Mac OS 9, Mac OS X, SheepShaver, BasiliskII) access Windows shared
folders over TCP/IP (DSI on port 548). The server is a single 64-bit (x64) Win32 GUI
executable: a tray-icon app with dialog-based share configuration. It is *not*
an MFC or .NET project — plain Win32 API.

User-facing documentation lives in the
[wiki](https://github.com/anti-matter/MacFile/wiki). The AFP
protocol reference is `afpServer/ref/afp3XX.pdf` (Apple's AFP 3.x spec) —
consult it before changing protocol behavior.

## Repository layout

- `afpServer.sln` — Visual Studio solution.
- `afpServer/afpServer.vcxproj` — the server project (x64 only).
- `afpServer/afp-source/` — all server source code:
  - `dsi/` — DSI transport over TCP: connection objects, socket setup.
  - `fp-core/` — AFP command implementations (dispatch, desktop, directory,
    file I/O, server info, users, volumes, extended attributes). `afp.h` holds
    the protocol constants, command codes, and the `AFPERROR` type.
  - `fp-classes/` — server object model: sessions, volumes, registry-backed
    configuration (`afp_registry`), Mac↔Windows filename mapping
    (`afp_char_map`), network buffers (`afp_net_buffer`), locks, open files,
    file index, users/groups cache.
  - `fp-imp/` — higher-level operations: DHX login (OpenSSL-based), logon,
    server info, volume management.
  - `server-core/` — Windows integration: security tokens, change
    notifications, performance counters, power settings.
  - `helpers/` — debug logging (`afpdebug`), misc helpers, `byte_swap.h`.
  - `line-conversion/` — Mac (CR) ↔ Unix (LF) text conversion for configured
    file extensions.
  - `ui/` — the Fluent-style main window (`main_window.*`), theme/palette and
    fonts (`ui_theme.*`), custom controls (toggle switch, owner-draw buttons,
    GDI+ drawing helpers in `ui_controls.*`), plus the about, add-share
    (folder picker) and send-message dialogs.
- `afpServer/afpServer.cpp` — entry point, tray icon, main window.
- `afpServer/resources/` — icons, `afpserver.rc`, `resource.h`, and
  **generated** event-log/counter files (see Build).
- `afpServer/afpServer.man` — source of truth for Windows event-log messages
  and PerfMon counters.
- `afpServer/deps/openssl-1.1/` — vendored OpenSSL 1.1 (headers,
  `libcrypto64MT.lib` import library, and the `libcrypto-1_1-x64.dll` shipped beside the exe).
- `AfpServerInstaller/` — MSI installer: `product.wxs` (WiX v7 source) +
  `license.rtf`. Built with the `wix` .NET global tool (see Build); replaces
  the legacy `.vdproj`, reusing its UpgradeCode so WiX MSIs seamlessly
  major-upgrade installs of the old installer.
- `deploy.cmd` — Release post-build step; collects `AfpServer.exe`,
  `afpServer.man`, `install.cmd`, and `libcrypto-1_1-x64.dll` into
  `MacFile/` and zips it to `MacFile.zip` (repo root).
- `install.cmd` — run on the target machine after unzipping; registers /
  unregisters (`wevtutil`, `lodctr`) the event-log and PerfMon providers and
  opens / removes the Windows Firewall rule for inbound TCP 548. The MSI
  invokes it the same way (deferred, as SYSTEM).
- `afpServer/version.h` — version numbers; `afpServer/Changes.txt` — change log.
- `tests/` — stdlib-only Python 3 end-to-end AFP/DSI clients (DHCAST128
  hardening probe, xattr/line-conversion/permissions/name-handling tests,
  mount stress); see `tests/README.md`.
- `winboat-deploy/` — Linux→Windows SSH dev-loop scripts (see its README).

## Build

Windows only. Requires **Visual Studio 2026** (platform toolset **v145**) with
the *Desktop development with C++* workload and a Windows 10/11 SDK. The
project uses `stdcpp20` / `stdc17`, Unicode, `/W4`, SDL checks, and the
precompiled header `afpServer/stdafx.h`.

From a *Developer Command Prompt*:

```
msbuild afpServer.sln /t:afpServer /p:Configuration=Release /p:Platform=x64
```

(In the IDE, open `afpServer.sln` and pick **Release | x64** — the solution
platform and project platform are both `x64`.)

Notes:

- The **pre-build event** regenerates `resources/afpEvents.h`, `afpEvents.rc`,
  `afpCounters.h`, and `afpCounters.rc` from `afpServer.man` using the SDK
  tools `mc.exe` and `ctrpp`. Edit `afpServer.man`, never the generated files.
- The Release **post-build event** copies the OpenSSL DLL and runs
  `deploy.cmd` (produces `MacFile.zip`; both are gitignored).
- The MSI is built by CI (the Build workflow uploads
  `MacFile-<ver>.msi` as the `MacFile-msi` artifact,
  versioned from `version.h`). Build it locally with WiX v7 (open source, no
  VS IDE needed): `dotnet tool install --global wix`, then
  `wix extension add -g WixToolset.UI.wixext` **and**
  `wix extension add -g WixToolset.Util.wixext` (v7 does not auto-fetch
  extensions) and from `AfpServerInstaller/`:
  `wix build -acceptEula wix7 -arch x64 -ext WixToolset.UI.wixext -ext WixToolset.Util.wixext -d ProductVersion=<version.h> -out MacFile-<ver>.msi product.wxs`.
  Run it from the `AfpServerInstaller/` directory (`product.wxs` uses `..\`
  paths resolved against the CWD). `product.wxs` notes explain the deferred
  custom actions (event-log/PerfMon registration via `install.cmd`) and the
  `MediaTemplate EmbedCab` requirement for single-file MSIs. The dev loop
  automates all of this: `winboat-deploy/ssh-dev.sh --msi[-install]` (SSH into
  the Windows guest; see `winboat-deploy/README.md`).
- **CI** (`.github/workflows/build.yml`) builds `Release | x64` on
  `windows-2025-vs2026` for every push to `master` and uploads
  `MacFile.zip` + PDB, plus the WiX `MacFile-<ver>.msi`,
  as artifacts. Since the project cannot be
  built *natively* on Linux/macOS, CI is the authoritative build check for
  merges — but do not treat "can't compile here" as a reason to skip
  verification: use the SSH dev loop below.

## Building and testing from Linux (dev loop)

Although MSVC-only, the code **can and should be built and smoke-tested from
the Linux workstation** over SSH — there is a Windows build machine reachable
with the key already installed (no password prompts).

- **Primary command:** `winboat-deploy/ssh-dev.sh` — tars the working tree to
  `C:\dev\repo` on the Windows box, runs MSBuild **Release | x64** remotely
  (incremental; compiler errors stream to the Linux terminal), stages the
  build to `C:\dev\server`, restarts the server, and finishes with a live
  **DSIGetStatus probe** on TCP 548. `PASS` = compiled, deployed, and serving.
- **Target:** default `AFP_GUEST=michaelc@127.0.0.1` (loopback implies the
  WinBoat guest's port-forward on 2222). For the real Windows dev box use
  `AFP_GUEST=<user>@<box-ip>` (port 22 implied) plus `--probe-host <box-ip>`.
- **Flags:** `--msi` (also builds the WiX installer, landed in
  `~/afp-deploy/dev/out/`), `--msi-install` (major-upgrade install in the
  guest), `--timeout SECS`, `--probe-host IP`.
- **Quick connectivity check:** `ssh -p 2222 -o BatchMode=yes
  michaelc@127.0.0.1 'echo CONNECTED'`.
- Full setup/operational details (one-time provisioning, guest quirks,
  fallbacks): `winboat-deploy/README.md`.

Prefer this loop over "build not possible" conclusions; GitHub Actions remains
the authoritative check for merges to `master`.

## Git workflow

- **Never commit or push without explicit user approval.** Build, deploy to
  the dev guest, and iterate freely, but leave changes staged-or-unstaged in
  the working tree and ask before any `git commit` or `git push` — including
  "follow-up" or "fixup" commits on top of previously approved work.

## Code conventions

- Include `"stdafx.h"` as the first line of every `.cpp` (PCH requirement).
- Unicode build: keep everything wide-char (`TEXT()`, `wchar_t`, Win32 W APIs).
- The AFP/DSI wire format is **big-endian**. When reading/writing network
  buffers, swap with the `byte_swap.h` template or `htonl`/`ntohl` family.
  Never assume host byte order.
- All request/response parsing goes through `afp_net_buffer`, which performs
  bounds checks. Keep new protocol code bounds-checked; the project has been
  deliberately hardened against malformed requests (see `Changes.txt` 2.0.3).
- SAL-style `IN`/`OUT` parameter annotations are used widely — keep them.
- Return `AFPERROR` codes and map them to AFP error replies as existing code
  does; avoid exceptions in network paths.
- Naming: files and free functions are `lower_snake_case` with an `afp`
  prefix; AFP commands are `FPCommandName` (e.g. `FPGetSrvrParms`); some
  legacy classes use an `LAFP*` prefix.
- Keep new code warning-free at `/W4` + `/sdl`.
- Event-log messages come from the macros in the generated `afpEvents.h`.

## Gotchas

- Batch scripts (`*.cmd`, `*.bat`) **must stay CRLF** — enforced via
  `.gitattributes`; `cmd.exe` misbehaves with LF-only files (goto/labels).
- The project is x64-only (since 2.3.0; 32-bit x86 is no longer supported) and
  links the vendored 64-bit `libcrypto64MT.lib` — the Shining Light
  Productions Win64 OpenSSL 1.1.1w import library for `libcrypto-1_1-x64.dll`.
- Releases: bump `VERSION_MAJOR/MINOR/REVISION` in `afpServer/version.h`,
  add a `Changes.txt` entry, and build Release.
- Git history predates 2026: the source was imported from a private Azure
  DevOps repo (`../AfpServer` at import time) via history merge.

## Verifying changes

1. Build (and smoke-test) locally via the SSH dev loop:
   `winboat-deploy/ssh-dev.sh` — see "Building and testing from Linux (dev
   loop)" above. A `PASS` means Release | x64 compiled clean and the restarted
   build answers DSI GetStatus.
2. Ensure the CI build passes (push to a branch and check Actions, or rely on
   the `master` workflow after merge).
3. Run the Python end-to-end tests in `tests/` against the running dev guest
   (point `AFP_HOST` at the forwarded server; see `tests/README.md`). They
   cover DHCAST128 hardening, xattrs, line conversion, permissions, name
   handling, and Finder-like mount stress.
4. Runtime behavior beyond the scripts must be tested manually on Windows
   against Mac clients (SheepShaver / BasiliskII / Mac OS X "Connect to
   Server").
