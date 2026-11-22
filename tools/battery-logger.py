#!/usr/bin/env python3
"""Battery log across one or more sleep phases, one sample every 2 s.

Read-only. Writes JSON lines with CLOCK_BOOTTIME, charge, current, voltage
and temperature. A gap between CLOCK_BOOTTIME and CLOCK_MONOTONIC of more than
30 s between two samples is a sleep phase and is marked "wake". The post-run
counts from the LAST such gap, so suspend-then-hibernate (s2idle, then S4)
is covered as well.

Ends POST_RUN s after the last wake or after MAX_TOTAL s.
Usage: python3 -I battery-logger.py <output.jsonl> [post_run_s]
"""
import json, sys, time

B = "/sys/class/power_supply/BAT0/"
A = "/sys/class/power_supply/ADP1/online"
POST_RUN = int(sys.argv[2]) if len(sys.argv) > 2 else 300
MAX_TOTAL = 8 * 3600


def rd(path):
    try:
        with open(path) as f:
            return f.read().strip()
    except OSError:
        return None


def sample():
    return {
        "boot": time.clock_gettime(time.CLOCK_BOOTTIME),
        "mono": time.clock_gettime(time.CLOCK_MONOTONIC),
        "real": time.time(),
        "status": rd(B + "status"),
        "ac": rd(A),
        "capacity": rd(B + "capacity"),
        "charge_uah": int(rd(B + "charge_now") or -1),
        "current_ua": int(rd(B + "current_now") or 0),
        "voltage_uv": int(rd(B + "voltage_now") or -1),
        "temp_dc": int(rd(B + "temp") or -1),
    }


out = open(sys.argv[1], "a", buffering=1)
start = time.clock_gettime(time.CLOCK_BOOTTIME)
woke_at = None
prev = None
while True:
    s = sample()
    if prev and (s["boot"] - prev["boot"]) - (s["mono"] - prev["mono"]) > 30:
        woke_at = s["boot"]
        s["event"] = "wake"
    out.write(json.dumps(s) + "\n")
    prev = s
    if woke_at is not None and s["boot"] - woke_at > POST_RUN:
        break
    if s["boot"] - start > MAX_TOTAL:
        break
    time.sleep(2)
