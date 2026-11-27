#include <iostream>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <chrono>
#include <random>
#include <iomanip>
using namespace std;

struct Tuple {
    uint32_t oid;
    uint32_t val;
};

// ============================================================================
// 1. POINTER-CHASING BST (Simulates Binary Tree: 1 key per memory hop)
// ============================================================================
struct BSTNode {
    uint32_t key;
    uint32_t oid;
    int32_t left;
    int32_t right;
};

int32_t build_bst(const vector<Tuple>& sorted, vector<BSTNode>& nodes, int l, int r) {
    if (l > r) return -1;
    int mid = l + (r - l) / 2;
    int32_t idx = static_cast<int32_t>(nodes.size());
    nodes.push_back({sorted[mid].val, sorted[mid].oid, -1, -1});
    int32_t left_idx = build_bst(sorted, nodes, l, mid - 1);
    int32_t right_idx = build_bst(sorted, nodes, mid + 1, r);
    nodes[idx].left = left_idx;
    nodes[idx].right = right_idx;
    return idx;
}

uint32_t lookup_bst(const vector<BSTNode>& nodes, int32_t root, uint32_t target) {
    int32_t curr = root;
    while (curr != -1) {
        if (target == nodes[curr].key) return nodes[curr].oid;
        curr = (target < nodes[curr].key) ? nodes[curr].left : nodes[curr].right;
    }
    return 0;
}

// ============================================================================
// 2. CACHE-LINE B-TREE (15 keys + 1 child offset = 64 bytes = 1 x86 cache line)
// ============================================================================
struct alignas(64) CacheLineBTreeNode {
    uint32_t keys[15];
    uint32_t first_child_idx;
};

class CacheLineBTree {
    vector<CacheLineBTreeNode> internal_nodes_;
    vector<Tuple> leaves_;
    size_t num_internal_ = 0;

public:
    void build(const vector<Tuple>& sorted_data) {
        leaves_ = sorted_data;
        vector<vector<CacheLineBTreeNode>> levels;
        vector<uint32_t> level_max_keys;

        size_t num_leaf_blocks = (leaves_.size() + 15) / 16;
        for (size_t b = 0; b < num_leaf_blocks; ++b) {
            size_t end_idx = min((b + 1) * 16 - 1, leaves_.size() - 1);
            level_max_keys.push_back(leaves_[end_idx].val);
        }

        while (level_max_keys.size() > 1) {
            size_t num_nodes = (level_max_keys.size() + 15) / 16;
            vector<CacheLineBTreeNode> curr_level(num_nodes);
            vector<uint32_t> next_max_keys;

            for (size_t n = 0; n < num_nodes; ++n) {
                curr_level[n].first_child_idx = static_cast<uint32_t>(n * 16);
                for (size_t k = 0; k < 15; ++k) {
                    size_t child_i = n * 16 + k;
                    curr_level[n].keys[k] = (child_i < level_max_keys.size())
                                                ? level_max_keys[child_i]
                                                : UINT32_MAX;
                }
                size_t last_child = min((n + 1) * 16 - 1, level_max_keys.size() - 1);
                next_max_keys.push_back(level_max_keys[last_child]);
            }
            levels.push_back(move(curr_level));
            level_max_keys = move(next_max_keys);
        }

        reverse(levels.begin(), levels.end());
        size_t offset = 0;
        for (size_t l = 0; l < levels.size(); ++l) {
            size_t next_level_start = offset + levels[l].size();
            for (auto& node : levels[l]) {
                node.first_child_idx += static_cast<uint32_t>(next_level_start);
                internal_nodes_.push_back(node);
            }
            offset = next_level_start;
        }
        num_internal_ = internal_nodes_.size();
    }

    inline uint32_t lookup(uint32_t target) const {
        size_t idx = 0;
        while (idx < num_internal_) {
            const CacheLineBTreeNode& node = internal_nodes_[idx];
            size_t child = 0;
            while (child < 15 && target > node.keys[child]) {
                child++;
            }
            idx = node.first_child_idx + child;
        }
        size_t leaf_block = idx - num_internal_;
        size_t start = leaf_block * 16;
        size_t end = min(start + 16, leaves_.size());
        for (size_t i = start; i < end; ++i) {
            if (leaves_[i].val == target) return leaves_[i].oid;
        }
        return 0;
    }
};

int main() {
    const size_t N = 4'000'000;
    const size_t NUM_QUERIES = 1'000'000;
    const int TRIALS = 10;

    cout << "Building indexes over " << N << " sorted tuples...\n";
    vector<Tuple> data(N);
    for (size_t i = 0; i < N; ++i) {
        data[i] = {static_cast<uint32_t>(i), static_cast<uint32_t>(i * 3 + 7)};
    }

    vector<BSTNode> bst_nodes;
    bst_nodes.reserve(N);
    int32_t bst_root = build_bst(data, bst_nodes, 0, static_cast<int>(N - 1));

    CacheLineBTree btree;
    btree.build(data);

    mt19937 rng(123);
    vector<uint32_t> queries(NUM_QUERIES);
    for (size_t i = 0; i < NUM_QUERIES; ++i) {
        queries[i] = data[rng() % N].val;
    }

    // Warmup runs (1 pass each)
    uint64_t checksum1 = 0, checksum2 = 0;
    for (uint32_t q : queries) checksum1 += lookup_bst(bst_nodes, bst_root, q);
    for (uint32_t q : queries) checksum2 += btree.lookup(q);

    // Timed Trials: Pointer-Chasing BST
    double total_bst_ms = 0.0;
    for (int t = 0; t < TRIALS; ++t) {
        uint64_t run_sum = 0;
        auto t0 = chrono::high_resolution_clock::now();
        for (uint32_t q : queries) {
            run_sum += lookup_bst(bst_nodes, bst_root, q);
        }
        auto t1 = chrono::high_resolution_clock::now();
        total_bst_ms += chrono::duration<double, milli>(t1 - t0).count();
        checksum1 = run_sum;
    }

    // Timed Trials: 64-Byte Cache-Line B-Tree
    double total_btree_ms = 0.0;
    for (int t = 0; t < TRIALS; ++t) {
        uint64_t run_sum = 0;
        auto t0 = chrono::high_resolution_clock::now();
        for (uint32_t q : queries) {
            run_sum += btree.lookup(q);
        }
        auto t1 = chrono::high_resolution_clock::now();
        total_btree_ms += chrono::duration<double, milli>(t1 - t0).count();
        checksum2 = run_sum;
    }

    double avg_bst_ms = total_bst_ms / TRIALS;
    double avg_btree_ms = total_btree_ms / TRIALS;

    cout << "\n--- Average Results over " << TRIALS << " Trials (" << NUM_QUERIES << " Lookups) ---\n";
    cout << "Pointer-Chasing BST (1 key/node)      : " << fixed << setprecision(2)
         << avg_bst_ms << " ms (checksum=" << checksum1 << ")\n";
    cout << "Cache-Line B-Tree (15 keys/64B node)  : " << avg_btree_ms
         << " ms (checksum=" << checksum2 << ")\n";
    cout << "Speedup                               : " << (avg_bst_ms / avg_btree_ms) << "x faster!\n";

    return 0;
}