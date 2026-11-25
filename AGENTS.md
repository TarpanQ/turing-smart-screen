# AGENTS.md

## Project
- System monitor for USB-C/TFT displays (Turing, XuanFang, Kipye, WeAct) with 3 programs: the system monitor (`main`), the configuration GUI (`configure`) and the theme editor (`theme-editor`), plus a display API example (`simple-program`).
- Target platforms: **Linux (Ubuntu 26.04 LTS)**, and **Windows 11** (in progress, built with MSYS2 UCRT64 and the same `Makefile`). Linux must keep working: every change is built and tested on both. macOS is not supported.
- The design separates **sensor collection**, **theme rendering** and **hardware driver**, so display revisions can be added without changing business logic.

## Layout
- C++ sources live in `src/`. `src/library/` holds the shared library, linked into every program; `src/tools/` holds one program per file.
- Entry points: `src/main.cpp`, `src/configure.cpp`, `src/theme-editor.cpp`, `src/simple-program.cpp` (no header).
- `src/lhm-bridge/lhm-bridge.cs`: Windows-only C# 5 helper program reading the sensors with `LibreHardwareMonitorLib.dll`, built by `make` on Windows with the C# compiler of the .NET Framework 4.x (`csc.exe`).
- Each other `.cpp` file has a `.h` file with the same base name, in the same directory.
- Unit tests live in `tests/cpp/` (GoogleTest), with the same layout as `src/library/`.
- `config.json` and `res/` (themes, fonts, images) are user data and resources, used as-is by the programs. The keys of `config.json` are described in `README.config`.
- Not converted and kept: `library/pythoncheck.py`, `library/sensors/sensors_librehardwaremonitor.py` (Windows only), `tools/windows-installer/`, `external/LibreHardwareMonitor/`.

## Build and tests
- Build with the root `Makefile` (C++20, g++, dependencies found with `pkg-config`; `PLATFORM=linux|windows` is detected): `make deps` checks the dependencies, `make` builds everything, `make test` runs the unit tests, `make clean` removes `build/`.
- Programs are built into `build/bin/`: `main`, `configure`, `theme-editor`, `simple-program`, and the tools (`make tools`).
- `make DEBUG=1` builds without optimizations and with debug symbols.
- GUI programs (`configure`, `theme-editor`) use GTK 4 through gtkmm-4.0.
- Build dependencies (Ubuntu packages): see the `Makefile` header and the README.

## Essential architecture (read before coding)
- **Entry point**: `src/main.cpp` loads the configuration, initializes the display, starts the scheduler, the tray icon and the OS signal handling.
- **Central config**: `src/library/config.cpp` loads `config.json`, then `res/themes/<THEME>/theme.yaml`, with default values for missing keys. The library is initialized explicitly with `config::init()`. Theme values are read with `config::get()` / `config::get_color()`, which return the default value for missing or empty keys.
- **Configuration writing**: `config::update_json_file()` replaces the changed values of `config.json` and keeps the order of the keys. The meaning of every key is documented in `README.config`.
- **LCD driver factory**: `src/library/display.cpp` selects the implementation via `REVISION` (`A/B/C/D/TUR_USB/WEACT_A/WEACT_B/SIMU`).
- **Hardware drivers**: `src/library/lcd/` contains the `LcdComm*` classes (serial/USB) with a common API (`InitializeComm`, `DisplayBitmap`, `DisplayText`, `SetBrightness`, etc.).
- **Sensors and stats rendering**: `src/library/stats.cpp` collects metrics (CPU/GPU/RAM/disk/net/weather) and applies theme styles. Sensors are in `src/library/sensors/`.
- **Scheduling**: `src/library/scheduler.cpp` runs one thread per periodic job, plus a queue thread that serializes writes to the display.
- **Platform layer**: `src/library/platform` holds the services that differ between Linux and Windows (program path, process id, uptime, local time, network interfaces, shared libraries, starting a program, stop requests). Put new OS-specific code there or behind `#ifdef _WIN32` in the module concerned, never in the business logic.
- **Helpers replacing third-party dependencies**: `src/library/lcd/image` (image drawing, PNG/JPEG), `src/library/lcd/font` (FreeType + HarfBuzz text shaping), `src/library/lcd/serial_port` (termios), `src/library/ping` (ICMP socket), `src/library/tray_icon` (StatusNotifierItem + dbusmenu over D-Bus with GIO on Linux, Shell_NotifyIcon on Windows).
- **Sensors**: `sensors_python` (selected by `HW_SENSORS: PYTHON` or `AUTO`) reads Linux `/proc` and `/sys` directly, plus NVIDIA NVML loaded at runtime. On Windows, `sensors_windows` reads the Windows API, and `sensors_lhm` adds the CPU temperature and fan speed through `lhm-bridge.exe` (`HW_SENSORS: LHM`, or `AUTO` as administrator). Custom sensors (`sensors_custom`) are C++ classes registered in `custom_data_sources()`: adding one requires rebuilding.

