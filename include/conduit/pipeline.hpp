// Four-stage pipeline: extraction -> conflict detection -> graph construction
// -> levelling. Sequential (Phase 5) and OpenMP (Phase 7) implementations
// share the stage boundaries and produce identical output; Phase 8 (CUDA)
// keeps them too.
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
#include <string>
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
    uint64_t groups = 0;            // distinct keys
    // OpenMP detection only: per-thread busy time in the group loops,
    // max / mean (1.0 = perfectly balanced). 0 for sequential runs.
    double imbalance = 0;
};

// Levelling instrumentation. relaxations = edge visits that compare levels;
// atomics = atomic read-modify-writes; rounds = synchronised rounds (barriers).
struct LevelStats {
    uint64_t relaxations = 0;
    uint64_t atomics = 0;
    uint64_t rounds = 0;
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
    LevelStats level_stats;
    Graph graph;
    std::vector<uint32_t> levels;
};

// Implementations, selectable at runtime.
//   seq-std    Phase 5 baseline: std::sort for records and edges, sweep levelling
//   seq-radix  second baseline: sequential LSD radix for both sorts, sweep
//   omp        OpenMP: every stage parallel, parallel radix, Kahn levelling
// seq-radix exists so a parallel speedup is not credited for the change from
// comparison sort to radix sort.
enum class Impl { SeqStd, SeqRadix, Omp };
enum class LevelAlgo { Sweep, Kahn, BellmanFord };
enum class Schedule { Static, Dynamic, Guided };

struct Options {
    Impl impl = Impl::SeqStd;
    LevelAlgo level = LevelAlgo::Sweep;   // use default_level(impl) unless comparing
    int threads = 0;                      // omp only; <= 0 = OpenMP default
    Schedule schedule = Schedule::Dynamic;   // omp detection loop
    int chunk = 64;
};

Impl parse_impl(const std::string& s);
LevelAlgo parse_level(const std::string& s);
Schedule parse_schedule(const std::string& s);
const char* impl_name(Impl i);
const char* level_name(LevelAlgo l);
const char* schedule_name(Schedule s);
LevelAlgo default_level(Impl i);
bool have_openmp();

// ---- sequential stages (Phase 5) ------------------------------------------------

// Stage 1. Policy inference plus packing, tx indices local to the slice.
std::vector<Record> extract(const Tx* tx, size_t n, Policy policy, const Selectors& sels);
std::vector<Record> extract(const AccessSets& sets);

// Stage 2a. Sort records as 128-bit integers.
void sort_records(std::vector<Record>& records);

// Stage 2b. Walk key groups of sorted records; returns packed edges u<<32|v.
std::vector<uint64_t> detect(const std::vector<Record>& sorted, EdgeStats* stats);

// Stage 3. Sorts and deduplicates edges in place, then builds the CSR.
Graph build_csr(uint32_t n, std::vector<uint64_t>& edges);
// Same, with sequential radix sort instead of std::sort.
Graph build_csr_radix(uint32_t n, std::vector<uint64_t>& edges);

// Stage 4. Longest-path levels by one ascending sweep. Every predecessor of
// v has a lower index, so it is final before v's level is read: exact and
// optimal sequentially, m relaxations, no synchronisation. The parallel
// versions cannot sweep in order (below); that difference is reported.
std::vector<uint32_t> level(const Graph& g, LevelStats* stats = nullptr);

// ---- OpenMP stages (Phase 7); throw if built without OpenMP ------------------------

// Per-tx access sets in parallel into fixed 7-slot buffers, an exclusive
// scan of the counts, then each tx writes its records at its offset.
std::vector<Record> extract_omp(const Tx* tx, size_t n, Policy policy, const Selectors& sels,
                                int threads);

// Group starts by flag + scan; per-group edge counts (scheduled loop); scan;
// emit at fixed offsets. Output equals detect() element for element.
std::vector<uint64_t> detect_omp(const std::vector<Record>& sorted, int threads,
                                 Schedule schedule, int chunk, EdgeStats* stats);

// Parallel radix sort of edges, dedup by flag + scan, row offsets by binary search.
Graph build_csr_omp(uint32_t n, std::vector<uint64_t>& edges, int threads);

// Kahn: in-degrees by atomic add, then frontiers. A vertex's level is the
// round in which its in-degree reaches zero (= its longest path). Each edge
// is relaxed once (m), at the cost of 2m atomics and one round per level.
std::vector<uint32_t> level_kahn(const Graph& g, int threads, LevelStats* stats);

// Level-synchronous Bellman-Ford (Jacobi, pull over predecessors): every
// round recomputes every level from the previous round's levels. No atomic
// counters or frontier queues, about rounds x m relaxations. A comparison
// variant: the shape of a naive GPU kernel. Includes building the
// predecessor CSR.
std::vector<uint32_t> level_bellman_ford(const Graph& g, int threads, LevelStats* stats);

// ---- whole pipeline ---------------------------------------------------------------

Result run_pipeline(const Tx* tx, size_t n, Policy policy, const Selectors& sels,
                    const Options& opts, Timings* timings = nullptr);
// Phase 5 signature: seq-std with sweep levelling.
Result run_pipeline(const Tx* tx, size_t n, Policy policy, const Selectors& sels,
                    Timings* timings = nullptr);

}  // namespace conduit
