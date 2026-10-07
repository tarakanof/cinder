#!/usr/bin/env python3
"""Start a glint chase on the knob now (dev tool, cinder#42): CINDER1 {"op":"chase"}.

The bot must be in working mood with the glint on (bot page, bot.working_ring on). The knob
answers ok, or an error: busy (a chase or its label return is running) or not_working.
The port is opened with raw termios and DTR/RTS left alone (snapshot.py's open_port), so
opening it does not reset the knob. RAM only: nothing is stored.

    python3 firmware/tools/chase.py [--style full|half] [--fps 60|32] [--laps N] [--port /dev/cu.usbmodem1101]

full: the eyes follow the glint all the way round. half: they follow it from 3 to 9 o'clock
through the bottom, blink while it is up top, and jump back to 3 o'clock to meet it.

--fps: eye refreshes per second while chasing (10-60, cinder#51); the knob keeps it for later
chases until a reboot (default 60). --laps: this chase only (1-20; default random 2-5); a long
chase spans several stats windows (diag_override stats_s 5).
"""
import argparse
import json
import os
import select
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snapshot import open_port  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/cu.usbmodem1101")
    ap.add_argument("--style", choices=["full", "half"], default="full")
    ap.add_argument("--fps", type=int, choices=range(10, 61), metavar="10..60")
    ap.add_argument("--laps", type=int, choices=range(1, 21), metavar="1..20")
    ap.add_argument("--timeout", type=float, default=8)
    args = ap.parse_args()
    fd = open_port(args.port)
    req = {"id": 7101, "op": "chase", "style": args.style}
    if args.fps:
        req["fps"] = args.fps
    if args.laps:
        req["laps"] = args.laps
    os.write(fd, ("CINDER1 " + json.dumps(req, separators=(",", ":")) + "\n").encode())
    buf, t0 = b"", time.monotonic()
    while time.monotonic() - t0 < args.timeout:
        r, _, _ = select.select([fd], [], [], 0.5)
        if not r:
            continue
        try:
            buf += os.read(fd, 4096)
        except BlockingIOError:
            continue
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            if not line.startswith(b"CINDER1 "):
                continue
            try:
                msg = json.loads(line[8:])
            except ValueError:
                continue
            if msg.get("id") == 7101:
                print("chase started" if msg.get("ok") else "chase: " + str(msg.get("error")))
                return 0 if msg.get("ok") else 1
    print("no answer", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
