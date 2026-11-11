#!/usr/bin/env python3
"""
lora_bridge.py - LoRa gateway console: bridge and web server.

Reads the gateway's JSON feed from its serial port, sends commands back
to it, keeps history in memory, logs packets to CSV, and serves the
browser console in dashboard.html.

Why a local server and a browser rather than a native window: the only
third party package needed is pyserial, which is already installed. A Qt
or matplotlib UI would be a 100 MB install for a worse chart. A browser
also cannot open a serial port from a hosted page, so the page has to be
served from here regardless.

Usage:
    python lora_bridge.py                      # auto-detect the port
    python lora_bridge.py --port COM4
    python lora_bridge.py --port COM4 --http 8787 --csv packets.csv

Then open http://127.0.0.1:8787/ (opens by itself unless --no-browser).

Protocol, both directions, one JSON object per line:

  gateway -> PC    hello, packet, status, config, node, ack
  PC -> gateway    {"cmd":"get_config"} and friends, see gw_command.h

Partial lines happen - a gateway reset cuts a line in half - so anything
that does not parse is counted and kept as a log line rather than
thrown away.
"""

import argparse
import csv
import json
import os
import sys
import threading
import time
import webbrowser
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    sys.exit("pyserial is required:  python -m pip install pyserial")

HERE = os.path.dirname(os.path.abspath(__file__))

HISTORY_MAX = 20000       # packets kept in RAM for the charts
FIRST_LOAD_MAX = 3000     # sent on a fresh page load
LOG_MAX = 400             # serial lines kept
ACK_MAX = 60
SCHEMA_EXPECTED = 1


class State:
    """Everything the web UI can ask for, behind one lock.

    The serial thread writes; HTTP handler threads read and send
    commands. Both run concurrently, so every access is guarded.
    """

    def __init__(self):
        self.lock = threading.Lock()
        self.write_lock = threading.Lock()
        self.ser = None

        self.packets = deque(maxlen=HISTORY_MAX)
        self.logs = deque(maxlen=LOG_MAX)
        self.acks = deque(maxlen=ACK_MAX)
        self.nodes = {}
        self.node_reports = {}     # from explicit get_nodes replies
        self.gateway = None
        self.gateway_time = 0.0
        self.config = None
        self.config_time = 0.0

        self.cursor = 0
        self.log_cursor = 0
        self.ack_cursor = 0
        self.connected = False
        self.port = None
        self.schema = None
        self.schema_warning = None
        self.parse_errors = 0
        self.total_packets = 0
        self.started = time.time()

    # -- schema -------------------------------------------------------
    def _check_schema(self, value):
        """Caller holds the lock."""
        if value is None:
            return
        self.schema = value
        if value != SCHEMA_EXPECTED:
            self.schema_warning = (
                f"gateway schema {value}, this tool expects {SCHEMA_EXPECTED} "
                f"- some fields may be missing")
        else:
            self.schema_warning = None

    # -- serial thread writes ----------------------------------------
    def add_packet(self, obj):
        with self.lock:
            self.cursor += 1
            self.total_packets += 1
            obj["i"] = self.cursor
            obj["ts"] = time.time()
            self.packets.append(obj)

            node = obj.get("node")
            if node is not None:
                # Status frames carry no temperature or humidity, so the
                # per-node view merges rather than replaces: the table
                # keeps the last known reading instead of blanking it.
                view = self.nodes.setdefault(node, {"node": node, "packets": 0})
                view["packets"] += 1
                view["ts"] = obj["ts"]
                for key in ("radio", "seq", "rssi", "snr", "len", "frame", "mic",
                            "temp_cc", "hum_cp", "batt_mv", "uptime_s"):
                    if key in obj:
                        view[key] = obj[key]

    def add_status(self, obj):
        with self.lock:
            self.gateway = obj
            self.gateway_time = time.time()
            self._check_schema(obj.get("schema"))

    def add_hello(self, obj):
        with self.lock:
            self._check_schema(obj.get("schema"))

    def add_config(self, obj):
        with self.lock:
            self.config = obj
            self.config_time = time.time()

    def add_node_report(self, obj):
        with self.lock:
            n = obj.get("node")
            if n is not None:
                self.node_reports[n] = obj

    def add_ack(self, obj):
        with self.lock:
            self.ack_cursor += 1
            obj["i"] = self.ack_cursor
            obj["ts"] = time.time()
            self.acks.append(obj)

    def add_log(self, text):
        with self.lock:
            self.log_cursor += 1
            self.logs.append({"i": self.log_cursor, "ts": time.time(), "text": text})

    def set_connected(self, flag, port=None, ser=None):
        with self.lock:
            self.connected = flag
            if port:
                self.port = port
            self.ser = ser

    # -- HTTP threads -------------------------------------------------
    def send_command(self, obj):
        """Writes one command line. Returns (ok, message)."""
        with self.lock:
            ser = self.ser
            connected = self.connected
        if not connected or ser is None:
            return False, "serial port not open"
        line = (json.dumps(obj, separators=(",", ":")) + "\r\n").encode("utf-8")
        try:
            # One writer at a time: two commands interleaved on the wire
            # would arrive as one unparseable line.
            with self.write_lock:
                ser.write(line)
                ser.flush()
        except Exception as exc:
            return False, str(exc)
        self.add_log(f"[pc] {line.decode().strip()}")
        return True, "sent"

    def snapshot(self, since):
        with self.lock:
            if since <= 0:
                sel = list(self.packets)[-FIRST_LOAD_MAX:]
            else:
                sel = [p for p in self.packets if p["i"] > since]

            now = time.time()
            recent = sum(1 for p in self.packets if now - p["ts"] <= 60.0)

            gw = dict(self.gateway) if self.gateway else None
            if gw is not None:
                gw["age_s"] = round(now - self.gateway_time, 1)

            cfg = dict(self.config) if self.config else None
            if cfg is not None:
                cfg["age_s"] = round(now - self.config_time, 1)

            nodes = []
            for n in sorted(self.nodes):
                view = dict(self.nodes[n])
                rep = self.node_reports.get(n)
                if rep:
                    for k in ("dups", "silent", "interval_ms"):
                        if k in rep:
                            view[k] = rep[k]
                nodes.append(view)

            return {
                "cursor": self.cursor,
                "connected": self.connected,
                "port": self.port,
                "schema": self.schema,
                "schema_warning": self.schema_warning,
                "gateway": gw,
                "config": cfg,
                "nodes": nodes,
                "packets": sel,
                "logs": list(self.logs)[-60:],
                "acks": list(self.acks)[-12:],
                "stats": {
                    "total": self.total_packets,
                    "rate_per_min": recent,
                    "parse_errors": self.parse_errors,
                    "bridge_uptime_s": int(now - self.started),
                },
            }


