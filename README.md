# MacFile — an Apple Filing Protocol server for Windows

[![Build](https://github.com/anti-matter/Windows_AFP_Server/actions/workflows/build.yml/badge.svg)](https://github.com/anti-matter/Windows_AFP_Server/actions/workflows/build.yml)

An Apple Filing Protocol (AFP) file server for Windows, designed to serve
vintage Macintosh clients (System 7 with AppleShare Client 3.7.x through
Mac OS 9, Mac OS X, SheepShaver, and BasiliskII) over TCP/IP port 548.
The current version is **2.4.5** and the project is MIT-licensed by
Michael J. Conrad.

A companion AFP server for **Haiku OS** — sharing the same protocol
heritage, server icon, and Mac-fork conventions — lives at
[MacFile_For_HaikuOS](https://github.com/anti-matter/MacFile_For_HaikuOS).

See the [Wiki](https://github.com/anti-matter/Windows_AFP_Server/wiki) for
setup guides and screenshots. Report bugs via
[GitHub Issues](https://github.com/anti-matter/Windows_AFP_Server/issues).

<img width="1012" height="908" alt="image" src="https://github.com/user-attachments/assets/7bb80ab6-6f30-4136-8d5c-249a4ccdae03" />

## Overview

`AfpServer.exe` is a single 64-bit Win32 tray application: a Windows service
in all but name, with a Windows 11 Fluent-style configuration window
(light/dark theming, per-monitor DPI), Windows event-log and Performance
Monitor integration, and shares backed directly by NTFS folders. It
implements **AFP 2.2 through 3.2** — each client negotiates its own version
during the `FPLogin` exchange.

## AFP Protocol Support (2.2 – 3.2)

| Feature | AFP 2.2 | AFP 3.0 | AFP 3.1 | AFP 3.2 |
| --- | --- | --- | --- | --- |
| Long filenames (Mac Roman) | Yes | Yes | Yes | Yes |
| Unicode (UTF-8) names | – | Yes | Yes | Yes |
| File IDs / `FPResolveID` | Yes | Yes | Yes | Yes |
| Resource forks (NTFS streams) | Yes | Yes | Yes | Yes |
| Finder Info blocks (32 B) | Yes | Yes | Yes | Yes |
| Extended attributes (`FP*ExtAttribute`) | – | – | – | Yes |
| Byte-range locking (`FPByteRangeLock(Ext)`) | Yes | Yes | Yes | Yes |
| Sleep notification (`FPZzzz`) | – | – | Yes | Yes |
| Server → client messages (`FPGetSrvrMsg`) | Yes | Yes | Yes | Yes |
| 64-bit free/total bytes | clamped | clamped | clamped | Yes |

Full command coverage: fork I/O (`FPRead/Write/FlushFork` + Ext), all three
directory-enumeration flavors (`FPEnumerate`, `Ext`, `Ext2`), file/directory
create/delete/rename/move/copy with attribute preservation,
`FPGetFileDirParms`/`FPSetFileDirParms`, volume open/close, sessions
(`FPLogin`/`FPLoginExt`/`FPContLogin`/`FPLogout`/`FPMapName`/`FPMapID`),
desktop database (`FPOpenDT`, icons incl. the non-standard `FPAddIcon` 192,
comments, APPL mappings), and extended attributes. Every request is parsed
through bounds-checked network buffers — the server is deliberately hardened
against malformed packets.

### Volume Parameters

Each shared volume reports to clients:

- **File IDs** — stable per server run, resolvable with `FPResolveID`
- **Unicode names** — full UTF-8 filenames on the wire for AFP 3.x clients
- **Extended attributes** — advertised to AFP 3.2+ clients
- **Blank access privileges** and **default privileges from parent**
- **No exchange files** — prevents AFP rename-from-volume conflicts
- **Read-only** — per-share flag (enforced server-side) or a genuinely
  read-only NTFS volume
- **64-bit free/total space** (32-bit values clamped for 2.x clients)

### Authentication Methods (UAMs)

| UAM | Description |
| --- | --- |
| **Guest** (`No User Authent`) | Unauthenticated access (when allowed); maps to the built-in Guest account |
| **Cleartxt passwrd** | Plaintext exchange (can be disabled server-wide) |
| **DHCAST128** | Diffie-Hellman key exchange with CAST-encrypted passwords; degenerate DH keys are rejected |
| **XOR** (Vigenère) | Legacy support for the AppleShare IP browser |

Login names map to **Windows user accounts** — file ownership, group
membership, and access decisions follow the NTFS ACLs (verified by the
end-to-end permissions tests). Per-volume passwords from legacy
configurations are no longer advertised, since the server never enforced
them.

### "Require Encrypted Logon" and guest access

The configuration UI can force encrypted (DHCAST128) logins only, and
toggle guest access, per the Security card on the main window. A server-wide
login message is delivered to each client on login, and a
**Send Message** dialog pushes attention messages to connected Macs
(classic "The server says…" alerts).

## Mac Filesystem Semantics on NTFS

- **Resource forks** live in the NTFS alternate data stream
  `:Afp_AfpResource`; Finder Info in the `AFP_AfpInfo` stream. Both are
  created lazily and survive copy-in from the Windows side.
- **Special `.res` handling** for source-control workflows: a `.res` file's
  *resource fork is its main stream* and its data fork is denied — classic
  resource-only files round-trip through Git as plain binary blobs.
- **Filename mapping** — Mac-illegal characters (`/ " * <> | : \`) are
  mapped per directory and recorded in a `:Afp_CharMap` stream so the
  original Mac name round-trips exactly. `:` maps to `-` Finder-style.
- **Finder type/creator codes** are assigned from the file extension
  (CodeWarrior: `.c/.h/.cpp/.prj/.mcp/.cwlib/.ppob/.err`; MPW `.o`;
  StuffIt/HQX/ZIP archives; disk images; `.txt`; …) so files double-click
  correctly in the Finder.
- **Line-ending conversion** — CR ↔ LF translation on the wire for
  file extensions you configure (Mac text files stored as-is on disk).
- **Desktop database** — volume icons, comments, and APPL mappings persist
  per share in `afp_desktop.db` in the share root (v2 format, legacy
  migration supported).
- **Volume change notifications** — NTFS change notifications drive AFP
  attention broadcasts so connected clients see new/removed files without
  rescanning.

### Custom Server Icon

The server advertises its own 32×32 monochrome icon (`GetSrvrInfo`
`VolumeIconAndMask`, compiled in as static data), so the Chooser and Finder
show a recognizable MacFile icon instead of the generic server icon.

## Architecture

```
AfpServer.exe (Win32, x64)
├── Tray app + Fluent config window (ui/)      ← registry-backed settings
├── TCP listener :548 → dsi_connection thread per client (dsi/)
│     └── AFP dispatch (fp-core/) ── version-gated command handlers
│           ├── volumes / file index / char map (fp-classes/)
│           ├── DHX login, server info, users (fp-imp/)
│           └── desktop DB, ext attrs, fork I/O
├── Housekeeping thread — tickles, dead-session reaping (120 s idle)
├── Change-notify thread — NTFS → AFP attention broadcasts (server-core/)
└── Windows integration — event log, PerfMon counters, tokens
```

Settings (shares, guest access, encryption, line-conversion extensions,
login message) persist under `HKCU\Software\MacFile\afpserver`.

## Building

The server builds on **Windows** with **Visual Studio 2026** (v145
toolset), the *Desktop development with C++* workload, and a Windows 10/11
SDK. C++20/C++17, `/W4` clean.

1. Open `afpServer.sln` in Visual Studio and build **Release | x64**, or
   from a Developer Command Prompt:

   ```
   msbuild afpServer.sln /t:afpServer /p:Configuration=Release /p:Platform=x64
   ```

2. The Release post-build step runs `deploy.cmd`, producing
   `MacFile.zip` in the repo root containing the server, the
   `libcrypto-1_1-x64.dll` it needs, its event-log/counter manifest, and
   `install.cmd`.

Every push to `master` is built automatically by
[GitHub Actions](https://github.com/anti-matter/Windows_AFP_Server/actions/workflows/build.yml),
which uploads the built zip as an artifact — grab a prebuilt binary from a
workflow run if you don't want to compile. Contributor notes (conventions,
gotchas, the Linux→Windows SSH dev loop) are in
[AGENTS.md](AGENTS.md).

## Installing

- **MSI**: `MacFile-<version>.msi` installs the service binaries,
  registers the event-log and Performance Monitor providers, and creates the
  shortcuts. Major-upgrades older installs.
- **Zip**: unzip a build, run `install.cmd` as Administrator once (registers
  the event-log and PerfMon providers via `wevtutil`/`lodctr`), then launch
  `AfpServer.exe`.

Configure shares from the tray icon or the main window. Full instructions
are on the [Wiki](https://github.com/anti-matter/Windows_AFP_Server/wiki).

## Testing

[`tests/`](tests) contains stdlib-only Python 3 AFP/DSI clients that run
against a live server (no test framework needed):

| Script | Covers |
| --- | --- |
| `afp_stress.py` | Finder-like mount/enumerate loops |
| `afp_dhx_probe.py` | DHCAST128 hardening (degenerate/short DH keys must fail cleanly) |
| `afp_xattr_test.py` | AFP 3.2 extended attributes / stream enumeration |
| `afp_lineconv_test.py` | CR↔LF line-conversion regressions |
| `afp_permissions_test.py` | Ownership/group/access model vs NTFS ACLs, `FPMapName`/`FPMapID` round-trip |
| `afp_colonname_test.py` | Colon and illegal-character filename handling |
| `afp_voldate.py` | Mac 1904-epoch date reporting |

See [`tests/README.md`](tests/README.md) for configuration.

## License

MIT — see [License.md](License.md).
