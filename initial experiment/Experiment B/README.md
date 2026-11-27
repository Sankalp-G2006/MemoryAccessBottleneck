# Experiment B — 1 GiB memory-access benchmark

## Configuration

- Data buffer: 1 GiB
- Cache-eviction buffer: 64 MiB
- Reads per stride per run: 200,000
- Repetitions per stride: 30
- Strides (bytes): 1, 2, 4, 8, 16, 24, 32, 40, 48, 56, 60, 64, 68, 72, 80, 96, 112, 120, 128, 160, 192, 256, 512, 1024, 2048, 4096

## Build and run (WSL / Ubuntu)

Install dependencies:

```bash
sudo apt update
sudo apt install -y build-essential python3 python3-venv
```

From this directory:

```bash
gcc -O2 -std=c11 -Wall -Wextra initialize_data.c -o initialize_data
gcc -O2 -std=c11 -Wall -Wextra stride_benchmark.c -o stride_benchmark
python3 -m venv venv
source venv/bin/activate
pip install matplotlib
./initialize_data
./stride_benchmark
python3 plot_results.py
```

The run creates `scan_buffer.bin` (1 GiB), `stride_results.csv`, and `stride_plot.png`.

To keep the raw output from a previous run, provide a different CSV filename:

```bash
./stride_benchmark scan_buffer.bin experiment_b_run2.csv
python3 plot_results.py experiment_b_run2.csv experiment_b_run2.png
```

## Notes

- The timed region measures memory loads from an already-loaded RAM allocation; file reading is outside the timed region.
- Cache eviction is best-effort. Touching a 64 MiB buffer does not guarantee that every cache level is empty.
- Run from the WSL Linux filesystem (for example, `~/projects`), not `/mnt/c`, and avoid heavy background workloads.
- The benchmark uses a volatile data view to keep the measured loads observable to the compiler.