STATE = State()


# ---------------------------------------------------------------- serial

def pick_port():
    """Prefer an ST-Link virtual COM port: that is how the Nucleo
    gateway presents USART3."""
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        return None
    for p in ports:
        d = (p.description or "").lower().replace("-", "")
        if "stlink" in d:
            return p.device
    return ports[0].device


def serial_thread(port_arg, baud, csv_path):
    writer = None
    csv_file = None

    if csv_path:
        header = ["iso_time", "radio", "node", "seq", "frame", "mic",
                  "len", "rssi_dbm", "snr_db", "temp_c", "humidity_pct",
                  "battery_mv", "node_uptime_s", "gateway_t_ms"]

        # Appending to a file whose header has a different column set shifts
        # every later value one column across when the file is opened in a
        # spreadsheet, with no visible sign. Rotate such a file aside instead.
        is_new = not os.path.exists(csv_path)
        if not is_new:
            try:
                with open(csv_path, "r", newline="", encoding="utf-8") as fh:
                    old = next(csv.reader(fh), [])
            except OSError:
                old = []
            if old != header:
                stamp = time.strftime("%Y%m%d-%H%M%S")
                base, ext = os.path.splitext(csv_path)
                kept = "%s-%s%s" % (base, stamp, ext)
                os.rename(csv_path, kept)
                print("csv:    column set changed; previous log kept as %s" % kept)
                is_new = True

        csv_file = open(csv_path, "a", newline="", encoding="utf-8")
        writer = csv.writer(csv_file)
        if is_new:
            writer.writerow(header)
            csv_file.flush()

    while True:
        port = port_arg or pick_port()
        if not port:
            STATE.set_connected(False)
            time.sleep(2.0)
            continue

        try:
            ser = serial.Serial(port, baud, timeout=1)
        except Exception as exc:
            STATE.set_connected(False, port)
            STATE.add_log(f"[bridge] cannot open {port}: {exc}")
            time.sleep(2.0)
            continue

        STATE.set_connected(True, port, ser)
        STATE.add_log(f"[bridge] connected to {port} at {baud}")
        # Ask for the configuration straight away so the console can
        # populate its settings page without the user doing anything.
        STATE.send_command({"cmd": "get_config"})

        try:
            while True:
                raw = ser.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", "replace").strip()
                if not line:
                    continue

                if not line.startswith("{"):
                    STATE.add_log(line[:200])      # boot banners, config array
                    continue

                try:
                    obj = json.loads(line)
                except json.JSONDecodeError:
                    with STATE.lock:
                        STATE.parse_errors += 1
                    STATE.add_log(f"[partial] {line[:80]}")
                    continue

                kind = obj.get("type")
                if kind == "packet":
                    STATE.add_packet(obj)
                    if writer:
                        t = obj.get("temp_cc")
                        h = obj.get("hum_cp")
                        writer.writerow([
                            time.strftime("%Y-%m-%dT%H:%M:%S"),
                            obj.get("radio"), obj.get("node"), obj.get("seq"),
                            obj.get("frame"), obj.get("mic"), obj.get("len"),
                            obj.get("rssi"), obj.get("snr"),
                            "" if t is None else f"{t / 100.0:.2f}",
                            "" if h is None else f"{h / 100.0:.2f}",
                            obj.get("batt_mv"), obj.get("uptime_s"), obj.get("t_ms"),
                        ])
                        csv_file.flush()
                elif kind == "status":
                    STATE.add_status(obj)
                elif kind == "config":
                    STATE.add_config(obj)
                elif kind == "node":
                    STATE.add_node_report(obj)
                elif kind == "ack":
                    STATE.add_ack(obj)
                    ok = "ok" if obj.get("ok") else "FAILED: " + str(obj.get("err"))
                    STATE.add_log(f"[gateway] ack {obj.get('cmd')} {ok}")
                elif kind == "hello":
                    STATE.add_hello(obj)
                    STATE.add_log(f"[gateway] hello, schema {obj.get('schema')}, "
                                  f"crypto_ok {obj.get('crypto_ok')}")
                    STATE.send_command({"cmd": "get_config"})
                else:
                    STATE.add_log(line[:160])

        except Exception as exc:
            STATE.set_connected(False)
            STATE.add_log(f"[bridge] {port} dropped: {exc}")
            try:
                ser.close()
            except Exception:
                pass
            time.sleep(1.5)


