#include <iostream>
#include <fstream>
#include <vector>
#include <list>
#include <unordered_map>
#include <cstdint>
#include <chrono>
#include <random>
#include <cmath>
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

// ============================================================================
// EXACT LRU CACHE / TLB SIMULATOR
// Tracks virtual addresses at cache-line or page granularity
// ============================================================================
class LRUSimulator {
    size_t capacity_;
    size_t shift_bits_; // log2(line_size or page_size)
    std::list<uint64_t> lru_list_;
    std::unordered_map<uint64_t, std::list<uint64_t>::iterator> map_;
    uint64_t misses_ = 0;

public:
    LRUSimulator(size_t num_entries, size_t block_size_bytes)
        : capacity_(num_entries),
          shift_bits_(static_cast<size_t>(std::log2(block_size_bytes))) {
        map_.reserve(num_entries * 2);
    }

    inline void access(uint64_t byte_addr) {
        uint64_t tag = byte_addr >> shift_bits_;
        auto it = map_.find(tag);
        if (it != map_.end()) {
            // Hit: move to front of LRU
            lru_list_.splice(lru_list_.begin(), lru_list_, it->second);
        } else {
            // Miss: insert and evict oldest if full
            misses_++;
            if (lru_list_.size() >= capacity_) {
                uint64_t oldest = lru_list_.back();
                lru_list_.pop_back();
                map_.erase(oldest);
            }
            lru_list_.push_front(tag);
            map_[tag] = lru_list_.begin();
        }
    }

    uint64_t misses() const { return misses_; }
};

// ============================================================================
// REAL EXECUTION (Measures actual wall-clock milliseconds on Windows)
// ============================================================================
void partition_chunk_fast(const Tuple* __restrict in, Tuple* __restrict out,
                          size_t n, int shift, int bits, std::vector<size_t>& out_counts) {
    const size_t H_p = 1ULL << bits;
    const uint32_t mask = static_cast<uint32_t>(H_p - 1);
    out_counts.assign(H_p, 0);

    for (size_t i = 0; i < n; ++i)
        out_counts[(hash_val(in[i].val) >> shift) & mask]++;

    std::vector<size_t> write_pos(H_p);
    size_t offset = 0;
    for (size_t b = 0; b < H_p; ++b) {
        write_pos[b] = offset;
        offset += out_counts[b];
    }

    for (size_t i = 0; i < n; ++i)
        out[write_pos[(hash_val(in[i].val) >> shift) & mask]++] = in[i];
}

