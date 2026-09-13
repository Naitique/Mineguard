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

**`firmware/lib/<Name>/` — one self-contained module per subsystem.** Sensor
modules expose the same shape:
- `begin(...)` → `bool` where a presence check is possible (false = device
  absent/miswired); MQ2Sensor has no handshake to check, so its `begin()`
  returns `void`.
- `read(<Name>Reading& out)` → `bool` (or `void` for MQ2Sensor, GPSModule,
  DS18B20Sensor — none of these can "fail" a read, only report a snapshot).
- a plain-struct `<Name>Reading` that carries **both raw and derived values**
  where that distinction exists (e.g. raw accel/gyro *and* roll/pitch). Raw
  measurements are never discarded — the AI team may need the raw time series.
- Current modules:
  - `MPU6050Sensor` — talks raw I2C registers directly, no vendor library —
    accepts WHO_AM_I `0x68` genuine MPU6050 *or* `0x70` MPU6500, since cheap
    GY-521 breakouts often ship the latter and Adafruit's driver hard-rejects
    it; roll/pitch computed here with `atan2`, **not** the on-chip DMP.
  - `GPSModule` — wraps `mikalhart/TinyGPSPlus` over the ESP32's UART2
    (`Serial2`); no address/ID handshake exists on a bare UART, and "did any
    byte arrive" is NOT a safe presence check -- a floating RX pin generates
    noise the UART reports as real bytes, confirmed on the bench. `begin()`
    instead requires `passedChecksum() > 0` within a 3s window (a fully
    checksum-valid NMEA sentence assembled -- noise can't fake that).
  - `MQ2Sensor` — plain `analogRead()`/`digitalRead()`, no library. Runs the
    heater at 3V3 (not the datasheet 5V) so the AO/DO outputs can never exceed
    the ESP32's 3.3V max input; readings are therefore uncalibrated (no ppm
    conversion, no safe/danger threshold — that classification is out of
    scope here regardless of calibration).
  - `DS18B20Sensor` — wraps `paulstoffregen/OneWire` + `milesburton/DallasTemperature`.
    Conversion takes ~750ms; rather than block, `update()` runs a non-blocking
    request/wait/read state machine (call every `loop()` iteration, like
    `GPSModule::poll()`) and `read()` returns the latest cached result instantly.
  (The VL53L1X ToF sensor was removed from the project — replaced by GPS + MQ-2 + DS18B20.)

**`ENABLE_MPU6050` / `ENABLE_GPS` / `ENABLE_MQ2` / `ENABLE_DS18B20` /
`ENABLE_LORA` in `config.h` are independent compile switches**, one per bus
(I2C / UART2 / analog+digital / 1-Wire / SPI) — `main.cpp` guards init and
loop bodies per flag with `#if`; a compile `#error` fires if nothing is
enabled. `FIRMWARE_PHASE` in `config.h` is a cosmetic boot-banner label —
update it when the enabled set changes.

**Error-handling philosophy:** never silently continue as if missing hardware is
fine. Every failure path prints a `[ERROR]`/`[FATAL]` line to serial. If no
enabled I2C sensor initializes, `diagnoseEmptyBus()` scans 0x08–0x77, retries
once at 100 kHz, and prints a wiring checklist. Serial line prefixes in use:
`[BOOT]` `[I2C]` `[MPU6050]` `[GPS]` `[MQ2]` `[DS18B20]` `[LoRa]` `[DATA]`
`[READY]` `[SENSOR]` `[ERROR]` `[FATAL]`.

## Hardware pin map

Board is a 30-pin ESP32-WROOM-32 dev board (DOIT DevKit V1 style). **Firmware uses
GPIO numbers; comments give the board silkscreen label** (e.g. GPIO 21 = "D21").
`docs/wiring.md` has the full pin tables with per-pin notes.

- I2C (MPU6050): SDA = GPIO 21 (D21), SCL = GPIO 22 (D22). Chip at 0x68
  (genuine MPU6050) or 0x70 (MPU6500 clone); 0x69 if AD0 is tied high.
- UART2 (GPS): RX = GPIO 16 ("RX2"), TX = GPIO 17 ("TX2"), 9600 baud default.
- MQ-2 (analog+digital): AO = GPIO 34 (D34, ADC1_CH6), DO = GPIO 35 (D35,
  ADC1_CH7) — both ADC1 (safe if Wi-Fi is later enabled) and input-only.
  VCC = 3V3 only, never 5V/VIN (see lib/MQ2Sensor for why).
- DS18B20 (1-Wire): DATA = GPIO 4 (D4), a plain non-strapping pin. Needs an
  external 4.7kΩ pull-up between DATA and VCC -- most bare sensors don't
  include one.
- LoRa Ra-02 SX1278 on VSPI (Phase 8): SCK 18, MISO 19, MOSI 23, NSS 5, RST 14,
  DIO0 26.
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
`status{}`) is specified in the project brief and materializes in Phase 5.
Distinguish device uptime (`millis()`) from a synchronized timestamp (NTP, when
Wi-Fi is available) — the current firmware only has uptime and labels it as
such.

## Note

This repo is not a git repository yet. A user-level OpenAI Codex config exists at
`~/.codex/config.toml`; reply `/import` if you want to scan it for importable
items (MCP servers, slash commands, subagents, skills, instructions).
