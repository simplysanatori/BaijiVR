#!/usr/bin/env python3
"""Summarise Baiji's Logs/perf.csv: median and 5th/95th percentile per step.

usage: perf-summary.py [perf.csv ...]   (default: ~/.local/share/baiji/Logs/perf.csv)

Rows with running=0 and the settling seconds of a performance test step (name#round*) are left
out. Rows of the same step name (all rounds) are pooled. Rows without a step are grouped by their
`switches` column. Same numbers as the report of the performance test.
"""
import csv
import os
import sys


def percentile(values, p):
    values = sorted(values)
    pos = p * (len(values) - 1)
    lo = int(pos)
    hi = min(lo + 1, len(values) - 1)
    return values[lo] + (values[hi] - values[lo]) * (pos - lo)


def cell(values):
    if not values:
        return "none"
    return "%.2f [%.2f, %.2f]" % (percentile(values, 0.5), percentile(values, 0.05),
                                   percentile(values, 0.95))


def main(paths):
    groups = {}
    order = []
    for path in paths:
        with open(path, newline="") as f:
            for row in csv.DictReader(f):
                if row.get("running") != "1":
                    continue
                step = row.get("step") or ""
                if step.endswith("*"):
                    continue
                key = step.rsplit("#", 1)[0] if step else (row.get("switches") or "(defaults)")
                if key not in groups:
                    groups[key] = {"fps": [], "speed": [], "gpu_ms": [], "skipped": [], "wait": []}
                    order.append(key)
                g = groups[key]
                seconds = float(row["seconds"]) or 1.0
                g["fps"].append(float(row["fps"]))
                g["speed"].append(float(row["speed"]))
                if int(row["gpu_frames"]) > 0:
                    g["gpu_ms"].append(float(row["gpu_ms"]))
                g["skipped"].append(int(row["skipped_pairs"]) / seconds)
                g["wait"].append(float(row["gpu_wait_ms"]))

    for key in order:
        g = groups[key]
        print("%s (%d seconds)" % (key, len(g["fps"])))
        print("  fps             %s" % cell(g["fps"]))
        print("  speed           %s" % cell(g["speed"]))
        print("  gpu_ms          %s" % cell(g["gpu_ms"]))
        print("  skipped_pairs/s %s" % cell(g["skipped"]))
        print("  gpu_wait_ms     %s" % cell(g["wait"]))
        print()


if __name__ == "__main__":
    args = sys.argv[1:] or [os.path.expanduser("~/.local/share/baiji/Logs/perf.csv")]
    main(args)
