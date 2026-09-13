# Mineguard — AI-Enabled Smart Mine Subsidence Monitoring (student prototype)

A low-cost prototype for monitoring surface deformation above underground coal
mines. A distributed network of sensor nodes sits over an underground mining
panel; each node measures ground tilt, vibration, and relative displacement.
Readings are forwarded to a Raspberry Pi gateway, which stores them in a
standardised JSON/JSONL format for a separate team to use for AI/ML anomaly
detection, GIS visualisation, dashboards, and alerts.

> **Scope disclaimer.** This is a student prototype. It does **not** predict the
> time or occurrence of a mine collapse. The goal is reliable measurement, data
> collection, anomaly-detection readiness, and wireless communication.

This repository is the **hardware / firmware** portion:

```
SENSORS -> ESP32 -> wireless -> Raspberry Pi -> JSON/JSONL
```

Everything after the JSON/JSONL data (dashboards, GIS, ML, alerts) is another
team's responsibility and is not built here.

---

## Current status: bring-up of individual subsystems

Each subsystem is brought up on its own, selected at build time by
`ENABLE_MPU6050` / `ENABLE_GPS` / `ENABLE_MQ2` / `ENABLE_DS18B20` /
`ENABLE_LORA` in `firmware/src/config.h`, printing to the Serial Monitor and
failing loudly when hardware is missing:

- **MPU6050 (I2C):** accel + gyro, derived roll/pitch. Confirmed working —
  the GY-521 breakout carries an MPU6500 die (WHO_AM_I `0x70`, not the genuine
  MPU6050's `0x68`), a common clone substitution; the driver talks raw I2C
  registers and accepts either chip ID.
- **GPS (UART2):** NEO-6M/M8N-style module, NMEA parsed via TinyGPSPlus.
  Bring-up only — reports fix/lat/lon/sats or "no fix yet" diagnostics.
- **MQ-2 (analog + digital):** methane/smoke sensor, deliberately
  uncalibrated bring-up (raw ADC + digital pin state only — no ppm
  conversion, no safe/danger threshold; see `lib/MQ2Sensor`).
- **DS18B20 (1-Wire):** temperature, via OneWire + DallasTemperature.
  Non-blocking conversion cycle (~750ms) so it never stalls the rest of the
  loop; needs an external 4.7kΩ pull-up resistor (see `docs/wiring.md`).
- **LoRa Ra-02 (SPI):** init + version check, transmit-test / receive roles
  (`LORA_ROLE_SENDER`).

All five buses are independent — enable any subset. The VL53L1X ToF sensor
was removed from the project scope and replaced by GPS + MQ-2 (+ DS18B20).

Not yet implemented: calibration/baseline/delta, filtering, configurable JSON
packets, Wi-Fi, Raspberry Pi gateway.

### Layout

```
firmware/
  platformio.ini              PlatformIO project (ESP32, Arduino framework)
  src/
    main.cpp                  setup/loop orchestration only
    config.h                  THE config file: node ID, pins, timing, baud
  lib/
    MPU6050Sensor/            self-contained MPU6050/6500 wrapper module
    GPSModule/                self-contained NEO-6M/M8N GPS wrapper module
    MQ2Sensor/                self-contained MQ-2 gas/smoke wrapper module
    DS18B20Sensor/            self-contained DS18B20 temperature wrapper module
    LoRaTransport/            self-contained Ra-02 / SX1278 wrapper module
docs/
  wiring.md                   pin tables (GPIO + board silkscreen labels)
```

### Wiring (Phase 1)

| GY-521 pin | ESP32 GPIO | Board label |
|------------|------------|-------------|
| VCC        | 3V3        | 3V3         |
| GND        | GND        | GND         |
| SCL        | GPIO 22    | D22         |
| SDA        | GPIO 21    | D21         |

AD0 unconnected → I2C address `0x68`. Full details in `docs/wiring.md`.

### Build / flash / monitor

Requires [PlatformIO](https://platformio.org/install) (`pio` CLI, or the VS Code
extension).

```bash
cd firmware
pio run                      # compile (works without hardware)
pio run --target upload      # flash a connected ESP32
pio device monitor -b 115200 # watch the Serial output
```

### Expected Serial output

```
[BOOT] Mineguard node NODE_01 -- Phase 1 (MPU6050)
[I2C]  SDA=GPIO21 (D21)  SCL=GPIO22 (D22)  @400kHz
[MPU6050] Detected at 0x68
[READY] sampling every 1000 ms
[DATA] node=NODE_01  uptime=1.0s
[MPU6050] accel_g   x=0.012  y=-0.021  z=0.998
[MPU6050] gyro_dps  x=0.13  y=-0.08  z=0.02
[MPU6050] roll=0.42 deg  pitch=-0.18 deg
```

If the sensor is missing or miswired:

```
[ERROR] MPU6050 not detected at 0x68 -- check wiring (SDA=GPIO21/D21, SCL=GPIO22/D22, VCC=3V3). ...
```
(repeats every 2 s until the wiring is fixed and the board is reset)

### Configuring a node

Everything tunable is in `firmware/src/config.h`:

- `NODE_ID` — set per board (`"NODE_01"`, `"NODE_02"`, …)
- `I2C_SDA_PIN` / `I2C_SCL_PIN` / `I2C_CLOCK_HZ`
- `MPU6050_I2C_ADDR` — `0x68` (AD0 low) or `0x69` (AD0 high)
- `SENSOR_INTERVAL_MS` — sample period
- `SERIAL_BAUD` — keep in sync with `monitor_speed` in `platformio.ini`

---

## Security

Do not commit Wi-Fi passwords, API keys, or other credentials. From Phase 6,
real values belong in a git-ignored local file (`secrets.h` / `config.local.h`,
already listed in `.gitignore`).
