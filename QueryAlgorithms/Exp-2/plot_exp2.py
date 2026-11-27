import pandas as pd
import matplotlib.pyplot as plt

df = pd.read_csv("sec3_2_grouping.csv")

plt.figure(figsize=(8.5, 5))
plt.plot(df["hashtable_kb"], df["sort_merge_ms"], marker="o", linestyle="--", label="Sort/Merge Grouping")
plt.plot(df["hashtable_kb"], df["hash_group_ms"], marker="s", linestyle="-", label="Hash-Grouping")

# Intel Core i5-1135G7 per-core cache sizes: 48 KiB L1d, 1.25 MiB (1280 KiB) L2
plt.axvline(x=48, color="green", linestyle=":", alpha=0.7)
plt.text(54, df["sort_merge_ms"].max()*0.85, "L1d Cache (48 KiB)", color="green", fontsize=9)

plt.axvline(x=1280, color="orange", linestyle=":", alpha=0.7)
plt.text(1450, df["sort_merge_ms"].max()*0.70, "L2 Cache (1.25 MiB)", color="darkorange", fontsize=9)

plt.xscale("log")
plt.yscale("log")
plt.xlabel("Temporary Hash Table Size (KB, log scale)")
plt.ylabel("Avg. Elapsed Time (millisecs, log scale)")
plt.title("Grouping & Aggregation: Sort/Merge vs. Hash-Grouping (8M Tuples)")
plt.grid(True, which="both", linestyle="--", alpha=0.4)
plt.legend()
plt.tight_layout()
plt.savefig("sec3_2_grouping.png", dpi=150)
plt.show()