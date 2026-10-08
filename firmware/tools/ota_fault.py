#!/usr/bin/env python3
"""Arm an OTA test fault, or mark a pending image valid over USB.

net, sha (CINDER1 {"op":"ota_fault"}, CONFIG_CINDER_OTA_TEST images only): net cuts the next
download once at 50 % (the knob resumes with Range); sha fails the final SHA-256 compare once
(failed/sha256, no reboot). RAM only.

valid (CINDER1 {"op":"ota_valid"}, every image): marks the running image valid while it is
pending verification, skipping only the health checks. Replies: ok; not_pending (the image is
not pending verification); no_checkin (no 200 checkin this boot yet); not_ready (under 60 s
uptime, no frame or no view poll yet); busy (another ota_valid is running, or the ember task
did not take the request within 25 s); failed (the otadata write failed, or the display link
failed at 40 MHz and the rollback is due). The mark runs on the knob's ember task, so the
reply can take up to ~30 s.

The port is opened with raw termios and DTR/RTS left alone (snapshot.py's open_port), so
opening it does not reset the knob.

    python3 firmware/tools/ota_fault.py net
    python3 firmware/tools/ota_fault.py sha
    python3 firmware/tools/ota_fault.py valid
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
    ap.add_argument("fault", choices=["net", "sha", "valid"])
    ap.add_argument("--port", default="/dev/cu.usbmodem1101")
    ap.add_argument("--timeout", type=float, default=None)
    args = ap.parse_args()
    if args.timeout is None:
        args.timeout = 35 if args.fault == "valid" else 4
    fd = open_port(args.port)
    req = {"id": 7301, "op": "ota_valid"} if args.fault == "valid" else {"id": 7301, "op": "ota_fault", "fault": args.fault}
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
            if msg.get("id") == 7301:
                if msg.get("ok"):
                    print("marked valid" if args.fault == "valid" else "armed: " + args.fault)
                    return 0
                if args.fault == "valid":
                    print("ota_valid: " + str(msg.get("error")))
                else:
                    print("ota_fault: " + str(msg.get("error")) + " (unknown_op: the running image is not an OTA test build)")
                return 1
    print("no answer", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
