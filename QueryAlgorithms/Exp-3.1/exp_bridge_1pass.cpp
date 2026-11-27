#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <chrono>
#include <random>
#include <iomanip>
#include <algorithm>
using namespace std;

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

// Strict 1-Pass Clustering: partitions `input` into 2^bits contiguous buckets
vector<size_t> cluster_1pass(const vector<Tuple>& input,
                             vector<Tuple>& output,
                             int bits) {
    const size_t N = input.size();
    if (bits == 0) {
        output = input;
        return {0, N};
    }

    const size_t H = 1ULL << bits;
    const uint32_t mask = static_cast<uint32_t>(H - 1);

    // 1. Histogram
    vector<size_t> counts(H, 0);
    for (size_t i = 0; i < N; ++i) {
        counts[hash_val(input[i].val) & mask]++;
    }

    // 2. Prefix sum -> cluster boundary offsets [0, b1, b2, ..., N]
    vector<size_t> bounds(H + 1, 0);
    vector<size_t> write_pos(H);
    for (size_t b = 0; b < H; ++b) {
        write_pos[b] = bounds[b];
        bounds[b + 1] = bounds[b] + counts[b];
    }

    // 3. Scatter (triggers TLB & cache thrashing once H exceeds hardware limits!)
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

// Build + Probe Hash-Join on a single matching cluster pair (L_c, R_c)
uint64_t hash_join_bucket(const Tuple* __restrict L_ptr, size_t L_len,
                          const Tuple* __restrict R_ptr, size_t R_len,
                          vector<HTEntry>& ht_buf) {
    if (L_len == 0 || R_len == 0) return 0;

    size_t cap = 8;
    while (cap < R_len * 2) cap <<= 1;
    const size_t mask = cap - 1;

    if (ht_buf.size() < cap) ht_buf.resize(cap);
    fill(ht_buf.begin(), ht_buf.begin() + cap, HTEntry{0, 0, 0});

    // Build inner hash table on R_c
    for (size_t i = 0; i < R_len; ++i) {
        uint32_t h = hash_val(R_ptr[i].val);
        size_t idx = ((h >> 16) ^ h) & mask;
        while (ht_buf[idx].used) {
            idx = (idx + 1) & mask;
        }
        ht_buf[idx] = {R_ptr[i].val, R_ptr[i].oid, 1};
    }

    // Probe with outer cluster L_c
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

int main() {
    const size_t N = 4'000'000;
    const int TRIALS = 5;
    cout << "Generating L and R (" << N << " tuples each = 32 MB per relation, Avg of " << TRIALS << " Trials)...\n";

    vector<Tuple> L(N), R(N);
    mt19937 rng(42);
    for (size_t i = 0; i < N; ++i) {
        R[i] = {static_cast<uint32_t>(i), static_cast<uint32_t>(i * 2)};
        L[i] = {static_cast<uint32_t>(i), static_cast<uint32_t>(rng() % (N * 2))};
    }

    ofstream csv("bridge_1pass_join.csv");
    csv << "bits,clusters,ht_kb_per_cluster,cluster_ms,join_ms,total_ms,matches\n";

    cout << "\n"
         << setw(6)  << "Bits"
         << setw(10) << "Clusters"
         << setw(14) << "HT/Clust(KB)"
         << setw(14) << "1PassClust(ms)"
         << setw(14) << "HashJoin(ms)"
         << setw(14) << "Total(ms)" << "\n";
    cout << string(72, '-') << "\n";

    vector<Tuple> L_clust(N), R_clust(N);
    vector<HTEntry> reusable_ht;
    uint64_t expected_matches = 0;

    for (int B = 0; B <= 16; ++B) {
        size_t H = 1ULL << B;
        double sum_c_ms = 0.0, sum_j_ms = 0.0;
        uint64_t matches = 0;

        // 1 Warmup + TRIALS timed passes
        for (int t = -1; t < TRIALS; ++t) {
            auto t0 = chrono::high_resolution_clock::now();
            vector<size_t> L_bounds = cluster_1pass(L, L_clust, B);
            vector<size_t> R_bounds = cluster_1pass(R, R_clust, B);
            auto t1 = chrono::high_resolution_clock::now();

            uint64_t run_matches = 0;
            for (size_t c = 0; c < H; ++c) {
                run_matches += hash_join_bucket(
                    L_clust.data() + L_bounds[c], L_bounds[c + 1] - L_bounds[c],
                    R_clust.data() + R_bounds[c], R_bounds[c + 1] - R_bounds[c],
                    reusable_ht
                );
            }
            auto t2 = chrono::high_resolution_clock::now();

            if (t == -1) {
                matches = run_matches;
            } else {
                sum_c_ms += (B == 0) ? 0.0 : chrono::duration<double, milli>(t1 - t0).count();
                sum_j_ms += chrono::duration<double, milli>(t2 - t1).count();
            }
        }

        if (B == 0) expected_matches = matches;
        if (matches != expected_matches) {
            cerr << "ERROR: Match count mismatch at B=" << B << "\n";
            return 1;
        }

        double avg_c_ms = sum_c_ms / TRIALS;
        double avg_j_ms = sum_j_ms / TRIALS;
        double avg_tot_ms = avg_c_ms + avg_j_ms;

        // Approximate hash table size per cluster in KB (12 bytes per HTEntry * 2x capacity)
        size_t avg_r = N >> B;
        size_t cap = 8;
        while (cap < avg_r * 2) cap <<= 1;
        double ht_kb = (cap * sizeof(HTEntry)) / 1024.0;

        cout << setw(6)  << B
             << setw(10) << H
             << setw(14) << fixed << setprecision(1) << ht_kb
             << setw(14) << setprecision(2) << avg_c_ms
             << setw(14) << avg_j_ms
             << setw(14) << avg_tot_ms << "\n";

        csv << B << "," << H << "," << ht_kb << "," << avg_c_ms << "," << avg_j_ms << "," << avg_tot_ms << "," << matches << "\n";
    }

    csv.close();
    cout << "\nSaved averaged results to bridge_1pass_join.csv!\n";
    return 0;
}