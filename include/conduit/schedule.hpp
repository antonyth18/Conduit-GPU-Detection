// Schedule checks and an execution model, independent of every pipeline.
//
//   check_batches   every batch (transactions with equal batch id) must be
//                   conflict-free under the access sets: per key, no two
//                   WRITEs, no WRITE with any other access, no READ with a
//                   COMMUTATIVE. Uses only the access sets, not any graph.
//   check_order     every edge u -> v (u earlier in the block) has
//                   batch[u] < batch[v]: executing batches in order then
//                   commits conflicting transactions in block order, i.e. the
//                   schedule is serially equivalent to the block.
//   brute_levels    longest-path levels from the pairwise conflict
//                   definition, O(n^2); an oracle for small slices.
//   simulate        executes the batches on W identical workers: inside a
//                   batch, longest-processing-time-first list scheduling of
//                   per-tx cost (gas used); batches run one after another with
//                   a barrier between them. makespan and speedup are in gas
//                   units, i.e. relative, not seconds.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "conduit/pipeline.hpp"
#include "conduit/policy.hpp"

namespace conduit {

struct BatchCheck {
    bool ok = true;
    uint64_t violations = 0;   // (key, batch) groups with a conflict
    std::string first;         // description of the first violation
};

BatchCheck check_batches(const AccessSets& sets, const std::vector<uint32_t>& batch);
bool check_order(const Graph& g, const std::vector<uint32_t>& batch);
std::vector<uint32_t> brute_levels(const AccessSets& sets);

// Lower bound on the number of conflict-free batches of ANY schedule, even
// one that reorders: per key, every WRITE needs its own batch and the READs
// (and the COMMUTATIVEs) need at least one more: max_k W_k + [R_k>0] + [C_k>0].
// Order-preserving schedules are additionally bounded by the longest path,
// which levels achieve exactly.
uint32_t clique_bound(const AccessSets& sets);

struct ExecSim {
    uint32_t batches = 0;
    uint32_t workers = 0;      // 0 = unbounded
    double serial = 0;         // sum of costs
    double makespan = 0;       // sum over batches of the batch makespan + barriers
    double speedup = 0;        // serial / makespan
    double utilisation = 0;    // serial / (workers * makespan), 0 when unbounded
};

// cost[i] = execution cost of tx i (e.g. gas used, floored at 21000).
// barrier = fixed cost added per batch (synchronisation), same units.
ExecSim simulate(const std::vector<uint32_t>& batch, const std::vector<double>& cost,
                 uint32_t workers, double barrier = 0);

std::vector<double> gas_costs(const Tx* tx, size_t n);

}  // namespace conduit
