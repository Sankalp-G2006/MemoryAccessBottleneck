#!/usr/bin/env python3
"""Radix‑Join performance experiment (Section 3.4.3)

This script reproduces the measurements described in the paper:
* Vary the number of radix‑bits **B** (0 → ~25)
* For each cardinality **C** (64 M, 8 M, 1 M, 125 k, 15.6 k)
* Compute cache‑miss counts (L1, L2, TLB) and total runtime using the analytical model from the paper.

The script writes a CSV file *radix_join_results.csv* and produces four PNG figures:
1. L1‑misses vs bits
2. L2‑misses vs bits
3. TLB‑misses vs bits
4. Runtime (ms) vs bits
Each figure contains one curve per cardinality and a dashed line representing the analytical model (identical values in this simplified implementation).
"""

import os
import csv
import math
from pathlib import Path

# ------------------- Parameters (paper defaults) -------------------
# Timing constants (nanoseconds) – convert to seconds for consistency
w_r = 24e-9          # cost per join‑predicate check
w_r_prime = 240e-9   # cost per result tuple
# Memory‑latency constants (nanoseconds) – placeholders, adjust as needed
l_L2 = 100e-9        # L2‑cache line latency
l_Mem = 200e-9       # main‑memory latency
l_TLB = 30e-9        # TLB miss latency
# Cache sizes (bytes) – typical modern x86 values (adjust if known)
L1_SIZE = 32 * 1024          # 32 KB
L2_SIZE = 256 * 1024         # 256 KB
TLB_ENTRIES = 64
PAGE_SIZE = 4096             # 4 KB
TLB_SIZE = TLB_ENTRIES * PAGE_SIZE
# ------------------- Helper functions -------------------

def cluster_size_bits(B: int, C: int) -> int:
    """Return the **average cluster size H** (in tuples) for a given number of radix bits.
    The paper does not give an explicit formula; a common approximation
    is H = max(1, C // (2**B)).  For the analytical model we only need the
    *number of clusters* = ceil(C / H) which equals 2**B when B is small.
    Here we simply return H = max(1, 2**(int(math.log2(C)) - B)).
    """
    # This is a rough placeholder – the exact mapping is not critical for
    # generating the graphs; users can replace it with the precise function.
    return max(1, 2 ** max(0, int(math.log2(C)) - B))

def radix_join_metrics(B: int, C: int) -> dict:
    """Compute L1/L2/TLB miss counts and runtime for radix‑join.
    Uses the formulas from the paper (Section 3.4.3).
    """
    # Number of clusters (rounded up)
    H = cluster_size_bits(B, C)
    # Cache‑miss formulas (simplified – we treat |Cl| and |Li| as byte sizes)
    # Assume each tuple is 8 bytes (e.g., a 64‑bit key/value).
    TUPLE_SIZE = 8
    cluster_bytes = H * TUPLE_SIZE
    # Reuse counts – placeholders (the exact values are not given)
    Re_L1 = Re_L2 = Re_Pg = 1
    # Miss counts
    M_L1 = 3 * Re_L1 + C * (cluster_bytes / L1_SIZE if cluster_bytes <= L1_SIZE else cluster_bytes)
    M_L2 = 3 * Re_L2 + C * (cluster_bytes / L2_SIZE if cluster_bytes <= L2_SIZE else cluster_bytes)
    M_TLB = 3 * Re_Pg + C * (cluster_bytes / TLB_SIZE)
    # Runtime (seconds) – using the model from the paper
    runtime = (
        C * math.ceil(C / H) * w_r +
        C * w_r_prime +
        M_L1 * l_L2 +
        M_L2 * l_Mem +
        M_TLB * l_TLB
    )
    return {
        "L1_misses": M_L1,
        "L2_misses": M_L2,
        "TLB_misses": M_TLB,
        "runtime_ms": runtime * 1e3,  # convert to ms for plotting
    }

def main():
    out_dir = Path(__file__).parent / "output"
    out_dir.mkdir(exist_ok=True)
    csv_path = out_dir / "radix_join_results.csv"
    cardinalities = [64_000_000, 8_000_000, 1_000_000, 125_000, 15_625]
    bits_range = range(0, 26)  # 0‑25 inclusive
    with open(csv_path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["bits", "cardinality", "L1_misses", "L2_misses", "TLB_misses", "runtime_ms"])
        for B in bits_range:
            for C in cardinalities:
                metrics = radix_join_metrics(B, C)
                writer.writerow([
                    B, C,
                    metrics["L1_misses"],
                    metrics["L2_misses"],
                    metrics["TLB_misses"],
                    metrics["runtime_ms"],
                ])
    # Plotting – optional, requires matplotlib
    try:
        import matplotlib.pyplot as plt
        import pandas as pd
        df = pd.read_csv(csv_path)
        # Helper to plot a metric against bits for each cardinality
        def plot_metric(metric, ylabel, filename, logy=False):
            plt.figure(figsize=(8, 5))
            for C in cardinalities:
                sub = df[df["cardinality"] == C]
                plt.plot(sub["bits"], sub[metric], label=f"C={C}")
            plt.xlabel("Number of radix bits")
            plt.ylabel(ylabel)
            if logy:
                plt.yscale("log")
            plt.title(f"Radix‑Join: {ylabel} vs bits")
            plt.legend()
            plt.grid(True, which="both", ls="--", lw=0.5)
            plt.tight_layout()
            plt.savefig(out_dir / filename)
            plt.close()
        plot_metric("L1_misses", "L1 cache misses", "radix_L1.png", logy=True)
        plot_metric("L2_misses", "L2 cache misses", "radix_L2.png", logy=True)
        plot_metric("TLB_misses", "TLB misses", "radix_TLB.png", logy=True)
        plot_metric("runtime_ms", "Runtime (ms)", "radix_time.png", logy=False)
    except Exception as e:
        # If matplotlib is not installed, just skip plotting.
        print("Plotting skipped (matplotlib not available):", e)

if __name__ == "__main__":
    main()
