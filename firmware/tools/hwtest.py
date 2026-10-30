#!/usr/bin/env python3
"""Drive the J2 hardware test image (firmware/hwtest, docs/hardware-test.md) over USB: HWT1 lines.

The port is opened with raw termios and DTR/RTS left alone (snapshot.py's open_port), so opening it
does not reset the knob. The test image keeps everything in RAM: pins set here last until a reboot.

    python3 firmware/tools/hwtest.py info
    python3 firmware/tools/hwtest.py probe              # guided J2 pin map: one GPIO high at a time, you read the multimeter
    python3 firmware/tools/hwtest.py levels             # which candidate GPIOs a module pulls up (wired I2C lines read 1)
    python3 firmware/tools/hwtest.py scan               # I2C addresses on the sensor bus
    python3 firmware/tools/hwtest.py pins --bclk 39 --lrc 40 --din 38
    python3 firmware/tools/hwtest.py watch -s 60        # room snapshot once a second
    python3 firmware/tools/hwtest.py tone --hz 880 --ms 400 --vol 20
    python3 firmware/tools/hwtest.py radar --ms 10000   # LD2410 UART reports (needs pins --radar_rx)
"""
import argparse
import json
import os
import select
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snapshot import open_port  # noqa: E402

PREFIX = b"HWT1 "
J2_SIGNAL_PINS = ["2", "4", "5", "6", "7"]


class Knob:
    def __init__(self, port):
        self.fd = open_port(port)
        self.buf = b""
        self.next_id = 9100

    def send(self, req):
        self.next_id += 1
        req = dict(req, id=self.next_id)
        os.write(self.fd, PREFIX + json.dumps(req).encode() + b"\n")
        return self.next_id

    def lines(self, timeout):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            r, _, _ = select.select([self.fd], [], [], 0.25)
            if r:
                try:
                    self.buf += os.read(self.fd, 4096)
                except BlockingIOError:
                    pass
            while b"\n" in self.buf:
                line, self.buf = self.buf.split(b"\n", 1)
                line = line.rstrip(b"\r")
                if not line.startswith(PREFIX):
                    continue
                try:
                    yield json.loads(line[len(PREFIX):])
                except ValueError:
                    continue

    def call(self, req, timeout=5, on_event=None):
        rid = self.send(req)
        for msg in self.lines(timeout):
            if msg.get("id") == rid:
                return msg
            if "ev" in msg and on_event:
                on_event(msg)
        raise SystemExit("no answer to %s (is the hwtest image running? port?)" % req.get("op"))

    def events(self, seconds, on_event):
        for msg in self.lines(seconds):
            if "ev" in msg:
                on_event(msg)


def show(msg):
    print(json.dumps(msg, indent=2, sort_keys=True))
    return 0 if msg.get("ok", True) else 1


def room_line(ev):
    r = ev.get("room", {})
    parts = []
    if "temp_c" in r:
        parts.append("%.1f C %.0f%%" % (r["temp_c"], r["rh_pct"]))
    if "eco2_ppm" in r:
        parts.append("eCO2 %d TVOC %d AQI %d (%s)" % (r["eco2_ppm"], r["tvoc_ppb"], r["aqi"], r.get("air_validity")))
    elif "air_validity" in r:
        parts.append("air %s" % r["air_validity"])
    if "lux" in r:
        parts.append("%.0f lx" % r["lux"])
    if "present" in r:
        parts.append("present" if r["present"] else "away")
    if "press_hpa" in r:
        parts.append("%.1f hPa" % r["press_hpa"])
    parts.append("err %d" % r.get("errors", 0))
    print(time.strftime("%H:%M:%S"), " | ".join(parts), flush=True)


