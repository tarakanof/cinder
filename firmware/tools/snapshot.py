#!/usr/bin/env python3
"""Save the knob's screen as a PNG over USB (CINDER1 {"op":"snapshot"}, cinder#14).

    firmware/tools/snapshot.py [--port /dev/cu.usbmodem1101] [--out knob.png] [--frame]

The knob renders its screen with LVGL's snapshot (screen objects only: the "Not paired" /
"Can't join" / reset overlays are not in it) and sends it as RGB565 in base64 lines. The
PNG is the round 466 px face with transparent corners; --frame keeps the full 472 x 466
driver frame instead.

The port is opened with raw termios and DTR/RTS left alone, so opening it does not reset
the knob. Nothing on the knob changes. A CINDER1 message lowers the knob's log level to
WARN for 30 s.
"""
import argparse
import base64
import json
import os
import select
import struct
import sys
import termios
import time
import zlib


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


def png(path, w, h, rows, alpha):
    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + r for r in rows)
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6 if alpha else 2, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/cu.usbmodem1101")
    ap.add_argument("--out", default="knob.png")
    ap.add_argument("--frame", action="store_true", help="full 472 x 466 frame, no round crop")
    ap.add_argument("--timeout", type=float, default=20)
    args = ap.parse_args()

    fd = open_port(args.port)
    os.write(fd, b'CINDER1 {"id":7001,"op":"snapshot"}\n')
    buf, meta, data, t0 = b"", None, bytearray(), time.monotonic()
    done = False
    while not done and time.monotonic() - t0 < args.timeout:
        r, _, _ = select.select([fd], [], [], 0.5)
        if not r:
            continue
        try:
            buf += os.read(fd, 65536)
        except BlockingIOError:
            continue
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            line = line.rstrip(b"\r")
            if line.startswith(b"SNAP ") and meta:
                data += base64.b64decode(line[5:])
            elif line.startswith(b"CINDER1 "):
                try:
                    msg = json.loads(line[8:])
                except ValueError:
                    continue
                if msg.get("id") != 7001:
                    continue
                if msg.get("ev") == "snapshot":
                    meta = msg
                elif msg.get("ev") == "snapshot_end":
                    done = True
                elif msg.get("error"):
                    sys.exit(f"knob: {msg['error']}")
    os.close(fd)
    if not meta or len(data) != meta["bytes"]:
        sys.exit(f"incomplete snapshot: {len(data)} of {meta and meta['bytes']} bytes")
    w, h, stride = meta["w"], meta["h"], meta["stride"]
    x0, size = (0, w) if args.frame else ((w - h) // 2, h)
    rows = []
    for y in range(h):
        row = bytearray()
        for x in range(x0, x0 + size):
            p = data[y * stride + 2 * x] | data[y * stride + 2 * x + 1] << 8
            r, g, b = (p >> 11) & 31, (p >> 5) & 63, p & 31
            row += bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
            if not args.frame:
                c = h / 2
                d = ((x - x0 + 0.5 - c) ** 2 + (y + 0.5 - c) ** 2) ** 0.5
                row.append(max(0, min(255, int((c - d + 0.5) * 255))))
        rows.append(bytes(row))
    png(args.out, size, h, rows, not args.frame)
    print(f"{args.out}: {size} x {h} in {time.monotonic() - t0:.1f} s")


if __name__ == "__main__":
    main()
