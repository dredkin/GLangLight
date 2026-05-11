# GLanglight

> A lightweight Windows system-tray application that watches the active keyboard input language and sets Logitech RGB keyboard lighting to match.

![Windows 10/11](https://img.shields.io/badge/Windows-10%2F11-0078D6?logo=windows)
![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus)
![MSVC 2022](https://img.shields.io/badge/MSVC-2022-5C2D91?logo=visualstudio)
![License: MIT](https://img.shields.io/badge/License-MIT-green)

---

## Features

- **Automatic current keyboard language detection** — polls the foreground window's keyboard layout every 200 ms via `GetKeyboardLayout()`
- **Per-language LED colour** — configurable colour per installed language; defaults:
  | Order | Language | Default colour |
  |---|---|---|
  | 1st | (any) | White |
  | 2nd | (any) | Red |
  | 3rd | (any) | Blue |
  | 4th+ | (any) | Random bright colour |
- **Settings dialog** — tray right-click -> Settings... opens a dialog listing all installed keyboard languages with colour swatches; double-click or "Change colour..." to open the Windows colour picker
- **Persistent settings** — colours saved to `HKCU\Software\GLanglight\LangColors` registry key
- **Dynamic tray icon** — the system tray icon background changes to match the current keyboard colour; the "G" letter is black on light backgrounds and white on dark ones (ITU-R BT.709 luminance)
- **Self-contained EXE** — MSVC C++ runtime is statically linked (`/MT`); no Visual C++ Redistributable needed

---

## Screenshots

| Tray icon (red = Russian) | Settings dialog |
|---|---|
| *(red icon with white G)* | *(ListView with language names and colour swatches)* |

---

## Requirements

| Requirement | Notes |
|---|---|
| **Windows 10 / 11** | x64 |
| **Logitech G HUB** or **LGS** | Must be running; provides `LogitechLEDLib.dll` |
| **Logitech RGB keyboard** | Any keyboard supported by the Logitech LED SDK |

> The application runs without G HUB (it will ask at startup), but lighting will not be controlled until G HUB is started.

---

## Building from source

### Prerequisites

- Visual Studio 2022 (Community / Professional / Build Tools) with the **C++ desktop workload**
- *Or* CMake >= 3.20 + MSVC (via VS Build Tools)

### Visual Studio 2022

1. Open `GLanglight.sln`
2. Select **Release | x64** in the toolbar
3. **Build → Build Solution** (`Ctrl+Shift+B`)

Output: `bin\Release\GLanglight.exe`

### CMake

```bat
cmake -B build -A x64
cmake --build build --config Release
```

Output: `build\Release\GLanglight.exe`

## Settings storage

Colours are stored per `PRIMARYLANGID` under:

```
HKEY_CURRENT_USER\Software\GLanglight\LangColors
```

Values are `DWORD` entries named by 4-hex-digit language ID (e.g. `0009` = English, `0019` = Russian) storing a `COLORREF` value.

---

## Debug build

The Debug configuration allocates a console window and writes timestamped diagnostic output for every SDK call, language change and tray event. All `DBG_LOG` calls compile to nothing in Release.

---

## License

MIT License — see [LICENSE](LICENSE) for details.

The Logitech LED SDK (`LogitechLEDLib.h`, `LogitechLEDLib.lib`) is copyright Logitech and is redistributed here solely as a build dependency under Logitech's SDK terms.
