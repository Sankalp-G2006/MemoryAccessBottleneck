import csv
import sys
from collections import defaultdict
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


# Intel Core Ultra 7 155H cache specifications
CACHE_LINE = 64

L1D = 48 * 1024
L1I = 64 * 1024
L2 = 2 * 1024 * 1024
L3 = 24 * 1024 * 1024

INPUT_FILE = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("stride_results.csv")
OUTPUT_FILE = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("stride_plot_B.png")


def cache_lines_touched(stride, iterations):
    if stride >= CACHE_LINE:
        return iterations

    return ((iterations - 1) * stride) // CACHE_LINE + 1


def main():
    measurements = defaultdict(list)
    iteration_counts = set()

    with INPUT_FILE.open(newline="", encoding="utf-8") as file:
        reader = csv.DictReader(file)

        required = {"stride_bytes", "iterations", "elapsed_seconds"}

        if not required.issubset(reader.fieldnames or []):
            raise SystemExit(f"CSV must contain columns: {required}")

        for row in reader:
            stride = int(row["stride_bytes"])
            iterations = int(row["iterations"])
            elapsed = float(row["elapsed_seconds"]) * 1000

            measurements[stride].append(elapsed)
            iteration_counts.add(iterations)

    if not measurements:
        raise SystemExit("No measurements found.")

    if len(iteration_counts) != 1:
        raise SystemExit("Inconsistent iteration counts.")

    iterations = iteration_counts.pop()
    strides = sorted(measurements)

    averages = [
        sum(measurements[s]) / len(measurements[s])
        for s in strides
    ]

    # Capacity-equivalent stride references.
    references = {
        "L1 Data (48 KiB)": L1D / (iterations - 1),
        "L1 Instruction (64 KiB)": L1I / (iterations - 1),
        "L2 (2 MiB)": L2 / (iterations - 1),
        "L3 (24 MiB, address-span reference)": L3 / (iterations - 1),
        "Cache line (64 B)": CACHE_LINE
    }

    fig, ax = plt.subplots(figsize=(13, 7))

    ax.plot(
        strides,
        averages,
        marker="o",
        linewidth=1.5,
        label="Mean measured time"
    )

    styles = {
        "L1 Data (48 KiB)": ":",
        "L1 Instruction (64 KiB)": "-.",
        "L2 (2 MiB)": "--",
        "L3 (24 MiB, address-span reference)": "-.",
        "Cache line (64 B)": "--"
    }

    # Display all cache and cache-line references.
    for label, position in references.items():
        ax.axvline(
            position,
            linestyle=styles[label],
            linewidth=1.5,
            label=f"{label}: {position:.3f} B"
        )

    # Extend the axis to include all markers.
    ax.set_xlim(
        min(strides[0], references["L1 Data (48 KiB)"]) * 0.8,
        max(strides[-1], references["L3 (24 MiB, address-span reference)"]) * 1.1
    )

    ax.set_xscale("log", base=2)
    ax.set_xlabel("Stride length (bytes; log₂ scale)")
    ax.set_ylabel("Mean elapsed time (milliseconds)")
    ax.set_title("Experiment B: 1 GiB Buffer — Memory Scan Time vs. Stride")

    ax.grid(True, which="both", alpha=0.3)
    ax.legend(fontsize=8)

    max_lines = max(
        cache_lines_touched(s, iterations) for s in strides
    )

    footprint_mib = max_lines * CACHE_LINE / (1024 * 1024)

    fig.text(
        0.08,
        0.015,
        (
            f"Maximum estimated cache-line footprint: {footprint_mib:.2f} MiB.\n"
            "L1I is shown for completeness but is not directly tested. "
            "L3 marker represents address span, not actual cache footprint."
        ),
        fontsize=8
    )

    fig.tight_layout(rect=(0, 0.08, 1, 1))
    fig.savefig(OUTPUT_FILE, dpi=300)
    plt.close(fig)

    print("\nMean execution times:")
    for stride, average in zip(strides, averages):
        print(
            f"Stride {stride:5d} B: {average:.4f} ms "
            f"({len(measurements[stride])} runs)"
        )

    print(f"\nMaximum cache-line footprint: {footprint_mib:.2f} MiB")
    print(f"Graph saved to {OUTPUT_FILE}")


if __name__ == "__main__":
    main()