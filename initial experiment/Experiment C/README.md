# Experiment C — Random Memory Access vs. Working-Set Size

## Objective

Measure how average random memory-access time changes as the working set grows across the CPU cache hierarchy. Unlike the stride experiments, each read targets a randomly selected cache line, reducing the usefulness of conventional hardware prefetching.

## Configuration

- Data file and allocated data buffer: 1 GiB
- Cache-eviction buffer: 64 MiB
- Accesses per run: 200,000
- Repetitions per working-set size: 200
- Access pattern: uniform pseudorandom selection with replacement
- Accessed location: first byte of a randomly selected 64-byte cache line
- Random indices: generated before timing; the same sequence is reused for every repetition of a working-set size
- Working-set sizes: 16, 32, 48, 64, 96, 128, and 256 KiB; 512 KiB; 1, 2, 4, 8, 16, 24, 32, 64, 128, 256, 512 MiB; and 1 GiB

The working-set list includes points around the cache capacities used in the project's earlier analysis: L1D 48 KiB, L2 2 MiB, and L3 24 MiB. The L1 instruction cache is shown on the plot for reference only; this benchmark measures data loads.

## Build and run (WSL / Ubuntu)

Install dependencies:

```bash
sudo apt update
sudo apt install -y build-essential python3 python3-venv
```

From this directory:

```bash
gcc -O2 -std=c11 -Wall -Wextra initialize_data.c -o initialize_data
gcc -O2 -std=c11 -Wall -Wextra random_benchmark.c -o random_benchmark
python3 -m venv venv
source venv/bin/activate
pip install matplotlib
./initialize_data
./random_benchmark
python3 plot_results.py
```

The run creates `random_buffer.bin` (1 GiB), `random_results.csv`, and `random_plot.png`.

To use alternate output names:

```bash
./random_benchmark random_buffer.bin experiment_c_run2.csv
python3 plot_results.py experiment_c_run2.csv experiment_c_run2.png
```

## Measurement details and limitations

- The file is loaded into a `malloc`-allocated buffer before timing. File I/O and random-index generation are excluded from the timed region.
- Each index identifies a 64-byte cache line, and the benchmark reads one byte at that line's start. Sampling is with replacement, so a line may be selected multiple times in a run.
- The pre-generated index array is traversed sequentially during the timed loop. It is separate from the data working set, but its own cache behavior is part of the benchmark overhead.
- A 64 MiB buffer is touched before each run to put pressure on the caches. This is best-effort cache eviction, not a guarantee that all cache levels are empty.
- Random access makes conventional stride/stream prefetching less effective; it does not guarantee that all hardware prefetching is disabled.
- The plotted result is average access time under repeated random sampling, not the isolated latency of a single cache miss. Repeated sampling allows cache hits and misses to coexist.
- The benchmark performs 800 million timed data reads in total (20 working-set sizes × 200 repetitions × 200,000 reads), so a complete run may take a while.
- Run from the WSL Linux filesystem (for example, `~/projects`), not `/mnt/c`, and avoid heavy background workloads.
