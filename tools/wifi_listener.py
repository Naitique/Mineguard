#!/usr/bin/env python3
"""Receive Mineguard LoRa packets forwarded over Wi-Fi and log them to JSONL.

Run this on the analysis laptop. The receiver ESP32 (ENABLE_WIFI_FORWARD in
firmware/src/config.h) POSTs one JSON object per received LoRa packet here:

    {"rssi_dbm": -52, "snr_db": 14.2, "len": 92, "payload": "node=NODE_01 seq=7 ..."}

Each POST is parsed, the payload's "key=value" text is split into fields
(same format tools/serial_logger.py uses for the USB path), and one record
per line is appended to a JSONL output file -- ready for
`pandas.read_json(path, lines=True)`.

Uses only the Python standard library -- nothing to pip install.

Usage:
    python3 wifi_listener.py                        # listens on 0.0.0.0:8000
    python3 wifi_listener.py --port 8000 --out run1.jsonl

Then put this machine's LAN IP in WIFI_FORWARD_HOST (firmware/src/config.h).
Find it with `ipconfig getifaddr en0` (macOS Wi-Fi), `hostname -I` (Linux),
or `ipconfig` (Windows).
"""
import argparse
import json
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, HTTPServer


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


def make_handler(out_path: str):
    class Handler(BaseHTTPRequestHandler):
        def do_POST(self):
            length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(length)
            try:
                incoming = json.loads(body)
                record = {
                    "received_at": datetime.now(timezone.utc).isoformat(),
                    "rssi_dbm": incoming.get("rssi_dbm"),
                    "snr_db": incoming.get("snr_db"),
                    "payload_len": incoming.get("len"),
                    **parse_payload(incoming.get("payload", "")),
                }
                with open(out_path, "a", buffering=1) as f:
                    f.write(json.dumps(record) + "\n")
                print(record)
                self.send_response(204)
            except json.JSONDecodeError as e:
                print(f"[wifi_listener] bad request body: {e}")
                self.send_response(400)
            self.end_headers()

        def log_message(self, fmt, *args):
            pass  # silence the default per-request access log

    return Handler


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="0.0.0.0",
                     help="Bind address (default: all interfaces)")
    ap.add_argument("--port", type=int, default=8000,
                     help="Must match WIFI_FORWARD_PORT in config.h (default: %(default)s)")
    ap.add_argument("--out", default="wifi_log.jsonl",
                     help="Output JSONL file, appended to (default: %(default)s)")
    args = ap.parse_args()

    server = HTTPServer((args.host, args.port), make_handler(args.out))
    print(f"[wifi_listener] listening on {args.host}:{args.port}, appending to {args.out}")
    print("[wifi_listener] set WIFI_FORWARD_HOST in firmware/src/config.h to this machine's LAN IP")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[wifi_listener] stopped")


if __name__ == "__main__":
    main()