def guided_probe(k):
    info = k.call({"op": "info"})
    gpios = info.get("probe_gpios", [])
    print("Measure J2 pins 2, 4, 5, 6 and 7 against pin 3 (GND) for each step.")
    print("Answer with the J2 pin that reads about 3.3 V, 'n' for none, 'r' to drive it again, 'q' to stop.\n")
    found = {}
    for g in gpios:
        while True:
            if not k.call({"op": "probe", "gpio": g}).get("ok"):
                print("  GPIO%d refused the probe (in use as the radar RX); skipped" % g)
                a = "skip"
                break
            a = input("GPIO%-2d is high. Which J2 pin reads ~3.3 V? " % g).strip().lower()
            if a == "r":
                continue
            break
        if a == "q":
            break
        if a == "skip":
            continue
        if a in J2_SIGNAL_PINS:
            found[g] = a
        elif a != "n":
            print("  (not a J2 signal pin; recorded as none)")
    k.call({"op": "stop"})
    print("\nJ2 map:")
    for pin in J2_SIGNAL_PINS:
        g = [str(x) for x, p in found.items() if p == pin]
        print("  J2 pin %s: %s" % (pin, ("GPIO" + ", GPIO".join(g)) if g else "no probed GPIO (do not use)"))
    dup = {p for p in found.values() if list(found.values()).count(p) > 1}
    if dup:
        print("  Two GPIOs on one J2 pin (%s): re-check the probe on those." % ", ".join(sorted(dup)))
    if found.get(43) != "7" or found.get(44) != "6":
        print("  Expected GPIO43 on pin 7 and GPIO44 on pin 6 (UART0 TX/RX). If not, check pin 1's position on the header.")
    free = [g for g in found if g not in (43, 44)]
    if len(free) >= 3:
        print("\nSpeaker pins to try: hwtest.py pins --bclk %d --lrc %d --din %d" % tuple(free[:3]))
    print(json.dumps({"j2": {str(g): p for g, p in found.items()}}, sort_keys=True))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="/dev/cu.usbmodem1101")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("info")
    sub.add_parser("probe")
    p = sub.add_parser("drive", help="hold one candidate GPIO high (or low with --low) until stop")
    p.add_argument("gpio", type=int)
    p.add_argument("--low", action="store_true")
    sub.add_parser("stop")
    sub.add_parser("levels")
    sub.add_parser("scan")
    p = sub.add_parser("room")
    p.add_argument("do", nargs="?", choices=["get", "start", "stop"], default="get")
    p = sub.add_parser("watch")
    p.add_argument("-s", type=int, default=30)
    p.add_argument("--json", action="store_true")
    p = sub.add_parser("pins")
    for name in ["sda", "scl", "bclk", "lrc", "din", "radar_rx"]:
        p.add_argument("--" + name, type=int)
    p.add_argument("--radar_bit", type=int)
    p.add_argument("--pcf", type=lambda s: int(s, 0))
    p = sub.add_parser("tone")
    p.add_argument("--hz", type=int, default=880)
    p.add_argument("--ms", type=int, default=400)
    p.add_argument("--vol", type=int, default=20)
    p = sub.add_parser("radar")
    p.add_argument("--ms", type=int, default=10000)
    p = sub.add_parser("raw")
    p.add_argument("json")
    a = ap.parse_args()
    k = Knob(a.port)

    if a.cmd == "probe":
        return guided_probe(k)
    if a.cmd == "drive":
        return show(k.call({"op": "probe", "gpio": a.gpio, "level": 0 if a.low else 1}))
    if a.cmd == "room":
        return show(k.call({"op": "room", "do": a.do}))
    if a.cmd == "watch":
        on = (lambda ev: print(json.dumps(ev), flush=True)) if a.json else room_line
        k.call({"op": "watch", "s": a.s}, on_event=on)
        k.events(a.s + 2, on)
        return 0
    if a.cmd == "pins":
        req = {"op": "pins"}
        for name in ["sda", "scl", "bclk", "lrc", "din", "radar_rx", "radar_bit", "pcf"]:
            if getattr(a, name) is not None:
                req[name] = getattr(a, name)
        return show(k.call(req))
    if a.cmd == "tone":
        return show(k.call({"op": "tone", "hz": a.hz, "ms": a.ms, "vol": a.vol}, timeout=a.ms / 1000 + 5))
    if a.cmd == "radar":
        def on(ev):
            if ev.get("ev") == "radar":
                print("%s move %dcm/%d still %dcm/%d detect %dcm" % (ev["state"], ev["move_cm"], ev["move_energy"], ev["still_cm"],
                                                                    ev["still_energy"], ev["detect_cm"]), flush=True)
        return show(k.call({"op": "radar", "ms": a.ms}, timeout=a.ms / 1000 + 5, on_event=on))
    if a.cmd == "raw":
        return show(k.call(json.loads(a.json), timeout=30))
    return show(k.call({"op": a.cmd}, timeout=15))


if __name__ == "__main__":
    sys.exit(main())
