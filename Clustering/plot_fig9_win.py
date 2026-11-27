import pandas as pd
import matplotlib.pyplot as plt

df = pd.read_csv("fig9_win.csv")

fig, axes = plt.subplots(4, 1, figsize=(8, 12), sharex=True)

metrics = [
    ("l1_misses",  "L1 misses"),
    ("l2_misses",  "L2 misses"),
    ("tlb_misses", "TLB misses"),
    ("ms",         "millisecs")
]

styles = {
    1: {"marker": "+", "linestyle": "-",  "label": "1 pass"},
    2: {"marker": "x", "linestyle": "--", "label": "2 passes"},
    3: {"marker": "*", "linestyle": ":",  "label": "3 passes"},
    4: {"marker": "s", "linestyle": "-.", "label": "4 passes", "fillstyle": "none"},
}

for ax, (col, ylabel) in zip(axes, metrics):
    for p in [4, 3, 2, 1]:
        sub = df[df["passes"] == p]
        ax.plot(sub["bits"], sub[col], markersize=7, **styles[p])
    ax.set_yscale("log")
    ax.set_ylabel(ylabel)
    ax.grid(True, which="both", linestyle="--", alpha=0.35)

    for x_val in [6, 10, 12, 15, 18]:
        ax.axvline(x=x_val, color="gray", linestyle=":", alpha=0.6)

axes[0].legend(loc="lower right", ncol=2)
axes[0].set_title("Replication of Figure 9 (Windows Real Timing + Hardware Cache/TLB Sim)")
axes[-1].set_xlabel("number of bits")
axes[-1].set_xticks(range(1, 19))

plt.tight_layout()
plt.savefig("figure9_windows_4panels.png", dpi=150)
plt.show()