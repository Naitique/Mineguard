# Mineguard sensor node — wiring

Board: **ESP32-WROOM-32 development board**. Firmware uses GPIO numbers; the
silkscreen label on our board is given in the "Board label" column.

---

Active subsystems are chosen at build time by `ENABLE_MPU6050` / `ENABLE_GPS` /
`ENABLE_MQ2` / `ENABLE_DS18B20` / `ENABLE_LORA` in `firmware/src/config.h`.
MPU6050 is on I2C; GPS is on the ESP32's second hardware UART; MQ-2 is plain
analog+digital; DS18B20 is 1-Wire; LoRa is on SPI. All five buses are
independent — the wiring below is cumulative, wire whatever you have.

(The VL53L1X ToF sensor was removed from the project — replaced by GPS + MQ-2.)

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

## GPS module (NEO-6M / NEO-M8N style) — ESP32 UART2

| GPS pin | ESP32 pin | Board label | Notes |
|---------|-----------|-------------|-------|
| VCC     | 3V3       | 3V3         | Most NEO-6M/NEO-M8N breakouts run fine on 3.3–5V via an onboard regulator — confirm your specific module. |
| GND     | GND       | GND         | Common ground. |
| TX      | GPIO 16   | **RX2**     | GPS's TX → ESP32's RX. Board silkscreen already labels this pin for a second serial device. |
| RX      | GPIO 17   | **TX2**     | ESP32's TX → GPS's RX. Only needed if sending config commands to the module; wire it anyway for a standard 4-wire hookup. |

Default baud 9600 (`GPS_BAUD` in `config.h`) — the NEO-6M/M8N power-on default. A first fix can take 30s to a few minutes outdoors with clear sky view, and may never arrive indoors.

---

## MQ-2 gas/smoke sensor — analog + digital, bring-up/testing only

| MQ-2 pin | ESP32 pin | Board label | Notes |
|----------|-----------|-------------|-------|
| VCC      | 3V3       | 3V3         | **Do not use 5V/VIN.** At 5V the AO/DO outputs can approach the supply rail — above the ESP32's 3.3V max input — and could damage the pins. 3.3V keeps this inherently safe, at the cost of under-driving the heater (readings are qualitative, not calibrated ppm). |
| GND      | GND       | GND         | Common ground. |
| AO       | GPIO 34   | D34         | ADC1 channel (stays usable even if Wi-Fi is enabled later — ADC2 pins conflict with the WiFi driver); input-only pin, which is fine since we only ever read it. |
| DO       | GPIO 35   | D35         | Same reasoning as AO. Which logic level means "gas detected" varies by breakout batch — confirm on the bench (e.g. wave an unlit lighter's gas near the sensor and see which level flips) rather than assuming. |

Needs a warm-up period (`MQ2_WARMUP_MS` in `config.h`, default 60s) before readings settle; the datasheet's full calibration soak is much longer (hours) and isn't attempted here. No ppm conversion and no "safe/danger" threshold — risk classification is downstream of this firmware.

---

## DS18B20 temperature sensor — 1-Wire

| DS18B20 pin | ESP32 pin | Board label | Notes |
|-------------|-----------|-------------|-------|
| VCC         | 3V3       | 3V3         | |
| GND         | GND       | GND         | |
| DATA        | GPIO 4    | D4          | **Requires a 4.7kΩ pull-up resistor between DATA and VCC.** A bare TO-92 sensor does not include one; some waterproof-probe breakout boards do — check yours. Without it the bus floats and no device will be found, even with wiring otherwise correct. |

GPIO 4 is a plain, unused general-purpose pin — not a strapping pin, so it carries no boot-mode risk (unlike GPIO 0/2/5/12/15). Conversion takes ~750ms; the firmware runs this as a non-blocking background cycle so it never stalls GPS polling or anything else in `loop()`.

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
