#!/usr/bin/env python3
"""
Read stride_results.csv, average the five runs for each stride, and plot
stride length (bytes) against mean elapsed time (seconds).

Run:
    python3 plot_results.py [results_csv]
"""

import csv
import sys
from collections import defaultdict
import matplotlib.pyplot as plt

input_path = sys.argv[1] if len(sys.argv) > 1 else "stride_results.csv"

times_by_stride = defaultdict(list)
with open(input_path, newline="", encoding="utf-8") as csvfile:
    reader = csv.DictReader(csvfile)
    for row in reader:
        stride = int(row["stride_bytes"])
        elapsed = float(row["elapsed_seconds"]) * 1_000
        times_by_stride[stride].append(elapsed)

strides = sorted(times_by_stride)
averages = [
    sum(times_by_stride[stride]) / len(times_by_stride[stride])
    for stride in strides
]

if not strides:
    raise SystemExit(f"No measurements found in {input_path}")

for stride, avg in zip(strides, averages):
    print(f"Stride {stride:3d} bytes: average {avg:.3f} milliseconds "
      f"({len(times_by_stride[stride])} runs)")

plt.figure(figsize=(10, 6))
plt.plot(strides, averages, marker=".", linewidth=1)
plt.xlabel("Stride length (bytes)")
plt.ylabel("Average elapsed time (milliseconds)")
plt.title("Sequential byte-scan time vs. stride length")
plt.grid(True, alpha=0.3)
plt.tight_layout()
plt.savefig("stride_plot.png", dpi=150)
plt.show()
print("Plot saved to stride_plot.png")
