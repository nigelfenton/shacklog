# ShackLog

Standalone ham radio logbook for Windows / Linux / macOS.

ShackLog is a small Qt 6 desktop application that keeps a SQLite-backed QSO
log and pulls live frequency / mode information from any radio that speaks
the **TCI** (Transceiver Control Interface) protocol over WebSocket —
AetherSDR, ExpertSDR2, SunSDR, and others.

It exports to **ADIF 3.x** for general logbook use and **Cabrillo 3.0** for
contest submission.

## Download

Pre-built binaries — including a Windows installer — are on the
[**Releases page**](https://github.com/nigelfenton/shacklog/releases/latest):

- **Windows installer** — `ShackLog-Setup-<version>-windows-x64.exe`
  (double-click to install)
- **Windows portable zip** — `ShackLog-<version>-windows-x64.zip`
  (unzip and run `ShackLog.exe`)
- **Linux AppImage** — `ShackLog-<version>-linux-x86_64.AppImage`
  (`chmod +x` and run)
- **macOS DMG (Apple Silicon)** — `ShackLog-<version>-macos-arm64.dmg`

Or build from source — see [Build](#build) below.

## Features

- Live freq / band / mode auto-fill from a TCI server (default
  `ws://127.0.0.1:40001`)
- Quick QSO entry: callsign + RST sent / RST received + comment, then `SAVE`
- Real-time duplicate-check warning as you type a callsign
- Contest mode with auto-incrementing serial numbers and free-form exchange
  fields
- ADIF 3.1.4 export with full standard field set
- Cabrillo 3.0 export with operator-defined header categories
- Filterable QSO table (text search across call/name/QTH/grid/comment, plus
  band / mode / contest selectors)
- All operator settings (MY_CALL, MY_GRIDSQUARE, MY_STATE, default TX power,
  Cabrillo header) live inside the database, no external config files

## Build

Requirements:
- Qt 6.2 or newer with the `Sql` and `WebSockets` modules
- CMake 3.20+
- A C++17 compiler (MSVC, GCC, or Clang)

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The resulting binary is `build/ShackLog` (or `build/ShackLog.exe` on Windows).

## Database location

The QSO database is auto-created on first run at:

| OS      | Path                                                          |
|---------|---------------------------------------------------------------|
| Windows | `%LOCALAPPDATA%\ShackLog\shacklog.sqlite`                     |
| Linux   | `~/.local/share/ShackLog/shacklog.sqlite`                     |
| macOS   | `~/Library/Application Support/ShackLog/shacklog.sqlite`      |

Back this file up if you care about your log — it is the entire database.

## License

MIT.  See [LICENSE](LICENSE).

73 de G0JKN / W3.
