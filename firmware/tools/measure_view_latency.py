#!/usr/bin/env python3
"""Measure the knob's mood-change latency and traffic against Ember (cinder#27, Ember#235).

Run on the Mac with the knob on USB, against the live Ember server, before and after
Ember gets the view long-poll (or before/after a firmware change):

    EMBER_URL=http://ember.local:3627 EMBER_TOKEN=... \\
        firmware/tools/measure_view_latency.py [--idle-min 3] [--samples 20] [--period 15] [--port ...]

What it does:
1. Idle traffic: reads the knob's HTTP counters (CINDER1 diag_override with no fields:
   changes nothing) --idle-min minutes apart, with no changes made. Then waits 35 s,
   because a CINDER1 message lowers the knob's log level to WARN for 30 s and the mood
   log line is INFO.
2. Posts a probe session ("latency-probe" on source "cinder-probe") as waiting, then
   deletes it, every --period seconds. Each change must move the knob's mood, so run it
   while no other session is waiting or in error (the script checks the baseline).
3. Latency = host time the knob's "Ember mood -> N" line arrives on USB minus the host time
   Ember answered the POST/DELETE. USB adds ~1 ms.
4. Deletes the probe session, reads the counters again, and prints latency avg / p50 /
   max, requests/min, KB/min in and out, and the knob's own render-time and internal-heap
   log lines seen during the run.

The port is opened with raw termios and DTR/RTS left alone (opening it does not reset the
knob). Nothing on the knob changes: no set_ember, no token, no Wi-Fi. The token is read
from the environment and never printed. The probe session shows on the TC001 and in
Ember.app while the script runs.
"""
import argparse
import json
import os
import re
import select
import statistics
import sys
import termios
import time
import urllib.request

MOOD_RE = re.compile(rb"ember: Ember mood -> (-?\d+)")
STATS_RE = re.compile(rb"cinder: (pose redraws .*|net: .*)")
PROBE = {"source": "cinder-probe", "tool": "claude", "session": "latency-probe"}


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


class Lines:

    def __init__(self, fd):
        self.fd, self.buf = fd, b""

    def poll(self, timeout):
        out = []
        r, _, _ = select.select([self.fd], [], [], timeout)
        if not r:
            return out
        try:
            self.buf += os.read(self.fd, 4096)
        except BlockingIOError:
            return out
        now = time.monotonic()
        while b"\n" in self.buf:
            line, self.buf = self.buf.split(b"\n", 1)
            out.append((now, line.rstrip(b"\r")))
        return out


def counters(fd, lines):
    os.write(fd, b'CINDER1 {"op":"diag_override"}\n')
    end = time.monotonic() + 3
    while time.monotonic() < end:
        for _, line in lines.poll(0.1):
            if line.startswith(b"CINDER1 ") and b'"requests"' in line:
                return json.loads(line[8:])
    sys.exit("no CINDER1 diag_override reply (firmware before 0.7.0?)")


def ember(method, path, body):
    req = urllib.request.Request(os.environ["EMBER_URL"].rstrip("/") + path, method=method,
                                 data=json.dumps(body).encode(),
                                 headers={"Authorization": "Bearer " + os.environ["EMBER_TOKEN"],
                                          "Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=5) as r:
        r.read()
    return time.monotonic()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=os.environ.get("KNOB_PORT", "/dev/cu.usbmodem1101"))
    ap.add_argument("--idle-min", type=float, default=3, help="minutes of idle traffic to count first")
    ap.add_argument("--samples", type=int, default=20, help="mood changes to time")
    ap.add_argument("--period", type=float, default=15, help="seconds between changes")
    a = ap.parse_args()
    for k in ("EMBER_URL", "EMBER_TOKEN"):
        if not os.environ.get(k):
            sys.exit(f"{k} is not set")

    fd = open_port(a.port)
    lines = Lines(fd)
    try:
        mood, seen = None, []

        def drain(secs):
            nonlocal mood
            end = time.monotonic() + secs
            while time.monotonic() < end:
                for _, line in lines.poll(0.2):
                    m = MOOD_RE.search(line)
                    if m:
                        mood = int(m.group(1))
                    s = STATS_RE.search(line)
                    if s:
                        seen.append(s.group(1).decode(errors="replace"))

        c0 = counters(fd, lines)
        print(f"counting idle traffic for {a.idle_min:g} min ...", flush=True)
        drain(max(35.0, a.idle_min * 60))
        c_idle = counters(fd, lines)
        print("waiting 35 s for the knob's INFO logs to come back ...", flush=True)
        drain(35)
        lat = []
        state = "deleted"
        for i in range(a.samples):
            want_waiting = state == "deleted"
            if want_waiting:
                done = ember("POST", "/v1/status", dict(PROBE, state="waiting"))
                state = "waiting"
            else:
                done = ember("DELETE", "/v1/status", PROBE)
                state = "deleted"
            got = None
            deadline = time.monotonic() + a.period
            while time.monotonic() < deadline:
                for at, line in lines.poll(0.05):
                    m = MOOD_RE.search(line)
                    if m and got is None:
                        got = at
                        mood = int(m.group(1))
                    s = STATS_RE.search(line)
                    if s:
                        seen.append(s.group(1).decode(errors="replace"))
            if got is None:
                print(f"{i + 1:2d} {state:8s} no mood change within {a.period:.0f} s "
                      "(another session waiting or in error?)", flush=True)
            else:
                lat.append(got - done)
                print(f"{i + 1:2d} {state:8s} mood {mood} after {1000 * (got - done):7.0f} ms", flush=True)
    finally:
        try:
            ember("DELETE", "/v1/status", PROBE)
        except Exception as e:  # noqa: BLE001 - best effort cleanup
            print("probe cleanup failed:", e)
    time.sleep(31)
    c1 = counters(fd, lines)
    os.close(fd)

    def rate(name, x, y):
        mins = (y["uptime_ms"] - x["uptime_ms"]) / 60000
        print(f"{name} ({mins:.1f} min): {(y['requests'] - x['requests']) / mins:.1f} req/min, "
              f"{(y['rx_bytes'] - x['rx_bytes']) / 1024 / mins:.2f} KB/min in, "
              f"{(y['tx_bytes'] - x['tx_bytes']) / 1024 / mins:.2f} KB/min out")

    print()
    if lat:
        ms = sorted(1000 * x for x in lat)
        print(f"mood latency n={len(ms)}: avg {statistics.mean(ms):.0f} ms, p50 {statistics.median(ms):.0f} ms, "
              f"max {ms[-1]:.0f} ms")
    rate("idle", c0, c_idle)
    rate(f"with a change every {a.period:g} s", c_idle, c1)
    for s in seen[-6:]:
        print("knob:", s)


if __name__ == "__main__":
    main()
