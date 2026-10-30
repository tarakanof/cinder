#!/usr/bin/env python3
"""Read the knob's music-face span timings over USB (CINDER1 {"op":"timing"}, cinder#297).

    firmware/tools/timing.py [--port /dev/cu.usbmodem1101] [--reset]

Prints one row per span: calls, average and maximum microseconds since boot or the last reset.
--reset clears the table after the read (the reply still holds the values read). Spans nest:
`update` holds the others on the LVGL task, `label` holds `v15_render` and `arc_text`.

The port is opened with raw termios and DTR/RTS left alone, so opening it does not reset
the knob. A CINDER1 message lowers the knob's log level to WARN for 30 s.
"""
import argparse
import json
import os
import select
import sys
import termios
import time


def open_port(port):
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    a = termios.tcgetattr(fd)
    a[0] &= ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK | termios.ISTRIP | termios.INLCR |
              termios.IGNCR | termios.ICRNL | termios.IXON)
    a[1] &= ~termios.OPOST
    a[2] &= ~(termios.CSIZE | termios.PARENB | termios.HUPCL | termios.CRTSCTS)
    a[2] |= termios.CS8 | termios.CLOCAL | termios.CREAD
    a[3] &= ~(termios.ECHO | termios.ECHONL | termios.ICANON | termios.ISIG | termios.IEXTEN)
    termios.tcsetattr(fd, termios.TCSANOW, a)
    return fd


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/cu.usbmodem1101")
    ap.add_argument("--reset", action="store_true", help="clear the table after reading it")
    ap.add_argument("--timeout", type=float, default=5)
    args = ap.parse_args()

    req = {"id": 7002, "op": "timing"}
    if args.reset:
        req["reset"] = True
    fd = open_port(args.port)
    os.write(fd, b"CINDER1 " + json.dumps(req, separators=(",", ":")).encode() + b"\n")
    buf, msg, t0 = b"", None, time.monotonic()
    while msg is None and time.monotonic() - t0 < args.timeout:
        r, _, _ = select.select([fd], [], [], 0.5)
        if not r:
            continue
        try:
            buf += os.read(fd, 65536)
        except BlockingIOError:
            continue
        while b"\n" in buf and msg is None:
            line, buf = buf.split(b"\n", 1)
            if not line.startswith(b"CINDER1 "):
                continue
            try:
                m = json.loads(line[8:])
            except ValueError:
                continue
            if m.get("id") == 7002:
                msg = m
    os.close(fd)
    if msg is None:
        sys.exit("no answer from the knob")
    if msg.get("error"):
        sys.exit(f"knob: {msg['error']}")
    print(f"since {'reset' if args.reset else 'boot or the last reset'}: {msg['since_ms'] / 1000:.1f} s")
    print(f"{'span':<14}{'n':>8}{'avg us':>10}{'max us':>10}")
    for name, s in msg["spans"].items():
        print(f"{name:<14}{s['n']:>8}{s['avg_us']:>10}{s['max_us']:>10}")
    if args.reset:
        print("table cleared")


if __name__ == "__main__":
    main()