## Data flow (practical)
- `config.json` selects the hardware, the theme and the options.
- `src/library/stats.cpp` retrieves values based on `HW_SENSORS` (`AUTO`, `PYTHON`, `STUB`, ...).
- `src/library/display.cpp` and the `LcdComm*` driver convert images and text, then push them to the display.
- Dev mode without hardware: `REVISION: SIMU` writes a capture (`screencap.png`) through `src/library/lcd/lcd_simulated.cpp`.

## Testing without hardware
- `LcdComm::serial_factory` replaces the serial port by `MockSerial` (`tests/cpp/library/lcd/serial_mock.h`). The data sent is compared with golden files (`tests/library/lcd/golden/`, revisions A, B, D) or with FNV-1a hashes (revision C, and WeAct A/B).
- The golden files are plain text files shared by the tests; keep them unchanged unless the display protocol changes.

## Documentation conventions (Doxygen)
- Every header and source file starts with the `@file` block and the SPDX header:
  ```cpp
  // SPDX-License-Identifier: GPL-3.0-or-later
  /**
   * @file config.h
   * @brief Short description of the file.
   */
  ```
- Classes, structs, enums, functions and public members have a `/** ... */` comment with `@brief`. Add `@param`, `@return` and `@throws` when they are not obvious from the name and the signature.
- Use `///<` for short comments after a member, and `//` only for implementation details inside a function body.
- Keep the comments in sync with the code when you change a function.

## Repository-specific conventions
- JSON-first configuration: avoid hardcoding. Add options to `config.json` first (and describe them in `README.config`), then read them in `src/library/config.cpp` and the code that uses them. The settings of each display are the elements of the `displays` array (`config::DisplayConfig`, `config::DISPLAYS`); the common settings are in `config`. Themes stay in YAML (`theme.yaml`).
- Hardware routing goes through `REVISION`: do not add a new revision outside the factory in `src/library/display.cpp`.
- Periodic updates go through `src/library/scheduler.cpp`: avoid ad-hoc threads that write to the device.
- UI rendering depends on theme keys (example structure in `res/themes/default.yaml`): keep the schema compatible.
- The rendering does not need to be pixel-identical to the former Pillow-based version, only close to it.

## PR change policy
- Keep pull requests limited and focused; avoid broad refactors unless explicitly requested.
- Prefer minimal diffs to reduce merge conflicts and downstream breakage.
- Split changes on different topics into separate PRs, and large PRs into smaller independent ones.
- Breaking changes are unlikely to be merged: maintain backward compatibility unless a maintainer explicitly approves a break.

## External integrations
- Serial displays: Linux termios. Turing USB models: libusb (DES command encryption implemented in the library).
- Weather: libcurl (OpenWeatherMap). Dates: ICU (CLDR formats).
- System metrics: Linux `/proc` and `/sys`; NVIDIA GPUs through NVML loaded at runtime; AMD GPUs through the amdgpu sysfs interface.
- UI: GTK 4 through gtkmm-4.0 (`configure`, `theme-editor`), GIO/D-Bus (tray icon).

## Reference files for changes
- Runtime orchestration: `src/main.cpp`
- Config/theme loading: `src/library/config.cpp`
- Display selection and dimensions: `src/library/display.cpp`
- Scheduler/periodic jobs: `src/library/scheduler.cpp`
- Sensor collection/rendering: `src/library/stats.cpp`
- User config schema: `config.json` (documented in `README.config`)
- Low-level API example: `src/simple-program.cpp`
- Build: `Makefile`
