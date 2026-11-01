// Parallel EXECUTION of a block with the computed schedule.
//
// The preprocessing pipeline (stages 1-4) says which transactions may run
// together; this module actually runs every transaction and checks that the
// parallel run ends in exactly the state a serial run produces.
//
// State model (no EVM): one 64-bit word per state key (the policy's keys,
// densely renumbered). Executing transaction t
//   1. does work proportional to its gas (gas / gas_per_iter mixing rounds,
//      standing in for EVM execution cost),
//   2. reads every READ and WRITE key into an accumulator,
//   3. stores a value derived from the accumulator into every WRITE key and
//      atomically adds a per-(t, key) constant to every COMMUTATIVE key,
//   4. records the accumulator as t's output.
// Any reordering of two conflicting transactions changes a stored value or
// an output, so equal (state hash, output hash) with a serial run is a direct
// test of serial equivalence on every transaction.
//
// Executors
//   serial     block order, one thread (the baseline: no preprocessing needed)
//   levels     batch by batch, every batch spread over T threads, a barrier
//              between batches (OpenMP); also runs reordering (JP) batches,
//              checked against the serial run in (batch, position) order
//   dag        dataflow on the reduced dependency DAG: a transaction starts as
//              soon as its last predecessor finishes (atomic in-degree
//              counters, a lock-free ready list, T threads). No barriers, so
//              a slow transaction only delays its own dependants.
//
// Models (gas units, no threads): simulate() in schedule.hpp for batches and
// simulate_dag() here for dataflow list scheduling on W workers; W = 0 gives
// the gas-weighted critical path, the best any executor can do.
#pragma once

#include <cstdint>
#include <vector>

#include "conduit/pipeline.hpp"
#include "conduit/policy.hpp"

namespace conduit {

struct ExecPlan {
    uint32_t n = 0;
    uint32_t num_keys = 0;
    std::vector<uint32_t> off;    // n + 1, per-tx access list
    std::vector<uint32_t> key;    // dense key ids
    std::vector<uint8_t> mode;
    std::vector<uint32_t> work;   // mixing rounds per tx
    uint64_t total_work = 0;
};

ExecPlan make_plan(const AccessSets& sets, const Tx* tx, double gas_per_iter);

struct ExecResult {
    double seconds = 0;
    uint64_t state_hash = 0;
    uint64_t output_hash = 0;
    uint64_t executed = 0;   // transactions actually run (must equal n)
    bool same_as(const ExecResult& o) const {
        return state_hash == o.state_hash && output_hash == o.output_hash &&
               executed == o.executed;
    }
};

ExecResult exec_serial(const ExecPlan& p);
// serial run in the order of the given permutation
ExecResult exec_serial_order(const ExecPlan& p, const std::vector<uint32_t>& order);
// permutation sorting by (batch, position)
std::vector<uint32_t> batch_order(const std::vector<uint32_t>& batch);
ExecResult exec_levels(const ExecPlan& p, const std::vector<uint32_t>& batch, int threads);
ExecResult exec_dag(const ExecPlan& p, const Graph& g, int threads);

struct DagSim {
    double serial = 0, makespan = 0, speedup = 0;
};
// list scheduling on the DAG, ready transactions in block order, W workers
// (0 = unbounded: the gas-weighted critical path)
DagSim simulate_dag(const Graph& g, const std::vector<double>& cost, uint32_t workers);

}  // namespace conduit
