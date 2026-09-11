#!/usr/bin/env python3
"""Log Mineguard LoRa receiver packets from USB serial to JSONL.

Run this on the laptop or Raspberry Pi that has the LoRa RECEIVER ESP32
(LORA_ROLE_SENDER = false in firmware/src/config.h) plugged in over USB.
It reads the board's Serial Monitor output, parses each

    [LoRa] RX rssi=-36dBm snr=13.2dB len=95: "node=NODE_01 seq=50 ..."

line, and appends one JSON object per packet to an output file, one packet
per line (JSONL) -- ready to load with `pandas.read_json(path, lines=True)`
or `jq` for analysis. Non-packet lines (boot banner, [ERROR] lines, etc.)
are ignored.

Usage:
    python3 serial_logger.py                       # auto-detect port
    python3 serial_logger.py --port /dev/cu.usbserial-0001 --out run1.jsonl
"""
import argparse
import json
import re
import sys
from datetime import datetime, timezone

import serial
from serial.tools import list_ports

RX_LINE_RE = re.compile(
    r'^\[LoRa\] RX rssi=(-?\d+)dBm snr=(-?\d+(?:\.\d+)?)dB len=(\d+): "(.*)"$'
)


def find_port():
    """Pick the first real USB-serial port (skips Bluetooth/virtual ports)."""
    candidates = [p for p in list_ports.comports() if p.vid is not None]
    return candidates[0].device if candidates else None


def parse_payload(payload: str) -> dict:
    """Split the radio payload's 'key=value key=value ...' text into a dict,
    converting each value to int/float where possible."""
    fields = {}
    for token in payload.split():
        if "=" not in token:
            continue
        key, value = token.split("=", 1)
        try:
            fields[key] = int(value)
        except ValueError:
            try:
                fields[key] = float(value)
            except ValueError:
                fields[key] = value
    return fields


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="Serial port (default: auto-detect)")
    ap.add_argument("--baud", type=int, default=115200,
                     help="Must match SERIAL_BAUD in firmware/src/config.h")
    ap.add_argument("--out", default="mineguard_log.jsonl",
                     help="Output JSONL file, appended to (default: %(default)s)")
    args = ap.parse_args()

    port = args.port or find_port()
    if not port:
        sys.exit("No serial port found -- pass --port explicitly.")

    print(f"[logger] opening {port} @ {args.baud} baud, appending to {args.out}")
    ser = serial.Serial(port, args.baud, timeout=1)

    count = 0
    with open(args.out, "a", buffering=1) as out:
        try:
            while True:
                raw = ser.readline().decode("utf-8", "replace").strip()
                if not raw:
                    continue
                m = RX_LINE_RE.match(raw)
                if not m:
                    continue  # boot banner / [ERROR] lines / partial reads
                rssi_dbm, snr_db, length, payload = m.groups()
                record = {
                    "received_at": datetime.now(timezone.utc).isoformat(),
                    "rssi_dbm": int(rssi_dbm),
                    "snr_db": float(snr_db),
                    "payload_len": int(length),
                    **parse_payload(payload),
                }
                out.write(json.dumps(record) + "\n")
                count += 1
                print(f"[{count}] {record}")
        except KeyboardInterrupt:
            print(f"\n[logger] stopped -- {count} packet(s) logged to {args.out}")


if __name__ == "__main__":
    main()
