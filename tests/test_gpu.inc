// Phase 8-10 tests: GPU kernel bodies (through the host emulation, and the
// real device when one is visible), Jones-Plassmann batches, schedule checks.
// Included by test_main.cpp so it shares the TEST / CHECK registry.
#include "conduit/gpu.hpp"
#include "conduit/schedule.hpp"
#include "../src/gpu/kernels.hpp"

namespace {

uint64_t g_rng = 0x1234567;
uint32_t rnd(uint32_t m) {
    g_rng = g_rng * 6364136223846793005ull + 1442695040888963407ull;
    return uint32_t((g_rng >> 33) % m);
}

// Random transactions over small id spaces so that keys collide a lot: a hot
// sender (nonce chain), a hot token, fallback calls, creations, absent fields.
std::vector<Tx> random_txs(size_t n, uint32_t accounts, uint32_t hot_pct) {
    std::vector<Tx> v;
    v.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const uint32_t kind = rnd(100);
        uint32_t frm = rnd(100) < hot_pct ? 1 : 2 + rnd(accounts);
        uint32_t to = 2 + rnd(accounts);
        uint16_t sel;
        uint8_t flags = FLAG_DECODED;
        uint32_t a1 = ABSENT, a2 = ABSENT;
        if (kind < 25) sel = 0;                                   // plain transfer
        else if (kind < 55) { sel = 1; to = USDT + rnd(3); a1 = 2 + rnd(accounts); }
        else if (kind < 62) { sel = 2; to = USDT; a1 = ROUTER; }
        else if (kind < 70) { sel = 3; to = USDT; a1 = 2 + rnd(accounts); a2 = 2 + rnd(accounts); }
        else if (kind < 72) { sel = 4; flags = FLAG_DECODED | FLAG_CREATE; to = ABSENT; }
        else if (kind < 74) { sel = 1; flags = 0; to = USDT; a1 = 3; }   // undecoded transfer
        else { sel = 4; to = ROUTER + rnd(4); }                         // generic call: fallback
        if (rnd(200) == 0) frm = ABSENT;
        v.push_back(make_tx(sel, frm, to, a1, a2, flags));
    }
    return v;
}

bool same_result(const Result& a, const Result& b) {
    return a.levels == b.levels && a.graph.n == b.graph.n && a.graph.cols == b.graph.cols &&
           a.graph.row_offsets == b.graph.row_offsets && a.stats.emitted == b.stats.emitted &&
           a.stats.read_comm_edges == b.stats.read_comm_edges && a.stats.groups == b.stats.groups &&
           a.records == b.records;
}

std::vector<GpuBackend> gpu_backends() {
    std::vector<GpuBackend> b = {GpuBackend::Emu};
    if (have_cuda()) b.push_back(GpuBackend::Cuda);
    return b;
}

}  // namespace

TEST(gpu_tx_access_matches_policy_port) {
    const gk::DevSelectors ds{SELS.plain, SELS.transfer, SELS.approve, SELS.transfer_from};
    std::vector<Tx> txs = random_txs(4000, 50, 10);
    for (Policy p : {Policy::P1, Policy::P2, Policy::P3})
        for (const Tx& t : txs) {
            uint64_t k1[MAX_TX_ACCESSES], k2[MAX_TX_ACCESSES];
            uint8_t m1[MAX_TX_ACCESSES], m2[MAX_TX_ACCESSES];
            uint8_t cat;
            const int a = tx_access(t, p, SELS, k1, m1, &cat);
            const int b = gk::k_tx_access(t, int(p), ds, k2, m2);
            CHECK(a == b);
            for (int j = 0; j < a && j < b; ++j) {
                CHECK(k1[j] == k2[j]);
                CHECK(m1[j] == m2[j]);
            }
        }
}

TEST(gpu_pipeline_identical_to_seq_std) {
    const size_t sizes[] = {0, 1, 2, 7, 300, 2500};
    const uint32_t hot[] = {0, 30};
    for (GpuBackend be : gpu_backends())
        for (size_t n : sizes)
            for (uint32_t h : hot) {
                std::vector<Tx> txs = random_txs(n, 40, h);
                for (Policy p : {Policy::P1, Policy::P2, Policy::P3}) {
                    const Result ref = run_pipeline(txs.data(), n, p, SELS);
                    for (DetectMode d : {DetectMode::Group, DetectMode::Balanced})
                        for (GpuLevel l : {GpuLevel::Sweep, GpuLevel::Kahn, GpuLevel::JpIndex})
                            for (uint32_t T : {0u, 16u, 1u << 30}) {
                                GpuOptions o;
                                o.backend = be;
                                o.detect = d;
                                o.level = l;
                                o.tail_threshold = T;
                                const Result r = run_pipeline_gpu(txs.data(), n, p, SELS, o);
                                CHECK(same_result(ref, r));
                            }
                }
            }
}

TEST(gpu_impls_through_run_pipeline) {
    std::vector<Tx> txs = random_txs(1500, 60, 20);
    for (Policy p : {Policy::P1, Policy::P3}) {
        const Result ref = run_pipeline(txs.data(), txs.size(), p, SELS);
        Options o;
        o.impl = Impl::GpuEmu;
        o.level = LevelAlgo::Kahn;
        CHECK(same_result(ref, run_pipeline(txs.data(), txs.size(), p, SELS, o)));
        if (have_cuda()) {
            o.impl = Impl::Cuda;
            CHECK(same_result(ref, run_pipeline(txs.data(), txs.size(), p, SELS, o)));
        }
    }
}