# ------------------------------------------------------------------ http

class Handler(BaseHTTPRequestHandler):
    def _send(self, code, body, ctype):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = self.path.split("?")[0]

        if path in ("/", "/index.html"):
            try:
                with open(os.path.join(HERE, "dashboard.html"), "rb") as fh:
                    self._send(200, fh.read(), "text/html; charset=utf-8")
            except OSError:
                self._send(500, b"dashboard.html missing next to lora_bridge.py",
                           "text/plain")
            return

        if path == "/api/state":
            since = 0
            if "?" in self.path:
                for part in self.path.split("?", 1)[1].split("&"):
                    if part.startswith("since="):
                        try:
                            since = int(part[6:])
                        except ValueError:
                            since = 0
            self._send(200, json.dumps(STATE.snapshot(since)).encode(),
                       "application/json")
            return

        self._send(404, b"not found", "text/plain")

    def do_POST(self):
        if self.path.split("?")[0] != "/api/command":
            self._send(404, b"not found", "text/plain")
            return
        try:
            n = int(self.headers.get("Content-Length", "0"))
            cmd = json.loads(self.rfile.read(n).decode("utf-8"))
        except Exception as exc:
            self._send(400, json.dumps({"ok": False, "msg": f"bad body: {exc}"}).encode(),
                       "application/json")
            return

        if not isinstance(cmd, dict) or "cmd" not in cmd:
            self._send(400, json.dumps({"ok": False, "msg": "no cmd field"}).encode(),
                       "application/json")
            return

        ok, msg = STATE.send_command(cmd)
        self._send(200 if ok else 503,
                   json.dumps({"ok": ok, "msg": msg}).encode(), "application/json")

    def log_message(self, *args):
        pass        # the default logger prints a line per poll


def main():
    ap = argparse.ArgumentParser(description="LoRa gateway console bridge")
    ap.add_argument("--port", help="serial port, e.g. COM4 (default: auto-detect)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--http", type=int, default=8787)
    ap.add_argument("--csv", default=os.path.join(HERE, "packets.csv"),
                    help="CSV log path, or empty string to disable")
    ap.add_argument("--no-browser", action="store_true")
    args = ap.parse_args()

    threading.Thread(target=serial_thread,
                     args=(args.port, args.baud, args.csv or None),
                     daemon=True).start()

    url = f"http://127.0.0.1:{args.http}/"
    srv = ThreadingHTTPServer(("127.0.0.1", args.http), Handler)
    print(f"LoRa gateway console on {url}")
    print(f"serial: {args.port or 'auto-detect'} at {args.baud}")
    if args.csv:
        print(f"csv:    {args.csv}")
    print("Ctrl+C to stop")

    if not args.no_browser:
        threading.Timer(0.8, lambda: webbrowser.open(url)).start()

    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")


if __name__ == "__main__":
    main()
