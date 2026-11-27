import pandas as pd
import matplotlib.pyplot as plt

df = pd.read_csv("stage3_scaling_simple_vs_1pass.csv")

plt.figure(figsize=(8.5, 5))
plt.plot(df["N"], df["simple_ns_per_tup"], marker="^", linestyle="--", linewidth=2, label="Simple Hash-Join (B=0)")
plt.plot(df["N"], df["part_ns_per_tup"], marker="o", linestyle="-", linewidth=2.2, label="1-Pass Partitioned HJ (L2-sized)")

plt.xscale("log")
plt.xlabel("Relation Cardinality N (log scale)")
plt.ylabel("Avg. Time per Tuple (nanoseconds)")
plt.title("Scaling Behavior: Simple Hash-Join vs. 1-Pass Partitioned Hash-Join")
plt.grid(True, which="both", linestyle="--", alpha=0.4)
plt.legend()
plt.tight_layout()
plt.savefig("stage3_scaling_simple_vs_1pass.png", dpi=150)
plt.show()