TEST(jp_random_batches_are_conflict_free_and_deterministic) {
    for (uint32_t h : {0u, 25u}) {
        std::vector<Tx> txs = random_txs(3000, 80, h);
        for (Policy p : {Policy::P1, Policy::P2, Policy::P3}) {
            const AccessSets sets = infer(txs.data(), txs.size(), p, SELS);
            const Result ref = run_pipeline(txs.data(), txs.size(), p, SELS);
            uint32_t L = 0;
            for (uint32_t v : ref.levels) L = std::max(L, v + 1);
            std::vector<Result> outs;
            for (GpuBackend be : gpu_backends())
                for (uint32_t T : {0u, 1u << 30}) {
                    GpuOptions o;
                    o.backend = be;
                    o.level = GpuLevel::JpRandom;
                    o.tail_threshold = T;
                    outs.push_back(run_pipeline_gpu(txs.data(), txs.size(), p, SELS, o));
                }
            for (const Result& r : outs) {
                CHECK(r.levels == outs[0].levels);   // backend- and threshold-independent
                const BatchCheck bc = check_batches(sets, r.levels);
                CHECK(bc.ok);
                if (!bc.ok) std::cerr << "    " << bc.first << "\n";
                for (uint32_t c : r.levels) CHECK(c != gk::JP_UNCOLORED);
                CHECK(r.level_stats.rounds > 0);
            }
            // jp-index reproduces the levels and they pass both schedule checks
            CHECK(check_batches(sets, ref.levels).ok);
            CHECK(check_order(ref.graph, ref.levels));
        }
    }
}

TEST(brute_levels_match_pipeline) {
    for (uint32_t h : {0u, 40u}) {
        std::vector<Tx> txs = random_txs(600, 30, h);
        for (Policy p : {Policy::P1, Policy::P2, Policy::P3}) {
            const AccessSets sets = infer(txs.data(), txs.size(), p, SELS);
            const Result ref = run_pipeline(txs.data(), txs.size(), p, SELS);
            CHECK(brute_levels(sets) == ref.levels);
        }
    }
}

TEST(check_batches_rejects_conflicts) {
    // two writers of one key in one batch
    AccessSets s = make_sets({{{5, W}}, {{5, W}}, {{6, R}}, {{6, R}}, {{7, R}}, {{7, C}}});
    CHECK(!check_batches(s, {0, 0, 1, 2, 3, 4}).ok);
    CHECK(check_batches(s, {0, 1, 2, 2, 3, 4}).ok);    // readers may share
    CHECK(!check_batches(s, {0, 1, 2, 2, 3, 3}).ok);   // READ with COMMUTATIVE may not
}

TEST(simulate_execution_model) {
    std::vector<double> cost = {10, 10, 10, 10};
    ExecSim serial = simulate({0, 1, 2, 3}, cost, 8);
    CHECK(serial.makespan == 40);
    CHECK(serial.speedup == 1);
    ExecSim wide = simulate({0, 0, 0, 0}, cost, 2);
    CHECK(wide.makespan == 20);
    CHECK(wide.speedup == 2);
    ExecSim inf = simulate({0, 0, 0, 0}, cost, 0);
    CHECK(inf.makespan == 10);
    ExecSim bar = simulate({0, 0, 1, 1}, cost, 0, 5);
    CHECK(bar.makespan == 30);
}

// ---- parallel execution ---------------------------------------------------------

#include "conduit/execute.hpp"

TEST(parallel_execution_is_serially_equivalent) {
    for (uint32_t h : {0u, 30u}) {
        std::vector<Tx> txs = random_txs(4000, 60, h);
        for (size_t i = 0; i < txs.size(); ++i) txs[i].gas = 21000 + uint32_t(rnd(200000));
        for (Policy p : {Policy::P1, Policy::P2, Policy::P3}) {
            const AccessSets sets = infer(txs.data(), txs.size(), p, SELS);
            const ExecPlan plan = make_plan(sets, txs.data(), 5000);
            const Result r = run_pipeline(txs.data(), txs.size(), p, SELS);
            const ExecResult ser = exec_serial(plan);
            CHECK(ser.executed == txs.size());
            for (int t : {1, 2, 4, 8}) {
                const ExecResult lv = exec_levels(plan, r.levels, t);
                const ExecResult dg = exec_dag(plan, r.graph, t);
                CHECK(lv.same_as(ser));
                CHECK(dg.same_as(ser));
            }
            // reordering batches: equal to the serial run in (batch, position) order
            GpuOptions o;
            o.backend = GpuBackend::Emu;
            o.level = GpuLevel::JpRandom;
            const Result jr = run_pipeline_gpu(txs.data(), txs.size(), p, SELS, o);
            const ExecResult ref = exec_serial_order(plan, batch_order(jr.levels));
            CHECK(exec_levels(plan, jr.levels, 4).same_as(ref));
            // and the state check has teeth: reversing the block order is detected
            CHECK(!exec_serial_order(plan, batch_order(r.levels)).same_as(ExecResult{}));
            const ExecResult wrong = exec_serial_order(plan, [&] {
                std::vector<uint32_t> o2(txs.size());
                for (uint32_t i = 0; i < o2.size(); ++i) o2[i] = uint32_t(o2.size() - 1 - i);
                return o2;
            }());
            CHECK(!wrong.same_as(ser));
        }
    }
}

TEST(dag_simulation_bounds) {
    // chain 0 -> 1 -> 2 and an independent 3
    std::vector<uint64_t> e = {edge(0, 1), edge(1, 2)};
    Graph g = graph_of(4, e);
    std::vector<double> c = {1, 2, 3, 10};
    CHECK(simulate_dag(g, c, 0).makespan == 10);   // critical path max(6, 10)
    CHECK(simulate_dag(g, c, 1).makespan == 16);   // serial
    CHECK(simulate_dag(g, c, 2).makespan == 10);
}
