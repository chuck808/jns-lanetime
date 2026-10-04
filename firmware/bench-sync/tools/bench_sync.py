#!/usr/bin/env python3
"""Pair bench-sync output from three boards and report timebase agreement.

    pip install pyserial
    python tools/bench_sync.py ST1=COM5 A=COM6 B=COM7        (Windows)
    python tools/bench_sync.py ST1=/dev/ttyACM0 A=/dev/ttyACM1 B=/dev/ttyACM2

ST1 is the heartbeat board (true ST-1 time of each pulse). A and B are units.
Edges are paired by time, not sequence number, so a missed edge cannot shift pairs.

  A-B    the sync error between units: the figure that matters for results,
         because start and finish come from different units.
  A-ST1  unit vs truth: includes the minimum radio delay, a bias that cancels
         in A-B and in elapsed times. Its spread still matters.
"""
import statistics
import sys
import threading
import time

import serial

PAIR_WINDOW_US = 100_000  # converted times sit within a few ms of truth


def reader(label, port, events, lock):
    with serial.Serial(port, 115200, timeout=1) as link:
        while True:
            line = link.readline().decode(errors="replace").strip()
            if not line:
                continue
            fields = line.split(",")
            if fields[0] == "EDGE" and len(fields) >= 3 and fields[2] != "FAIL":
                with lock:
                    events[label].append((int(fields[2]), fields[3] if len(fields) > 3 else "1"))
            elif fields[0] in ("TB", "FATAL") or (fields[0] == "EDGE" and "FAIL" in fields):
                print(f"[{label}] {line}")


def nearest(values, target):
    best = min(values, key=lambda v: abs(v[0] - target), default=None)
    return best if best and abs(best[0] - target) <= PAIR_WINDOW_US else None


def summary(name, xs):
    if len(xs) < 2:
        return f"{name}: n={len(xs)}"
    return (f"{name}: n={len(xs)} mean={statistics.mean(xs):+.1f} "
            f"sd={statistics.stdev(xs):.1f} worst={max(xs, key=abs):+.1f} us")


def main():
    ports = dict(arg.split("=", 1) for arg in sys.argv[1:])
    if not {"ST1", "A", "B"} <= ports.keys():
        sys.exit(__doc__)
    events = {label: [] for label in ports}
    lock = threading.Lock()
    for label, port in ports.items():
        threading.Thread(target=reader, args=(label, port, events, lock), daemon=True).start()
    seen, ab, ast, bst = set(), [], [], []
    print("truth_us            A-B      A-ST1    B-ST1   (us; * = unit not Ready at the edge)")
    while True:
        time.sleep(0.5)
        with lock:
            truths = [t for t, _ in events["ST1"] if t not in seen]
            for truth in truths:
                a, b = nearest(events["A"], truth), nearest(events["B"], truth)
                if not (a and b):
                    continue  # Units convert ~2 s after the edge; try again later.
                seen.add(truth)
                flag = "" if a[1] == "1" and b[1] == "1" else " *"
                d_ab, d_a, d_b = a[0] - b[0], a[0] - truth, b[0] - truth
                if not flag:
                    ab.append(d_ab); ast.append(d_a); bst.append(d_b)
                print(f"{truth:<18d} {d_ab:+8d} {d_a:+8d} {d_b:+8d}{flag}")
                if len(ab) and len(ab) % 20 == 0:
                    print("  " + summary("A-B", ab))
                    print("  " + summary("A-ST1", ast))
                    print("  " + summary("B-ST1", bst))


if __name__ == "__main__":
    main()
