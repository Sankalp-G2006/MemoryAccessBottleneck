#!/usr/bin/env python3
"""Partitioned Hash‑Join performance experiment (Section 3.4.3 continuation)

Implements the analytical model for the partitioned hash‑join described in the
paper. For each number of radix bits **B** and each input cardinality **C** the
script computes:
* L1 cache misses
* L2 cache misses
* TLB misses
* Total runtime (ms)

The results are written to *partitioned_hash_join_results.csv* and four PNG
figures (L1, L2, TLB, runtime) are generated under the same *output* folder.
"""

import os
import csv
import math
from pathlib import Path

# ------------------- Parameters (paper defaults) -------------------
# Timing constants (nanoseconds) – placeholders, adjust as needed
w_h = 24e-9          # cost per tuple (hash build + lookup)
w_h_prime = 240e-9   # extra cost per cluster for hash‑table creation/destruction
# Memory‑latency constants (nanoseconds)
l_L2 = 100e-9        # L2‑cache line latency
l_Mem = 200e-9       # main‑memory latency
l_TLB = 30e-9        # TLB miss latency
# Cache sizes (bytes) – typical values
L1_SIZE = 32 * 1024          # 32 KB
L2_SIZE = 256 * 1024         # 256 KB
TLB_ENTRIES = 64
PAGE_SIZE = 4096             # 4 KB
TLB_SIZE = TLB_ENTRIES * PAGE_SIZE
# ---------------------------------------------------------------

# Helper: map radix bits to average cluster size (in tuples).
# We reuse the same heuristic as the radix‑join script.
def cluster_size_bits(B: int, C: int) -> int:
    return max(1, 2 ** max(0, int(math.log2(C)) - B))

def partitioned_hash_metrics(B: int, C: int) -> dict:
    # Number of clusters (approx.)
    H = cluster_size_bits(B, C)
    # Assume each tuple is 8 bytes.
    TUPLE_SIZE = 8
    cluster_bytes = H * TUPLE_SIZE
    # Placeholder reuse counts (the exact numbers are not given)
    Re_L1 = Re_L2 = Re_Pg = 1
    # L1 / L2 miss calculations following the paper's formula.
    def miss_Li(cluster_bytes, Li_size):
        if cluster_bytes <= Li_size:
            return 3 * Re_L1 + C * (cluster_bytes / Li_size)
        else:
            # The paper uses a factor of 10 for the penalty when clusters are larger.
            return 3 * Re_L1 + C * 10 * (1 - Li_size / cluster_bytes)
    M_L1 = miss_Li(cluster_bytes, L1_SIZE)
    M_L2 = miss_Li(cluster_bytes, L2_SIZE)
    # TLB miss calculation
    if cluster_bytes <= TLB_SIZE:
        M_TLB = 3 * Re_Pg + C * (cluster_bytes / TLB_SIZE)
    else:
        # Use the same 10× penalty as in the L_i formula (paper has a typo).
        M_TLB = 3 * Re_Pg + C * 10 * (1 - TLB_SIZE / cluster_bytes)
    # Runtime according to the analytical model for partitioned hash‑join.
    runtime = (
        C * w_h +
        H * w_h_prime +
        M_L1 * l_L2 +
        M_L2 * l_Mem +
        M_TLB * l_TLB
    )
    return {
        "L1_misses": M_L1,
        "L2_misses": M_L2,
        "TLB_misses": M_TLB,
        "runtime_ms": runtime * 1e3,
    }

def main():
    out_dir = Path(__file__).parent / "output"
    out_dir.mkdir(exist_ok=True)
    csv_path = out_dir / "partitioned_hash_join_results.csv"
    cardinalities = [64_000_000, 8_000_000, 1_000_000, 125_000, 15_625]
    bits_range = range(0, 26)
    with open(csv_path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["bits", "cardinality", "L1_misses", "L2_misses", "TLB_misses", "runtime_ms"])
        for B in bits_range:
            for C in cardinalities:
                metrics = partitioned_hash_metrics(B, C)
                writer.writerow([
                    B, C,
                    metrics["L1_misses"],
                    metrics["L2_misses"],
                    metrics["TLB_misses"],
                    metrics["runtime_ms"],
                ])
    # Plotting (requires matplotlib & pandas)
    try:
        import matplotlib.pyplot as plt
        import pandas as pd
        df = pd.read_csv(csv_path)
        def plot(metric, ylabel, filename, logy=False):
            plt.figure(figsize=(8,5))
            for C in cardinalities:
                sub = df[df["cardinality"] == C]
                plt.plot(sub["bits"], sub[metric], label=f"C={C}")
            plt.xlabel("Number of radix bits")
            plt.ylabel(ylabel)
            if logy:
                plt.yscale("log")
            plt.title(f"Partitioned Hash‑Join: {ylabel} vs bits")
            plt.legend()
            plt.grid(True, which="both", ls="--", lw=0.5)
            plt.tight_layout()
            plt.savefig(out_dir / filename)
            plt.close()
        plot("L1_misses", "L1 cache misses", "hash_L1.png", logy=True)
        plot("L2_misses", "L2 cache misses", "hash_L2.png", logy=True)
        plot("TLB_misses", "TLB misses", "hash_TLB.png", logy=True)
        plot("runtime_ms", "Runtime (ms)", "hash_time.png", logy=False)
    except Exception as e:
        print("Plotting skipped (matplotlib not available):", e)

if __name__ == "__main__":
    main()
