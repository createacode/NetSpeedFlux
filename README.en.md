# NetSpeedFlux

> Real-time network speed overlay for the Windows taskbar · Qt6 / C++17 / Win32

A small, transparent overlay sits on top of the taskbar and shows live upload (↑ orange) and
download (↓ blue) throughput. Clicks pass through to the taskbar; the overlay never takes focus
and never appears in Alt+Tab.

Current version: **1.3.7**

---

## Features

- **Live throughput** — refreshed every second, four unit modes (auto / KB / MB / short)
- **Taskbar overlay** — transparent, always-on-top, click-through, follows taskbar position and DPI
- **Automatic contrast** — built-in themes brighten or darken the text when the taskbar background
  is too similar, so it stays readable in both light and dark themes
- **Fullscreen aware** — hides itself when a fullscreen app or presentation mode is detected
- **Tray icon** — left click opens the window, right click menu, live tooltip
- **Custom typography** — family, size, bold, italic and a custom colour
- **Persistent settings** — JSON config with window position restored only when it is still on screen
- **Autostart** — `HKCU\Run` entry with a properly quoted command line (`--min`)
- **Single instance** — launching again activates the running window
- **Daily logs** — keeps the last 30 days

---

## Architecture

```
src/
├── main.cpp              entry point: single instance, paths, CLI flags
├── app.{h,cpp}           controller: wiring, signals, single exit path
├── net_monitor.{h,cpp}   sampling via GetIfTable2
├── overlay.{h,cpp}       taskbar overlay widget
├── tray.{h,cpp}          system tray (icon object created once)
├── settings_window.*     settings UI
├── config.{h,cpp}        config struct, validation, atomic I/O
├── app_paths.{h,cpp}     portable vs per-user directory selection
├── logger.{h,cpp}        daily log files with pruning
├── speed_format.*        speed formatting (pure logic, unit tested)
├── theme.{h,cpp}         theme palette + WCAG contrast helpers
├── win_util.{h,cpp}      taskbar geometry, fullscreen state, autostart
└── single_instance.*     named mutex + broadcast message
```

### Sampling

There is exactly **one** data source: `GetIfTable2` (`MIB_IF_ROW2`).

- `InOctets` / `OutOctets` are **64-bit** counters, so there is no 4 GiB wraparound
- Deltas are computed **per interface** (keyed by `InterfaceLuid`), so adapters coming and going
  cannot corrupt other counters
- Rate = delta ÷ **measured elapsed time** (`QElapsedTimer`), so timer jitter, sleep/resume and
  debugging breakpoints do not produce spikes
- Loopback and tunnel interfaces (Teredo / ISATAP / VPN) are skipped to avoid double counting

Earlier releases used a "PDH + GetIfTable" dual-source fallback. Each source kept its own baseline,
so switching between them attributed dozens of seconds of traffic to a single one-second sample; the
32-bit counter sum also wrapped on gigabit links. That design was removed entirely.

### Overlay

- Window flags: `FramelessWindowHint | Tool | WindowStaysOnTopHint | WindowDoesNotAcceptFocus`
- `WA_TranslucentBackground` plus `WM_NCHITTEST → HTTRANSPARENT` give transparency and click-through
- `WM_SHOWWINDOW` / `WM_WINDOWPOSCHANGING` are **not** intercepted; the app does not fight Explorer
- A **1 Hz** light-weight check handles visibility, taskbar rectangle and DPI; the window is moved
  only when something actually changed
- `EVENT_SYSTEM_FOREGROUND` makes fullscreen transitions immediate; `TaskbarCreated` handles an
  Explorer restart
- All geometry is computed in **Qt logical coordinates** (the taskbar rect is converted with the
  `devicePixelRatio` of the screen it sits on)

---

## Building

### Requirements

| Component | Version |
|-----------|---------|
| Qt | ≥ 6.5 (Widgets) |
| CMake | ≥ 3.21 |
| Compiler | MSVC 2022 or MinGW-w64 GCC ≥ 13 |

### MSYS2 / MinGW

```bash
pacman -S mingw-w64-ucrt-x86_64-qt6-base \
          mingw-w64-ucrt-x86_64-gcc \
          mingw-w64-ucrt-x86_64-cmake \
          mingw-w64-ucrt-x86_64-ninja

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

### MSVC

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:\Qt\6.8.0\msvc2022_64"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

### Packaging

```powershell
cmake --install build --prefix dist --config Release
```

`install` uses `qt_generate_deploy_app_script` to deploy the Qt runtime and plugins automatically —
no manual DLL copying.

---

## Where data lives

The app prefers **portable mode**: when the directory containing the executable is writable, the
configuration and logs stay next to it.

| Mode | Config | Logs |
|------|--------|------|
| Portable (writable executable dir) | `<exe>/config/settings.json` | `<exe>/logs/YYYY-MM-DD.log` |
| Per-user (Program Files and other read-only locations) | `%LOCALAPPDATA%/XAF/NetSpeedFlux/settings.json` | `%LOCALAPPDATA%/XAF/NetSpeedFlux/logs/` |

When moving from a portable copy to an installed one, the old configuration is copied into the user
directory on first start (existing files are never overwritten).

### Settings file

```json
{
  "cfgVersion": 2,
  "ff": "Microsoft YaHei UI",
  "fs": 12,
  "fb": false,
  "fi": false,
  "fc": "#00ff88",
  "theme": "霓虹",
  "sti": true,
  "sto": true,
  "tp": "right",
  "um": "auto",
  "fsc": "hide",
  "as": false,
  "wx": 490, "wy": 205, "ww": 720, "wh": 400
}
```

| Key | Meaning |
|-----|---------|
| `ff` / `fs` / `fb` / `fi` | font family / size in pt (8-48) / bold / italic |
| `fc` | text colour used by the "custom" theme |
| `theme` | neon / dark / bright / cyber / plain-white / custom |
| `sti` / `sto` | show tray icon / show overlay |
| `tp` | overlay position: left / center / right |
| `um` | units: auto / KB / MB / short |
| `fsc` | behaviour in fullscreen: hide / show |
| `as` | start with Windows |
| `wx/wy/ww/wh` | main window geometry (also written for downgrade compatibility) |

Writes go through `QSaveFile` (atomic replace). A corrupted file is renamed to
`settings.json.bak` and the app starts with defaults instead of failing.

---

## Command line

| Argument | Behaviour |
|----------|-----------|
| (none) | show the main window, tray icon and overlay |
| `--min` / `-m` | tray icon and overlay only (used by autostart) |

---

## Tests

```bash
ctest --test-dir build --output-on-failure
```

Coverage:

- `speed_format` — boundary values, precision and width samples for all unit modes
- `config` — round trip, clamping of out-of-range values, corrupted JSON, legacy key migration
- `theme` — palette correctness (dark/light are no longer swapped), WCAG contrast, readability of
  built-in themes, custom colour left untouched

CI (GitHub Actions) configures, builds, tests and packages on Windows + MSVC + Qt 6.8.

---

## Known limitations

1. **Primary taskbar only** — only `Shell_TrayWnd` is handled; `Shell_SecondaryTrayWnd` is not
2. **Vertical taskbar** — best-effort positioning above the notification area, not verified on hardware
3. **Windows 11** — the taskbar is a XAML rewrite; right-side placement depends on the
   `TrayNotifyWnd` anchor and falls back to a fixed offset if the internal structure changes
4. **Unsigned binary** — some antivirus products flag unsigned resident programs
5. The overlay is click-through by design; all interaction goes through the tray icon or main window

---

## License

See [LICENSE](LICENSE).
