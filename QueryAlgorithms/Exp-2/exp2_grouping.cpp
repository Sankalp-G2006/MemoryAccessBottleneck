#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <chrono>
#include <random>
#include <iomanip>

struct GroupTuple {
    uint32_t oid;
    uint32_t group_key;
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

// 1. SORT/MERGE GROUPING: Sort full 64 MB relation, then scan sequentially
double run_sort_merge_grouping(std::vector<GroupTuple> data, uint64_t& checksum) {
    auto t0 = std::chrono::high_resolution_clock::now();

    std::sort(data.begin(), data.end(), [](const GroupTuple& a, const GroupTuple& b) {
        if (a.group_key != b.group_key) return a.group_key < b.group_key;
        return a.oid < b.oid;
    });

    uint64_t total_sum = 0;
    uint32_t curr_key = data[0].group_key;
    uint64_t curr_agg = data[0].val;
    for (size_t i = 1; i < data.size(); ++i) {
        if (data[i].group_key == curr_key) {
            curr_agg += data[i].val;
        } else {
            total_sum += curr_agg;
            curr_key = data[i].group_key;
            curr_agg = data[i].val;
        }
    }
    total_sum += curr_agg;

    auto t1 = std::chrono::high_resolution_clock::now();
    checksum = total_sum;
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

// 2. HASH-GROUPING: Single sequential scan of data + random updates in hash table
double run_hash_grouping(const std::vector<GroupTuple>& data, size_t num_groups, uint64_t& checksum) {
    size_t cap = 1;
    while (cap < num_groups * 2) cap <<= 1;
    const size_t mask = cap - 1;

    struct HashEntry {
        uint32_t key;
        uint32_t used;
        uint64_t sum;
    }; // 16 bytes per entry

    auto t0 = std::chrono::high_resolution_clock::now();

    std::vector<HashEntry> table(cap, {0, 0, 0});

    for (const auto& row : data) {
        size_t idx = hash_val(row.group_key) & mask;
        while (true) {
            if (!table[idx].used) {
                table[idx].used = 1;
                table[idx].key = row.group_key;
                table[idx].sum = row.val;
                break;
            } else if (table[idx].key == row.group_key) {
                table[idx].sum += row.val;
                break;
            }
            idx = (idx + 1) & mask;
        }
    }

    uint64_t total_sum = 0;
    for (const auto& entry : table) {
        if (entry.used) total_sum += entry.sum;
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    checksum = total_sum;
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

int main() {
    const size_t N = 8'000'000;
    const int TRIALS = 5;

    std::cout << "=== EXPERIMENT 2: GROUPING & AGGREGATION (8M Tuples, Avg of " << TRIALS << " Trials) ===\n";
    std::cout << std::setw(12) << "Groups (G)"
              << std::setw(16) << "HashTable KB"
              << std::setw(18) << "Sort/Merge (ms)"
              << std::setw(18) << "Hash-Group (ms)"
              << std::setw(12) << "Speedup" << "\n";
    std::cout << std::string(76, '-') << "\n";

    std::ofstream csv("sec3_2_grouping.csv");
    csv << "groups,hashtable_kb,sort_merge_ms,hash_group_ms\n";

    std::mt19937 rng(99);
    std::vector<size_t> group_counts = {8, 64, 512, 4096, 32768, 262144, 1048576, 4194304};

    for (size_t G : group_counts) {
        std::vector<GroupTuple> data(N);
        for (size_t i = 0; i < N; ++i) {
            data[i] = {static_cast<uint32_t>(i), static_cast<uint32_t>(rng() % G), 1};
        }

        uint64_t c1 = 0, c2 = 0;
        // 1 Warmup pass each
        run_sort_merge_grouping(data, c1);
        run_hash_grouping(data, G, c2);

        double sum_sort_ms = 0.0, sum_hash_ms = 0.0;
        for (int t = 0; t < TRIALS; ++t) {
            sum_sort_ms += run_sort_merge_grouping(data, c1);
            sum_hash_ms += run_hash_grouping(data, G, c2);
        }

        if (c1 != c2) {
            std::cerr << "ERROR: Checksum mismatch!\n";
            return 1;
        }

        double avg_sort_ms = sum_sort_ms / TRIALS;
        double avg_hash_ms = sum_hash_ms / TRIALS;

        size_t cap = 1;
        while (cap < G * 2) cap <<= 1;
        double ht_kb = (cap * 16.0) / 1024.0;

        std::cout << std::setw(12) << G
                  << std::setw(16) << std::fixed << std::setprecision(1) << ht_kb
                  << std::setw(18) << std::setprecision(2) << avg_sort_ms
                  << std::setw(18) << avg_hash_ms
                  << std::setw(11) << (avg_sort_ms / avg_hash_ms) << "x\n";

        csv << G << "," << ht_kb << "," << avg_sort_ms << "," << avg_hash_ms << "\n";
    }
    csv.close();
    std::cout << "\nSaved averaged results to sec3_2_grouping.csv!\n";
    return 0;
}