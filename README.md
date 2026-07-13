# GUET-UTMS

GUET (Guilin University of Electronic Technology) UTMS is a Qt Widgets radar tracking and monitoring application.

## Build

Requirements: CMake, Qt 6 with Core/Network/Widgets/WebEngineWidgets/WebChannel/Test modules, and a C++17 compiler. The acceptance platform is Windows 10/11 with Qt 6.8 LTS and MSVC 2022 64-bit.

```powershell
cmake -S . -B build -DPROJECT_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Run `UTMS`, leave the default UDP port at `10000`, and click **启动监听**. The status is red while stopped, yellow while listening without recent valid data, and green after an accepted frame.

## Online Amap

Copy `config/amap.example.json` to `config/amap.json`, then enter an Amap Web JavaScript API Key and its security code. The real file is ignored by Git and is copied beside the executable for local builds. Without a complete configuration, the online-map area displays an explicit error; it does not switch map modes automatically.

Create and manage the Web JavaScript API Key in the [Amap developer console](https://console.amap.com/dev/key/app). Keep both configuration values as quoted JSON strings and do not add comments to `amap.json`.

The online map starts in street mode at longitude `110.416819`, latitude `25.311724`, zoom `17` (allowed range `15` to `19`). Use the map controls on the right to switch to satellite imagery or locate the latest valid radar position.

## UDP Simulator

Python 3 is required. The simulator sends complete radar snapshots to `127.0.0.1:10000` at approximately 11 FPS with 10 to 20 targets:

```powershell
python scripts/udp_simulator.py
```

Options can override the destination, target count/range, and frequency:

```powershell
python scripts/udp_simulator.py --host 127.0.0.1 --port 10000 --targets 12-18 --fps 11
```

## Runtime Logs

Runtime logs are written to `logs/utms.log` beside the executable. They include application lifecycle,
UDP binding, payload validation, sequence rejection or jumps, and sequence-baseline resets. Normal accepted
frames are not logged individually. A log rotates at 10 MB, and at most five current/archive files are kept.
