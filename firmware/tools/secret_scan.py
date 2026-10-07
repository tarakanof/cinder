#!/usr/bin/env python3
import argparse
import os
import re
import sys

FW = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MARKER = b"CINDER-DEV-SEED-BUILD"


def unquote(v):
    v = v.strip()
    if len(v) >= 2 and v[0] == v[-1] and v[0] in "\"'":
        v = v[1:-1]
        v = v.replace('\\"', '"').replace("\\\\", "\\")
    return v


def secrets_values(path):
    out = []
    if not os.path.exists(path):
        return out
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = re.match(r'^\s*(CONFIG_[A-Z0-9_]+)\s*=\s*"(.*)"\s*$', line)
            if m and m.group(2):
                out.append((m.group(1), unquote('"' + m.group(2) + '"').encode()))
    return out


def env_values(path):
    out = []
    if not os.path.exists(path):
        return out
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = re.match(r"^\s*(?:export\s+)?(EMBER_TOKEN)\s*=\s*(.+?)\s*$", line)
            if m and unquote(m.group(2)):
                out.append((m.group(1) + " (producer.env)", unquote(m.group(2)).encode()))
    return out


def main():
    p = argparse.ArgumentParser(
        description="Fail when a built image holds a value from sdkconfig.secrets, the Ember master token or "
        "the dev-seed marker. Prints key names and booleans only, never a value.")
    p.add_argument("--secrets", default=os.path.join(FW, "sdkconfig.secrets"))
    p.add_argument("--env", default=os.path.expanduser("~/.config/ember/producer.env"))
    p.add_argument("files", nargs="+")
    a = p.parse_args()
    data = b""
    for path in a.files:
        with open(path, "rb") as f:
            data += f.read()
    checks = secrets_values(a.secrets) + env_values(a.env)
    if not os.path.exists(a.secrets):
        print("sdkconfig.secrets: not found (nothing to compare)")
    found = False
    for name, value in checks:
        present = value in data
        found |= present
        print(f"{name}: {'PRESENT' if present else 'absent'}")
    marker = MARKER in data
    found |= marker
    print(f"dev-seed marker: {'PRESENT' if marker else 'absent'}")
    print("secret scan: " + ("FAILED" if found else "clean") + f" ({', '.join(os.path.basename(x) for x in a.files)})")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
