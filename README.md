# GUET-UTMS

GUET (Guilin University of Electronic Technology) UTMS is a Qt Widgets radar tracking and monitoring application.

## Build

Requirements: CMake, Qt 6 with Core/Network/Widgets/Test modules, and a C++17 compiler. The acceptance platform is Windows 10/11 with Qt 6.8 LTS and MSVC 2022 64-bit.

```powershell
cmake -S . -B build -DPROJECT_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Run `UTMS`, leave the default UDP port at `10000`, and click **启动监听**. The status is red while stopped, yellow while listening without recent valid data, and green after an accepted frame.

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
