#!/usr/bin/env python3
"""Arm an OTA test fault for the next download (CINDER1 {"op":"ota_fault"}, CONFIG_CINDER_OTA_TEST images only).

net: the download is cut once at 50 % (the knob resumes with Range). sha: the final SHA-256
compare fails once (failed/sha256, no reboot). RAM only; the port is opened with raw termios
and DTR/RTS left alone (snapshot.py's open_port), so opening it does not reset the knob.

    python3 firmware/tools/ota_fault.py net
    python3 firmware/tools/ota_fault.py sha
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
    ap.add_argument("fault", choices=["net", "sha"])
    ap.add_argument("--port", default="/dev/cu.usbmodem1101")
    ap.add_argument("--timeout", type=float, default=4)
    args = ap.parse_args()
    fd = open_port(args.port)
    os.write(fd, ("CINDER1 " + json.dumps({"id": 7301, "op": "ota_fault", "fault": args.fault}) + "\n").encode())
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
            if msg.get("id") == 7301:
                if msg.get("ok"):
                    print("armed: " + args.fault)
                    return 0
                print("ota_fault: " + str(msg.get("error")) + " (unknown_op: the running image is not an OTA test build)")
                return 1
    print("no answer", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
