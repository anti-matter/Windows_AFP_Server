# AGENTS.md

Guidance for AI coding agents (and humans) working on this repository.

## What this project is

**Personal AFP Server for Windows** — an Apple Filing Protocol (AFP 3.2) file
server for Windows, written in C++. It lets vintage Macintosh clients
(System 7–Mac OS 9, Mac OS X, SheepShaver, BasiliskII) access Windows shared
folders over TCP/IP (DSI on port 548). The server is a single 32-bit Win32 GUI
executable: a tray-icon app with dialog-based share configuration. It is *not*
an MFC or .NET project — plain Win32 API.

User-facing documentation lives in the
[wiki](https://github.com/anti-matter/Windows_AFP_Server/wiki). The AFP
protocol reference is `afpServer/ref/afp3XX.pdf` (Apple's AFP 3.x spec) —
consult it before changing protocol behavior.

## Repository layout

- `afpServer.sln` — Visual Studio solution.
- `afpServer/afpServer.vcxproj` — the server project (Win32/x86 only).
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
  - `ui/` — dialogs: about, add-share, send-message.
- `afpServer/afpServer.cpp` — entry point, tray icon, main window.
- `afpServer/resources/` — icons, `afpserver.rc`, `resource.h`, and
  **generated** event-log/counter files (see Build).
- `afpServer/afpServer.man` — source of truth for Windows event-log messages
  and PerfMon counters.
- `afpServer/deps/openssl-1.1/` — vendored OpenSSL 1.1 (headers,
  `libcrypto32MT.lib`, and the `libcrypto-1_1.dll` shipped beside the exe).
- `AfpServerInstaller/` — Visual Studio Installer Projects (`.vdproj`) MSI
  setup, built manually from the VS IDE.
- `deploy.cmd` — Release post-build step; collects `AfpServer.exe`,
  `afpServer.man`, `install.cmd`, and `libcrypto-1_1.dll` into
  `PersonalAfpServer/` and zips it to `PersonalAfpServer.zip` (repo root).
- `install.cmd` — run on the target machine after unzipping; registers /
  unregisters (`wevtutil`, `lodctr`) the event-log and PerfMon providers.
- `afpServer/version.h` — version numbers; `afpServer/Changes.txt` — change log.

## Build

Windows only. Requires **Visual Studio 2026** (platform toolset **v145**) with
the *Desktop development with C++* workload and a Windows 10/11 SDK. The
project uses `stdcpp20` / `stdc17`, Unicode, `/W4`, SDL checks, and the
precompiled header `afpServer/stdafx.h`.

From a *Developer Command Prompt*:

```
msbuild afpServer.sln /t:afpServer /p:Configuration=Release /p:Platform=x86
```

(In the IDE, open `afpServer.sln` and pick **Release | x86** — the solution
platform is `x86`, the project platform is `Win32`.)

Notes:

- The **pre-build event** regenerates `resources/afpEvents.h`, `afpEvents.rc`,
  `afpCounters.h`, and `afpCounters.rc` from `afpServer.man` using the SDK
  tools `mc.exe` and `ctrpp`. Edit `afpServer.man`, never the generated files.
- The Release **post-build event** copies the OpenSSL DLL and runs
  `deploy.cmd` (produces `PersonalAfpServer.zip`; both are gitignored).
- The `.vdproj` installer is **not** part of CI; build it from Visual Studio
  with the *Microsoft Visual Studio Installer Projects* extension.
- **CI** (`.github/workflows/build.yml`) builds `Release | x86` on
  `windows-2025-vs2026` for every push to `master` and uploads
  `PersonalAfpServer.zip` + PDB as artifacts. Since the project cannot be
  built on Linux/macOS, **CI is the authoritative build check**.

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
- A few files on disk are legacy leftovers **not referenced by the project**:
  `fp-classes/DesktopDatabase.cpp`, `fp-classes/LAFPLock.*`,
  `fp-classes/LAFPObject.h`, `fp-classes/LArray.cpp`, `fp-imp/attention.*`,
  `helpers/bswap.c`. Editing them has no effect on the build.
- `resources/RCa31236`, `resources/RDa31236`, and `resources/afpEventsTEMP.BIN`
  are stray leftovers from past resource-tool runs; unused.
- The project is x86-only and links the 32-bit vendored `libcrypto32MT.lib`.
  Do not "modernize" to OpenSSL 3 without a ported 32-bit library.
- Releases: bump `VERSION_MAJOR/MINOR/REVISION` in `afpServer/version.h`,
  add a `Changes.txt` entry, and build Release.
- Git history predates 2026: the source was imported from a private Azure
  DevOps repo (`../AfpServer` at import time) via history merge.

## Verifying changes

1. Ensure the CI build passes (push to a branch and check Actions, or rely on
   the `master` workflow after merge).
2. Runtime behavior must be tested manually on Windows against Mac clients
   (SheepShaver / BasiliskII / Mac OS X "Connect to Server"); there are no
   automated tests.
