// Four-stage sequential pipeline: extraction -> conflict detection ->
// graph construction -> levelling. Single-threaded by design (Phase 5); the
// OpenMP and CUDA versions in Phases 7 and 8 keep the same stage boundaries.
//
// Conflict rule (oracle.py): two accesses to one key conflict unless both are
// READ or both are COMMUTATIVE.
//
// Reduced edge rule. Within one key, accesses in block order split into
// maximal RUNS: consecutive READs, consecutive COMMUTATIVEs, or a single
// WRITE. Every access in run k depends on every access in run k-1, and on
// nothing earlier.
//   - Every emitted edge is a true conflict (adjacent runs are different
//     classes, or two WRITEs), so the reduced graph H is a subgraph of the
//     full conflict graph G.
//   - Same-run pairs never conflict, so a conflicting pair a < b lies in runs
//     i < j, and a -> x(i+1) -> ... -> x(j-1) -> b is a path in H.
//   - Hence H and G have the same reachability and, since each G edge is an
//     H path of length >= 1, the same longest paths: identical levels.
// Work is bounded per record except where a multi-READ run meets a
// multi-COMMUTATIVE run (complete bipartite, irreducible without changing the
// vertex set). Phase 2 never puts READ and COMMUTATIVE on the same key, so
// on real data this never happens; EdgeStats counts it anyway.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "conduit/policy.hpp"
#include "conduit/record.hpp"

namespace conduit {

// Successor CSR: cols[row_offsets[u] .. row_offsets[u+1]) are the v with an
// edge u -> v, ascending. Edges always run from lower to higher index.
struct Graph {
    uint32_t n = 0;
    std::vector<uint64_t> row_offsets;
    std::vector<uint32_t> cols;
    uint64_t num_edges() const { return cols.size(); }
};

struct EdgeStats {
    uint64_t emitted = 0;           // before deduplication
    uint64_t read_comm_edges = 0;   // edges between adjacent READ and COMM runs
};

struct Timings {
    double extract = 0;
    double sort = 0;
    double detect = 0;
    double csr = 0;
    double level = 0;
    double total() const { return extract + sort + detect + csr + level; }
};

struct Result {
    size_t records = 0;
    EdgeStats stats;
    Graph graph;
    std::vector<uint32_t> levels;
};

// Stage 1. Policy inference plus packing, tx indices local to the slice.
std::vector<Record> extract(const Tx* tx, size_t n, Policy policy, const Selectors& sels);
std::vector<Record> extract(const AccessSets& sets);

// Stage 2a. Sort records as 128-bit integers.
void sort_records(std::vector<Record>& records);

// Stage 2b. Walk key groups of sorted records; returns packed edges u<<32|v.
std::vector<uint64_t> detect(const std::vector<Record>& sorted, EdgeStats* stats);

// Stage 3. Sorts and deduplicates edges in place, then builds the CSR.
Graph build_csr(uint32_t n, std::vector<uint64_t>& edges);

// Stage 4. Longest-path levels by one ascending sweep. Every predecessor of
// v has a lower index, so it is final before v's level is read: exact and
// optimal sequentially. The OpenMP and CUDA versions cannot sweep in order
// and will need frontier-based relaxation (Kahn-style, level by level); that
// algorithmic difference must be reported alongside their timings.
std::vector<uint32_t> level(const Graph& g);

Result run_pipeline(const Tx* tx, size_t n, Policy policy, const Selectors& sels,
                    Timings* timings = nullptr);

}  // namespace conduit
