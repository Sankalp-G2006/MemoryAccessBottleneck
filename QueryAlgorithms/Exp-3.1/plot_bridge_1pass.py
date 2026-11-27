import pandas as pd
import matplotlib.pyplot as plt

df = pd.read_csv("bridge_1pass_join.csv")

plt.figure(figsize=(8.5, 5))
plt.plot(df["bits"], df["total_ms"], marker="o", linewidth=2.2, color="tab:blue", label="Total Join Time")

# Highlight B=0 (Simple Hash-Join)
plt.annotate("B=0: Simple Hash-Join\n(Giant HT spills to RAM)",
             xy=(0, df.loc[0, "total_ms"]),
             xytext=(1.2, df.loc[0, "total_ms"] * 0.92),
             arrowprops=dict(arrowstyle="->", color="black"), fontsize=9)

# Highlight the sweet-spot minimum
min_idx = df["total_ms"].idxmin()
min_b = int(df.loc[min_idx, "bits"])
min_t = df.loc[min_idx, "total_ms"]
plt.annotate(f"Sweet Spot (B={min_b})\nClusters fit in L1/L2 cache",
             xy=(min_b, min_t),
             xytext=(min_b - 1.5, min_t + (df["total_ms"].max() - min_t) * 0.25),
             arrowprops=dict(arrowstyle="->", color="green"), color="darkgreen", fontsize=9)

plt.xlabel("Number of Radix Bits (B)  [B=0 is Simple Hash-Join]")
plt.ylabel("Avg. Total Elapsed Time (millisecs)")
plt.title("Bridge Experiment: Simple Hash-Join vs. 1-Pass Partitioned Hash-Join (N=4M)")
plt.xticks(range(0, 17))
plt.grid(True, linestyle="--", alpha=0.4)
plt.legend()
plt.tight_layout()
plt.savefig("bridge_1pass_ucurve.png", dpi=150)
plt.show()