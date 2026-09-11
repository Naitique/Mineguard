# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this project is

Firmware for the sensor nodes of "Mineguard" — a student prototype for monitoring
surface deformation above underground coal mines. This repo is **only the
hardware/firmware slice**: `sensors → ESP32 → wireless → Raspberry Pi → JSON/JSONL`.
Do not build dashboards, GIS, ML/AI, cloud, auth, or alerting here — another team
owns everything downstream of the JSON. You may define data structures those
systems consume; do not implement them.

Hard constraint: never write code or docs claiming the system predicts the time
or occurrence of a collapse. No hard-coded "subsidence thresholds" — the firmware
collects data and detects sensor faults; risk classification happens elsewhere.

## Toolchain & commands

PlatformIO + Arduino framework, single board env `esp32dev`. The `pio` CLI is
**not on PATH** — invoke it by full path:

```bash
cd firmware
~/.platformio/penv/bin/pio run                       # compile (works with no hardware — use as the build check)
~/.platformio/penv/bin/pio run --target upload       # flash a connected ESP32
~/.platformio/penv/bin/pio device monitor -b 115200  # serial monitor
~/.platformio/penv/bin/pio pkg install               # fetch lib_deps after editing platformio.ini
```

There is no test suite. Verification is: `pio run` must compile clean, then the
per-phase hardware test (serial output on the bench). `monitor_speed` in
`platformio.ini` and `SERIAL_BAUD` in `config.h` must stay equal (115200).

## Architecture

**`firmware/src/config.h` is the single source of truth for every tunable** —
node ID, I2C pins, I2C clock, sensor addresses, sampling interval, per-sensor
parameters, and the `ENABLE_*` switches. Nothing elsewhere may hard-code these.
When adding a knob, it goes here.

**`firmware/src/main.cpp` is an orchestrator only.** It does `setup()`/`loop()`
scheduling, per-sensor init/status reporting, and serial formatting. It must not
contain sensor register logic — that lives in the per-sensor modules.

**`firmware/lib/<Name>Sensor/` — one self-contained module per sensor.** Each
wraps a vendor library and exposes the same shape:
- `begin(TwoWire&, ...)` → `bool` (false = device absent/miswired)
- `read(<Name>Reading& out)` → `bool`
- a plain-struct `<Name>Reading` that carries **both raw and derived values**
  (e.g. raw accel/gyro *and* roll/pitch; raw distance *and* status code). Raw
  measurements are never discarded — the AI team may need the raw time series.
- Current modules: `MPU6050Sensor` (wraps `adafruit/Adafruit MPU6050`; roll/pitch
  computed here with `atan2`, **not** the on-chip DMP) and `VL53L1XSensor` (wraps
  `pololu/VL53L1X`; continuous ranging; only range_status `RangeValid` / `6` count
  as usable, everything else is a *reported* rejected reading).

**`ENABLE_MPU6050` / `ENABLE_VL53L1X` in `config.h` are compile switches that map
to the development phases:** `1/0` = Phase 1 (MPU only), `0/1` = Phase 2 (ToF
only), `1/1` = Phase 3 (both on the shared I2C bus). `main.cpp` guards init and
loop bodies per flag with `#if`, and a compile `#error` fires if both are off.
`FIRMWARE_PHASE` in `config.h` is a cosmetic boot-banner label — update it when
the enabled set changes.

**Error-handling philosophy:** never silently continue as if a missing sensor is
fine. Every failure path prints a `[ERROR]`/`[FATAL]` line to serial. If no
enabled sensor initializes, `diagnoseEmptyBus()` scans 0x08–0x77, retries once at
100 kHz, and prints a wiring checklist. Serial line prefixes in use: `[BOOT]`
`[I2C]` `[MPU6050]` `[VL53L1X]` `[DATA]` `[READY]` `[SENSOR]` `[ERROR]` `[FATAL]`.

## Hardware pin map

Board is a 30-pin ESP32-WROOM-32 dev board (DOIT DevKit V1 style). **Firmware uses
GPIO numbers; comments give the board silkscreen label** (e.g. GPIO 21 = "D21").
`docs/wiring.md` has the full pin tables with per-pin notes.

- I2C (MPU6050 + VL53L1X share it): SDA = GPIO 21 (D21), SCL = GPIO 22 (D22).
  MPU6050 at 0x68 (0x69 if AD0 high), VL53L1X at 0x29.
- LoRa Ra-02 SX1278 on VSPI (Phase 8, not yet wired in firmware): SCK 18, MISO 19,
  MOSI 23, NSS 5, RST 14, DIO0 26.
- All peripherals run from the ESP32 3V3 rail; all grounds common.

## Development process

Work strictly one numbered phase at a time (see `README.md` / the plan file for
the full 1–9 list). After each milestone: explain what changed, how to wire/test,
expected serial output, common failure cases — then **stop**; do not auto-start
the next phase. Keep filtering simple (moving average / median / low-pass); no
Kalman without a tested-simpler-method justification. Calibration offsets,
baseline/delta distance, and the complementary filter are deferred to Phase 4.

Before adding an Arduino library: justify it, prefer mature/standard ones, and
verify the real API against the installed header under
`firmware/.pio/libdeps/esp32dev/<Lib>/` — do not invent hardware APIs.

Credentials (Wi-Fi password, API keys — Phase 6+) must live in a git-ignored
local file (`secrets.h` / `*.local.h`, already in `.gitignore`), never in
tracked source.

## Data model

The canonical sensor-packet JSON schema (node_id, timestamp, `mpu6050{}`,
`vl53l1x{}`, `status{}`) is specified in the project brief and materializes in
Phase 5. Distinguish device uptime (`millis()`) from a synchronized timestamp
(NTP, when Wi-Fi is available) — the current firmware only has uptime and labels
it as such.

## Note

This repo is not a git repository yet. A user-level OpenAI Codex config exists at
`~/.codex/config.toml`; reply `/import` if you want to scan it for importable
items (MCP servers, slash commands, subagents, skills, instructions).