double measure_real_ms(const std::vector<Tuple>& input, std::vector<Tuple>& output,
                       int total_bits, int passes) {
    const size_t N = input.size();
    std::vector<Tuple> temp_buf(passes > 1 ? N : 0);

    std::vector<int> pass_bits(passes);
    for (int p = 0; p < passes; ++p)
        pass_bits[p] = (total_bits / passes) + (p < (total_bits % passes) ? 1 : 0);

    std::vector<size_t> current_chunks = {N};
    const Tuple* src = input.data();
    Tuple* dst = (passes % 2 == 1) ? output.data() : temp_buf.data();

    auto t0 = std::chrono::high_resolution_clock::now();

    int bit_shift = 0;
    for (int p = 0; p < passes; ++p) {
        int bits = pass_bits[p];
        std::vector<size_t> next_chunks;
        next_chunks.reserve(current_chunks.size() * (1ULL << bits));
        size_t chunk_offset = 0;
        std::vector<size_t> sub_counts;

        for (size_t chunk_size : current_chunks) {
            if (chunk_size > 0) {
                partition_chunk_fast(src + chunk_offset, dst + chunk_offset, chunk_size, bit_shift, bits, sub_counts);
                next_chunks.insert(next_chunks.end(), sub_counts.begin(), sub_counts.end());
            } else {
                next_chunks.insert(next_chunks.end(), 1ULL << bits, 0);
            }
            chunk_offset += chunk_size;
        }
        bit_shift += bits;
        current_chunks = std::move(next_chunks);
        if (p + 1 < passes) {
            src = dst;
            dst = (dst == output.data()) ? temp_buf.data() : output.data();
        }
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

// ============================================================================
// SIMULATED EXECUTION (Counts L1, L2, and TLB misses using Paper's Hardware)
// Paper specs (Section 3.4.1):
//   L1 = 32 KB (1024 lines x 32 B), L2 = 4 MB (32768 lines x 128 B),
//   TLB = 64 entries x 16 KB pages
// ============================================================================
struct SimMisses {
    uint64_t l1;
    uint64_t l2;
    uint64_t tlb;
};

SimMisses simulate_hardware_misses(const std::vector<Tuple>& full_input, int total_bits, int passes) {
    // Trace on a 1M tuple subset and scale by 8x so simulation finishes in seconds
    // while keeping every cluster larger than cache lines!
    const size_t SCALE = 8;
    const size_t N = full_input.size() / SCALE;

    LRUSimulator sim_l1(1024, 32);     // 1024 lines of 32 bytes
    LRUSimulator sim_l2(32768, 128);   // 32768 lines of 128 bytes
    LRUSimulator sim_tlb(64, 16384);   // 64 entries of 16 KB pages

    // Virtual base addresses for input and output buffers (separated in address space)
    const uint64_t BASE_A = 0x10000000ULL;
    const uint64_t BASE_B = 0x20000000ULL;

    std::vector<Tuple> buf_a(full_input.begin(), full_input.begin() + N);
    std::vector<Tuple> buf_b(N);

    std::vector<int> pass_bits(passes);
    for (int p = 0; p < passes; ++p)
        pass_bits[p] = (total_bits / passes) + (p < (total_bits % passes) ? 1 : 0);

    std::vector<size_t> current_chunks = {N};
    Tuple* src = buf_a.data();
    Tuple* dst = buf_b.data();
    uint64_t src_addr = BASE_A;
    uint64_t dst_addr = BASE_B;

    int bit_shift = 0;
    for (int p = 0; p < passes; ++p) {
        int bits = pass_bits[p];
        const size_t H_p = 1ULL << bits;
        const uint32_t mask = static_cast<uint32_t>(H_p - 1);

        std::vector<size_t> next_chunks;
        size_t chunk_offset = 0;

        for (size_t chunk_size : current_chunks) {
            std::vector<size_t> counts(H_p, 0);

            // 1. Histogram pass (sequential read of src)
            for (size_t i = 0; i < chunk_size; ++i) {
                uint64_t r_addr = src_addr + (chunk_offset + i) * sizeof(Tuple);
                sim_tlb.access(r_addr);
                sim_l1.access(r_addr);
                sim_l2.access(r_addr);

                uint32_t b = (hash_val(src[chunk_offset + i].val) >> bit_shift) & mask;
                counts[b]++;
            }

            std::vector<size_t> write_pos(H_p);
            size_t off = 0;
            for (size_t b = 0; b < H_p; ++b) {
                write_pos[b] = off;
                off += counts[b];
            }

            // 2. Scatter pass (sequential read of src + random write to H_p buckets in dst)
            for (size_t i = 0; i < chunk_size; ++i) {
                uint64_t r_addr = src_addr + (chunk_offset + i) * sizeof(Tuple);
                sim_tlb.access(r_addr);
                sim_l1.access(r_addr);
                sim_l2.access(r_addr);

                uint32_t b = (hash_val(src[chunk_offset + i].val) >> bit_shift) & mask;
                size_t w_idx = write_pos[b]++;
                dst[chunk_offset + w_idx] = src[chunk_offset + i];

                // Scale simulated write stride by SCALE so clusters span the exact 8M page layout
                uint64_t w_addr = dst_addr + (chunk_offset + w_idx) * sizeof(Tuple) * SCALE;
                sim_tlb.access(w_addr);
                sim_l1.access(w_addr);
                sim_l2.access(w_addr);
            }

            next_chunks.insert(next_chunks.end(), counts.begin(), counts.end());
            chunk_offset += chunk_size;
        }

        bit_shift += bits;
        current_chunks = std::move(next_chunks);
        std::swap(src, dst);
        std::swap(src_addr, dst_addr);
    }

    return {sim_l1.misses() * SCALE, sim_l2.misses() * SCALE, sim_tlb.misses() * SCALE};
}

int main() {
    const size_t N = 8'000'000;
    std::cout << "Generating 8,000,000 tuples...\n";

    std::vector<Tuple> input(N), output(N);
    std::mt19937 rng(42);
    for (size_t i = 0; i < N; ++i) input[i] = {static_cast<uint32_t>(i), rng()};

    measure_real_ms(input, output, 2, 1); // Warm-up

    std::ofstream csv("fig9_win.csv");
    csv << "bits,passes,ms,l1_misses,l2_misses,tlb_misses\n";

    const int TRIALS = 5;
    for (int B = 1; B <= 18; ++B) {
        std::cout << "Running B = " << B << " / 18...\n";
        for (int P = 1; P <= 4; ++P) {
            if (B < P) continue;
            measure_real_ms(input, output, B, P); // 1 warmup
            double sum_ms = 0.0;
            for (int t = 0; t < TRIALS; ++t) {
                sum_ms += measure_real_ms(input, output, B, P);
            }
            double ms = sum_ms / TRIALS;
            SimMisses sim = simulate_hardware_misses(input, B, P);
            csv << B << "," << P << "," << ms << ","
                << sim.l1 << "," << sim.l2 << "," << sim.tlb << "\n";
        }
    }

    csv.close();
    std::cout << "Done! Saved to fig9_win.csv\n";
    return 0;
}