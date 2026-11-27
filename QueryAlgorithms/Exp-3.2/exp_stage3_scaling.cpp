#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <chrono>
#include <random>
#include <iomanip>
#include <algorithm>

struct Tuple {
    uint32_t oid;
    uint32_t val;
};

inline uint32_t hash_val(uint32_t x) {
    x ^= x >> 16;
    x *= 0x85ebca6b;
    x ^= x >> 13;
    x *= 0xc2b2ae35;
    x ^= x >> 16;
    return x;
}

std::vector<size_t> cluster_1pass(const std::vector<Tuple>& input,
                                  std::vector<Tuple>& output,
                                  int bits) {
    const size_t N = input.size();
    if (bits <= 0) {
        output = input;
        return {0, N};
    }

    const size_t H = 1ULL << bits;
    const uint32_t mask = static_cast<uint32_t>(H - 1);

    std::vector<size_t> counts(H, 0);
    for (size_t i = 0; i < N; ++i) {
        counts[hash_val(input[i].val) & mask]++;
    }

    std::vector<size_t> bounds(H + 1, 0);
    std::vector<size_t> write_pos(H);
    for (size_t b = 0; b < H; ++b) {
        write_pos[b] = bounds[b];
        bounds[b + 1] = bounds[b] + counts[b];
    }

    for (size_t i = 0; i < N; ++i) {
        uint32_t b = hash_val(input[i].val) & mask;
        output[write_pos[b]++] = input[i];
    }
    return bounds;
}

struct HTEntry {
    uint32_t val;
    uint32_t oid;
    uint32_t used;
};

uint64_t hash_join_bucket(const Tuple* __restrict L_ptr, size_t L_len,
                          const Tuple* __restrict R_ptr, size_t R_len,
                          std::vector<HTEntry>& ht_buf) {
    if (L_len == 0 || R_len == 0) return 0;

    size_t cap = 8;
    while (cap < R_len * 2) cap <<= 1;
    const size_t mask = cap - 1;

    if (ht_buf.size() < cap) ht_buf.resize(cap);
    std::fill(ht_buf.begin(), ht_buf.begin() + cap, HTEntry{0, 0, 0});

    for (size_t i = 0; i < R_len; ++i) {
        uint32_t h = hash_val(R_ptr[i].val);
        size_t idx = ((h >> 16) ^ h) & mask;
        while (ht_buf[idx].used) idx = (idx + 1) & mask;
        ht_buf[idx] = {R_ptr[i].val, R_ptr[i].oid, 1};
    }

    uint64_t matches = 0;
    for (size_t i = 0; i < L_len; ++i) {
        uint32_t key = L_ptr[i].val;
        uint32_t h = hash_val(key);
        size_t idx = ((h >> 16) ^ h) & mask;
        while (ht_buf[idx].used) {
            if (ht_buf[idx].val == key) matches++;
            idx = (idx + 1) & mask;
        }
    }
    return matches;
}

// Averages over `iters` runs (after 1 warmup) to eliminate Windows timer quantization
double benchmark_join_ms(const std::vector<Tuple>& L,
                         const std::vector<Tuple>& R,
                         int bits,
                         int iters,
                         uint64_t& out_matches) {
    const size_t N = L.size();
    std::vector<Tuple> L_clust(N), R_clust(N);
    std::vector<HTEntry> reusable_ht;

    auto run_once = [&]() -> uint64_t {
        std::vector<size_t> L_bounds = cluster_1pass(L, L_clust, bits);
        std::vector<size_t> R_bounds = cluster_1pass(R, R_clust, bits);
        uint64_t matches = 0;
        size_t H = L_bounds.size() - 1;
        for (size_t c = 0; c < H; ++c) {
            matches += hash_join_bucket(
                L_clust.data() + L_bounds[c], L_bounds[c + 1] - L_bounds[c],
                R_clust.data() + R_bounds[c], R_bounds[c + 1] - R_bounds[c],
                reusable_ht
            );
        }
        return matches;
    };

    // Warmup run (brings pages into OS working set & stabilizes CPU boost)
    out_matches = run_once();

    auto t0 = std::chrono::high_resolution_clock::now();
    for (int it = 0; it < iters; ++it) {
        out_matches = run_once();
    }
    auto t1 = std::chrono::high_resolution_clock::now();

    double total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return total_ms / iters;
}

int main() {
    std::cout << "=== STAGE 3 SCALING (Averaged): Simple HJ vs. 1-Pass Partitioned HJ ===\n";
    std::cout << std::setw(10) << "Tuples(N)"
              << std::setw(8)  << "Bits"
              << std::setw(16) << "Simple(ms)"
              << std::setw(16) << "1PassPart(ms)"
              << std::setw(16) << "Simple(ns/tup)"
              << std::setw(18) << "1PassPart(ns/tup)" << "\n";
    std::cout << std::string(84, '-') << "\n";

    std::ofstream csv("stage3_scaling_simple_vs_1pass.csv");
    csv << "N,bits,clusters,simple_hj_ms,part_1pass_ms,simple_ns_per_tup,part_ns_per_tup\n";

    std::mt19937 rng(100);
    std::vector<size_t> sizes = {
        4096, 8192, 16384, 32768, 65536, 131072, 262144,
        524288, 1048576, 2097152, 4194304, 8388608
    };

    for (size_t N : sizes) {
        std::vector<Tuple> L(N), R(N);
        for (size_t i = 0; i < N; ++i) {
            R[i] = {static_cast<uint32_t>(i), static_cast<uint32_t>(i * 2)};
            L[i] = {static_cast<uint32_t>(i), static_cast<uint32_t>(rng() % (N * 2))};
        }

        // Target ~4,096 tuples per cluster (~96 KB HT, fits in L2 cache)
        int B = 0;
        while ((N >> B) > 4096) B++;

        // Scale iterations so every benchmark runs for at least ~150-300 ms total
        int iters = std::max(3, static_cast<int>(16'000'000 / N));

        uint64_t m1 = 0, m2 = 0;
        double simple_ms = benchmark_join_ms(L, R, 0, iters, m1);
        double part1_ms  = benchmark_join_ms(L, R, B, iters, m2);

        if (m1 != m2) {
            std::cerr << "ERROR: Match count mismatch at N=" << N << "\n";
            return 1;
        }

        double simple_ns = (simple_ms * 1e6) / N;
        double part1_ns  = (part1_ms  * 1e6) / N;

        std::cout << std::setw(10) << N
                  << std::setw(8)  << B
                  << std::setw(16) << std::fixed << std::setprecision(3) << simple_ms
                  << std::setw(16) << part1_ms
                  << std::setw(16) << std::setprecision(1) << simple_ns
                  << std::setw(18) << part1_ns << "\n";

        csv << N << "," << B << "," << (1ULL << B) << ","
            << simple_ms << "," << part1_ms << ","
            << simple_ns << "," << part1_ns << "\n";
    }

    csv.close();
    std::cout << "\nSaved clean results to stage3_scaling_simple_vs_1pass.csv!\n";
    return 0;
}