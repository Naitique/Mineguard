# Mineguard sensor node — wiring

Board: **ESP32-WROOM-32 development board**. Firmware uses GPIO numbers; the
silkscreen label on our board is given in the "Board label" column.

---

Active subsystems are chosen at build time by `ENABLE_MPU6050` /
`ENABLE_VL53L1X` / `ENABLE_LORA` in `firmware/src/config.h`. The two sensors
share one I2C bus (different addresses); LoRa is on a separate SPI bus. The
wiring below is cumulative — wire whatever you have.

## MPU6050 (GY-521)

GY-521 MPU6050 breakout on the I2C bus:

| GY-521 pin | ESP32 pin | Board label | Notes |
|------------|-----------|-------------|-------|
| VCC        | 3V3       | 3V3         | The GY-521 has an onboard regulator; 3V3 is fine and keeps logic levels safe for the ESP32. |
| GND        | GND       | GND         | Common ground. |
| SCL        | GPIO 22   | D22         | I2C clock. |
| SDA        | GPIO 21   | D21         | I2C data. |
| AD0        | (leave unconnected / low) | — | Low → I2C address `0x68`. Tie to 3V3 for `0x69` (then update `MPU6050_I2C_ADDR` in `config.h`). |
| XCL, XDA, INT | not connected | — | Not used in Phase 1. |

I2C bus speed: 400 kHz (see `I2C_CLOCK_HZ` in `config.h`; drop to 100 kHz if
wiring is long or readings are unstable).

---

## VL53L1X Time-of-Flight — shares the same I2C bus

| VL53L1X pin | ESP32 pin | Board label |
|-------------|-----------|-------------|
| VIN         | 3V3       | 3V3         |
| GND         | GND       | GND         |
| SCL         | GPIO 22   | D22         |
| SDA         | GPIO 21   | D21         |
| XSHUT, GPIO1 | not connected | — | Not used yet (needed only for multiple VL53L1X on one bus). |

Default I2C address `0x29` — different from the MPU6050, so both can share the
bus without an address change. Point the sensor at a fixed reference target
~0.1–1 m away for testing.

---

## LoRa Ra-02 / SX1278 — SPI (VSPI), independent of the I2C bus

| Ra-02 pin | ESP32 pin | Board label | Notes |
|-----------|-----------|-------------|-------|
| 3.3V      | 3V3       | 3V3         | **3.3 V only — the Ra-02 is NOT 5 V tolerant.** Powering it from 5 V will damage it. |
| GND       | GND       | GND         | Common ground. |
| NSS       | GPIO 5    | D5          | SPI chip select. |
| SCK       | GPIO 18   | D18         | SPI clock. |
| MISO      | GPIO 19   | D19         | SPI data (module → ESP32). |
| MOSI      | GPIO 23   | D23         | SPI data (ESP32 → module). |
| RST       | GPIO 14   | D14         | Reset, driven by the library. |
| DIO0      | GPIO 26   | D26         | RX-done / TX-done interrupt. |
| DIO1–5    | not connected | — | Not used (only needed for LoRaWAN / advanced modes). |
| ANT       | antenna   | —           | **Always attach a 433 MHz antenna (or ~17 cm wire) before transmitting** — transmitting with no antenna can destroy the PA. |

`GPIO 5`, `18`, `19`, `23` are the ESP32 VSPI defaults, so this bus does not
collide with anything else. Radio parameters (frequency, spreading factor,
bandwidth, sync word) are in `config.h` and **must be identical on both ends**
of a link. `LORA_ROLE_SENDER` in `config.h` selects transmit-test vs
listen-and-print for a given board.

All grounds common. Every peripheral runs from the ESP32 3V3 rail.
