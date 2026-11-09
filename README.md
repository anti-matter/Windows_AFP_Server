# Windows AFP Server

[![Build](https://github.com/anti-matter/Windows_AFP_Server/actions/workflows/build.yml/badge.svg)](https://github.com/anti-matter/Windows_AFP_Server/actions/workflows/build.yml)

This is a AFP3.2 over TCP/IP server that runs on Windows clients (up to Windows 11 has been tested) that allows file sharing to vintage Macintosh clients. Classic Mac OS clients show the server's own custom icon (served in `FPGetSrvrInfo`) instead of the generic network-server icon.

See the [Wiki](https://github.com/anti-matter/Windows_AFP_Server/wiki) for additional information and instructions. Report bugs via [GitHub Issues](https://github.com/anti-matter/Windows_AFP_Server/issues).

<img width="1012" height="908" alt="image" src="https://github.com/user-attachments/assets/7bb80ab6-6f30-4136-8d5c-249a4ccdae03" />

The project is now open source — the full source code lives in this repository.

## Repository layout

- `afpServer/` — the server source code (Visual Studio C++ project). Protocol
  and server code is under `afpServer/afp-source/`; a vendored OpenSSL 1.1
  dependency lives in `afpServer/deps/`.
- `AfpServerInstaller/` — WiX v7 installer source (`product.wxs`); builds a
  single-file MSI (major-upgrades legacy Installer Projects installs). Not
  part of CI; see `AGENTS.md` for the `wix` build commands.
- `deploy.cmd` / `install.cmd` — packaging and post-install scripts.
- `AGENTS.md` — notes for contributors (and AI coding agents) on building,
  conventions, and gotchas.

## Building

The server is a 64-bit (x64) Win32 application and builds on **Windows** with
**Visual Studio 2026** (v145 toolset), the *Desktop development with C++*
workload, and a Windows 10/11 SDK.

1. Open `afpServer.sln` in Visual Studio and build **Release | x64**, or from
   a Developer Command Prompt:

   ```
   msbuild afpServer.sln /t:afpServer /p:Configuration=Release /p:Platform=x64
   ```

2. The Release build's post-build step runs `deploy.cmd`, producing
   `PersonalAfpServer.zip` in the repo root containing the server, the
   `libcrypto-1_1-x64.dll` it needs, its event-log/counter manifest, and
   `install.cmd`.

Every push to `master` is built automatically by
[GitHub Actions](https://github.com/anti-matter/Windows_AFP_Server/actions/workflows/build.yml),
which uploads the built zip as an artifact — grab a prebuilt binary from a
workflow run if you don't want to compile.

## Installing

Unzip a build, run `install.cmd` as Administrator to register the event-log
and Performance Monitor providers, then launch `AfpServer.exe`. Configure
shares via the system tray icon. The Wiki has full instructions.

## License

MIT — see [License.md](License.md).
