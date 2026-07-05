# ShackLog

Standalone ham radio logbook for Windows / Linux / macOS.

ShackLog is a Qt 6 desktop application that keeps a SQLite-backed QSO log and
pulls live frequency / mode information from any radio that speaks the **TCI**
(Transceiver Control Interface) protocol over WebSocket — AetherSDR,
ExpertSDR2, SunSDR, and others. It fills in callsign details automatically,
tracks awards, watches the DX cluster, and exports to **ADIF 3.x** and
**Cabrillo 3.0**.

It also ships an optional headless **server** for multi-station operating —
mirroring an N3FJP network, ingesting WSJT-X spots, and serving a live
Field Day score over HTTP.

## Download

Pre-built binaries — including a Windows installer — are on the
[**Releases page**](https://github.com/nigelfenton/shacklog/releases/latest):

- **Windows installer** — `ShackLog-Setup-<version>-windows-x64.exe` (double-click)
- **Windows portable zip** — `ShackLog-<version>-windows-x64.zip` (unzip, run `ShackLog.exe`)
- **Linux AppImage** — `ShackLog-<version>-linux-x86_64.AppImage` (`chmod +x` and run)
- **macOS DMG (Apple Silicon)** — `ShackLog-<version>-macos-arm64.dmg`

Or build from source — see [Build](#build).

## Features

### Logging
- **Live freq / band / mode auto-fill** from a TCI server (default `ws://127.0.0.1:40001`), with auto-reconnect
- Quick QSO entry: callsign + RST sent/received + comment, then `SAVE`
- Real-time **duplicate-check** warning as you type a callsign
- Full-fidelity QSO editor (Core / Other Station / My Station / Contest / Notes & QSL)
- Filterable QSO table — text search across call/name/QTH/grid/comment, plus band / mode / contest selectors
- **Multi-operator logs** — one database per callsign, live *Switch Operator/Log*

### Callsign lookup
Three-tier autofill of an empty QSO's name / QTH / grid / state / country:
1. **Worked-before** — reuses details from your previous QSO with that station
2. **cty.dat** — bundled offline AD1C prefix resolver (country / continent / CQ / ITU zones)
3. **Online** — QRZ.com (XML, paid), HamQTH (free), and callook.info (US, no account)

### Awards & spotting
- **Awards panel** (Tools → Awards): DXCC / WAS / WAC / WAZ / grids, worked vs confirmed, chase lists
- **DX cluster** client with configurable login suffix and duplicate-login handling
- **POTA** spot integration
- **"How far?"** button — opens a PSK Reporter map of who's hearing you, filtered to your band
- **Section map** (Maps menu) — native ARRL/RAC section map, N3FJP-style

### Import / export
- **ADIF import** (File → Import ADIF) — whole-file, deduplicated, fast (proven on 16k+ record logs)
- **ADIF 3.1.4 export** with the full standard field set
- **Cabrillo 3.0 export** with operator-defined header categories

### Server (optional, for multi-station / Field Day)
A headless companion (`shacklog-server`) that:
- Mirrors a remote **N3FJP** network so its QSOs land in your log
- Ingests **WSJT-X** "Secondary UDP" spots (udp/1100) straight into the log
- Serves a **live ARRL Field Day score** breakdown over HTTP (`/api/score`)
- Exposes a REST API (`/api/qsos`) with schema versioning, audit trail, and soft-delete

All operator settings (MY_CALL, grid, power, TCI host/port, Cabrillo headers,
lookup credentials, cluster login) live inside the database — no external
config files.

## Build

Requirements:
- Qt 6.2 or newer with the `Sql` and `WebSockets` modules
- CMake 3.20+
- A C++17 compiler (MSVC, GCC, or Clang)

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The resulting binaries are `build/ShackLog` (the logbook) and
`build/shacklog-server` (the optional server), plus `.exe` on Windows.
On Windows, `build.bat` also handles the MSVC environment setup.

## Database location

The QSO database is auto-created on first run (one file per operator callsign):

| OS      | Path                                                     |
|---------|----------------------------------------------------------|
| Windows | `%LOCALAPPDATA%\ShackLog\shacklog-<CALL>.sqlite`         |
| Linux   | `~/.local/share/ShackLog/shacklog-<CALL>.sqlite`         |
| macOS   | `~/Library/Application Support/ShackLog/shacklog-<CALL>.sqlite` |

Back this file up if you care about your log — it is the entire database.

## Documentation

- [CHANGELOG.md](CHANGELOG.md) — what changed in each release
- [ROADMAP.md](ROADMAP.md) — what's planned next

## License

MIT. See [LICENSE](LICENSE).

73 de G0JKN / W3.
