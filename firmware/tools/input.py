#!/usr/bin/env python3
"""Send the knob an input as if from its hardware (dev tool, Ember#280): CINDER1 {"op":"input"}.

For testing the now-playing controls without a hand on the knob. The port is opened with raw
termios and DTR/RTS left alone (snapshot.py's open_port), so opening it does not reset the
knob. RAM only: nothing is stored.

    python3 firmware/tools/input.py push            # play / pause (after the double-push window)
    python3 firmware/tools/input.py push -n 2       # double push: next
    python3 firmware/tools/input.py long            # previous
    python3 firmware/tools/input.py turn -n -3      # three detents counter-clockwise: volume down
    python3 firmware/tools/input.py touch           # wakes the now-playing face only
    python3 firmware/tools/input.py page -n -1      # push and turn: the page before
    python3 firmware/tools/input.py swipe_up        # swipe up: the next page (ignored with swipe_pages off)
    python3 firmware/tools/input.py swipe_down      # swipe down: the page before
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
    ap.add_argument("input", choices=["turn", "push", "long", "touch", "page", "swipe_up", "swipe_down"])
    ap.add_argument("-n", type=int, default=1)
    ap.add_argument("--port", default="/dev/cu.usbmodem1101")
    ap.add_argument("--timeout", type=float, default=4)
    args = ap.parse_args()
    fd = open_port(args.port)
    req = {"id": 7201, "op": "input", "input": args.input, "n": args.n}
    os.write(fd, ("CINDER1 " + json.dumps(req) + "\n").encode())
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
            if msg.get("id") == 7201:
                print("ok" if msg.get("ok") else "input: " + str(msg.get("error")))
                return 0 if msg.get("ok") else 1
    print("no answer", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
