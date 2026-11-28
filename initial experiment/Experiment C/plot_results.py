import csv
import math
import sys
from collections import defaultdict
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

CACHE_LINE = 64
L1D = 48 * 1024
L1I = 64 * 1024
L2 = 2 * 1024 * 1024
L3 = 24 * 1024 * 1024
MAX_WORKING_SET = 64 * 1024 * 1024

INPUT_FILE = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("random_results.csv")
OUTPUT_FILE = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("random_plot_C.png")


def mean(values):
    return sum(values) / len(values)


def sample_stddev(values):
    if len(values) < 2:
        return 0.0
    average = mean(values)
    return math.sqrt(sum((value - average) ** 2 for value in values) / (len(values) - 1))


def format_size(byte_count):
    gib = 1024 * 1024 * 1024
    mib = 1024 * 1024
    if byte_count >= gib:
        return f"{byte_count / gib:g} GiB"
    if byte_count >= mib:
        return f"{byte_count / mib:g} MiB"
    return f"{byte_count / 1024:g} KiB"


def main():
    measurements = defaultdict(list)
    iteration_counts = set()

    with INPUT_FILE.open(newline="", encoding="utf-8") as file:
        reader = csv.DictReader(file)
        required = {"working_set_bytes", "iterations", "elapsed_seconds"}
        if not required.issubset(reader.fieldnames or []):
            raise SystemExit(f"CSV must contain columns: {required}")

        for row in reader:
            working_set = int(row["working_set_bytes"])
            if working_set > MAX_WORKING_SET:
                continue
            iterations = int(row["iterations"])
            elapsed = float(row["elapsed_seconds"])
            measurements[working_set].append(elapsed * 1e9 / iterations)
            iteration_counts.add(iterations)

    if not measurements:
        raise SystemExit("No measurements found.")
    if len(iteration_counts) != 1:
        raise SystemExit("Inconsistent iteration counts.")

    sizes = sorted(measurements)
    averages = [mean(measurements[size]) for size in sizes]
    deviations = [sample_stddev(measurements[size]) for size in sizes]

    fig, ax = plt.subplots(figsize=(13, 7))
    ax.plot(
        sizes,
        averages,
        marker="o",
        linewidth=1.5,
        label="Mean time per random access"
    )

    references = {
        "L1 Data (48 KiB)": L1D,
        "L1 Instruction (64 KiB)": L1I,
        "L2 (2 MiB)": L2,
        "L3 (24 MiB)": L3,
    }
    styles = {
        "L1 Data (48 KiB)": ":",
        "L1 Instruction (64 KiB)": "-.",
        "L2 (2 MiB)": "--",
        "L3 (24 MiB)": "-.",
    }

    for label, position in references.items():
        ax.axvline(position, linestyle=styles[label], linewidth=1.4,
                   label=f"{label}: {format_size(position)}")

    max_y = max(averages)
    ax.set_xscale("linear")
    ax.set_xlim(0, max(sizes) * 1.05)
    ax.set_ylim(0, max_y * 1.2 if max_y > 0 else 1.0)
    ax.set_xlabel("Random-access working-set size (bytes; linear scale)")
    ax.set_ylabel("Mean elapsed time per access (nanoseconds)")
    ax.set_title("Experiment C: Random Memory Access Time vs. Working-Set Size")
    ax.grid(True, which="both", alpha=0.3)
    ax.legend(fontsize=8)
    fig.text(
        0.08, 0.015,
        "Each access selects one byte at the start of a randomly selected 64-byte cache line. "
        "Indices are pre-generated; sampling is with replacement. Cache eviction is best-effort. "
        "L1I is shown for reference but is not directly tested.",
        fontsize=8
    )
    fig.tight_layout(rect=(0, 0.07, 1, 1))
    fig.savefig(OUTPUT_FILE, dpi=300)
    plt.close(fig)

    print("\nMean time per random access:")
    for size, average, deviation in zip(sizes, averages, deviations):
        print(f"{format_size(size):>10}: {average:.2f} ns ± {deviation:.2f} ns "
              f"({len(measurements[size])} runs)")
    print(f"\nGraph saved to {OUTPUT_FILE}")


if __name__ == "__main__":
    main()