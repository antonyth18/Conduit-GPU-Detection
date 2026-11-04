#include "conduit/execute.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <queue>
#include <thread>

#ifdef CONDUIT_HAVE_OPENMP
#include <omp.h>
#endif

namespace conduit {

namespace {

inline uint64_t mix(uint64_t a, uint64_t b) {
    uint64_t x = a ^ (b + 0x9E3779B97F4A7C15ull + (a << 6) + (a >> 2));
    x ^= x >> 31;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 29;
    return x;
}

inline void cpu_relax() {
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#else
    std::this_thread::yield();
#endif
}

struct Run {
    const ExecPlan& p;
    std::vector<uint64_t> state;
    std::vector<uint64_t> out;
    explicit Run(const ExecPlan& plan) : p(plan), state(plan.num_keys, 0), out(plan.n, 0) {}

    // Executes one transaction. Conflicting transactions are ordered by the
    // caller; concurrent ones only share READs (loads) or COMMUTATIVE keys
    // (atomic adds).
    void tx(uint32_t t) {
        uint64_t acc = mix(0x51ED27C3A5D4E5B1ull, t);
        for (uint32_t i = 0, w = p.work[t]; i < w; ++i) acc = mix(acc, i);
        for (uint32_t a = p.off[t]; a < p.off[t + 1]; ++a)
            if (p.mode[a] != COMMUTATIVE)
                acc = mix(acc, __atomic_load_n(&state[p.key[a]], __ATOMIC_RELAXED));
        for (uint32_t a = p.off[t]; a < p.off[t + 1]; ++a) {
            const uint32_t k = p.key[a];
            if (p.mode[a] == WRITE)
                __atomic_store_n(&state[k], mix(acc, k), __ATOMIC_RELAXED);
            else if (p.mode[a] == COMMUTATIVE)
                __atomic_fetch_add(&state[k], mix(t, k) | 1, __ATOMIC_RELAXED);
        }
        out[t] = acc;
    }

    void finish(ExecResult& r) const {
        uint64_t h = 0x1234;
        for (uint64_t v : state) h = mix(h, v);
        r.state_hash = h;
        h = 0x5678;
        for (uint64_t v : out) h = mix(h, v);
        r.output_hash = h;
    }
};

using clk = std::chrono::steady_clock;
double since(clk::time_point a) { return std::chrono::duration<double>(clk::now() - a).count(); }

}  // namespace

ExecPlan make_plan(const AccessSets& sets, const Tx* tx, double gas_per_iter) {
    ExecPlan p;
    p.n = uint32_t(sets.size());
    std::vector<uint64_t> uk = sets.keys;
    std::sort(uk.begin(), uk.end());
    uk.erase(std::unique(uk.begin(), uk.end()), uk.end());
    p.num_keys = uint32_t(uk.size());
    p.off.resize(size_t(p.n) + 1);
    for (size_t i = 0; i <= p.n; ++i) p.off[i] = uint32_t(sets.offsets[i]);
    p.key.resize(sets.keys.size());
    for (size_t r = 0; r < sets.keys.size(); ++r)
        p.key[r] = uint32_t(std::lower_bound(uk.begin(), uk.end(), sets.keys[r]) - uk.begin());
    p.mode = sets.modes;
    p.work.resize(p.n);
    for (uint32_t i = 0; i < p.n; ++i) {
        const double g = std::max<double>(tx[i].gas, 21000.0);
        p.work[i] = gas_per_iter > 0 ? uint32_t(g / gas_per_iter) : 0;
        p.total_work += p.work[i];
    }
    return p;
}

ExecResult exec_serial(const ExecPlan& p) {
    Run run(p);
    ExecResult r;
    const auto t0 = clk::now();
    for (uint32_t t = 0; t < p.n; ++t) run.tx(t);
    r.seconds = since(t0);
    r.executed = p.n;
    run.finish(r);
    return r;
}

ExecResult exec_serial_order(const ExecPlan& p, const std::vector<uint32_t>& order) {
    Run run(p);
    ExecResult r;
    const auto t0 = clk::now();
    for (uint32_t t : order) run.tx(t);
    r.seconds = since(t0);
    r.executed = order.size();
    run.finish(r);
    return r;
}

std::vector<uint32_t> batch_order(const std::vector<uint32_t>& batch) {
    uint32_t B = 0;
    for (uint32_t b : batch) B = std::max(B, b + 1);
    std::vector<uint32_t> start(size_t(B) + 1, 0), order(batch.size());
    for (uint32_t b : batch) ++start[size_t(b) + 1];
    for (uint32_t b = 0; b < B; ++b) start[b + 1] += start[b];
    for (uint32_t t = 0; t < batch.size(); ++t) order[start[batch[t]]++] = t;
    return order;
}

ExecResult exec_levels(const ExecPlan& p, const std::vector<uint32_t>& batch, int threads) {
    uint32_t B = 0;
    for (uint32_t b : batch) B = std::max(B, b + 1);
    std::vector<uint32_t> start(size_t(B) + 1, 0), order(p.n);
    for (uint32_t b : batch) ++start[size_t(b) + 1];
    for (uint32_t b = 0; b < B; ++b) start[b + 1] += start[b];
    {
        std::vector<uint32_t> pos(start.begin(), start.end() - 1);
        for (uint32_t t = 0; t < p.n; ++t) order[pos[batch[t]]++] = t;
    }
    Run run(p);
    ExecResult r;
    uint64_t executed = 0;
    const auto t0 = clk::now();
#ifdef CONDUIT_HAVE_OPENMP
#pragma omp parallel num_threads(threads > 0 ? threads : omp_get_max_threads()) reduction(+ : executed)
    {
        for (uint32_t b = 0; b < B; ++b) {
            const int64_t lo = start[b], hi = start[b + 1];
            if (hi - lo == 1) {   // single transaction: one thread, then the barrier
#pragma omp single
                {
                    run.tx(order[lo]);
                    ++executed;
                }
            } else {
#pragma omp for schedule(dynamic, 8)
                for (int64_t i = lo; i < hi; ++i) {
                    run.tx(order[i]);
                    ++executed;
                }
            }
        }
    }
#else
    (void)threads;
    for (uint32_t b = 0; b < B; ++b)
        for (uint32_t i = start[b]; i < start[b + 1]; ++i) {
            run.tx(order[i]);
            ++executed;
        }
#endif
    r.seconds = since(t0);
    r.executed = executed;
    run.finish(r);
    return r;
}

ExecResult exec_dag(const ExecPlan& p, const Graph& g, int threads) {
    const uint32_t n = p.n;
    std::vector<std::atomic<uint32_t>> indeg(n);
    std::vector<std::atomic<uint32_t>> slot(n);   // ready list; 0 = not yet published
    for (uint32_t v = 0; v < n; ++v) {
        indeg[v].store(0, std::memory_order_relaxed);
        slot[v].store(0, std::memory_order_relaxed);
    }
    for (uint32_t c : g.cols) indeg[c].fetch_add(1, std::memory_order_relaxed);
    std::atomic<uint64_t> head{0}, tail{0};
    for (uint32_t v = 0; v < n; ++v)
        if (indeg[v].load(std::memory_order_relaxed) == 0)
            slot[tail.fetch_add(1, std::memory_order_relaxed)].store(v + 1,
                                                                    std::memory_order_relaxed);
    Run run(p);
    ExecResult r;
    std::atomic<uint64_t> executed{0};
    // Each worker claims the next ready-list slot and waits until it is
    // published. Slot i is published once i + 1 transactions are ready, and
    // the DAG always has a ready transaction while work remains, so a waiting
    // worker is always eventually served (no deadlock, no lost work).
    auto worker = [&]() {
        uint64_t mine = 0;
        for (;;) {
            const uint64_t i = head.fetch_add(1, std::memory_order_relaxed);
            if (i >= n) break;
            uint32_t v;
            unsigned spins = 0;
            while ((v = slot[i].load(std::memory_order_acquire)) == 0)
                if (++spins < 256) cpu_relax();
                else std::this_thread::yield();
            const uint32_t t = v - 1;
            run.tx(t);
            ++mine;
            for (uint64_t e = g.row_offsets[t]; e < g.row_offsets[size_t(t) + 1]; ++e) {
                const uint32_t s = g.cols[e];
                if (indeg[s].fetch_sub(1, std::memory_order_acq_rel) == 1)
                    slot[tail.fetch_add(1, std::memory_order_relaxed)].store(
                        s + 1, std::memory_order_release);
            }
        }
        executed.fetch_add(mine, std::memory_order_relaxed);
    };
    const auto t0 = clk::now();
#ifdef CONDUIT_HAVE_OPENMP
#pragma omp parallel num_threads(threads > 0 ? threads : omp_get_max_threads())
    worker();
#else
    (void)threads;
    worker();
#endif
    r.seconds = since(t0);
    r.executed = executed.load();
    run.finish(r);
    return r;
}

DagSim simulate_dag(const Graph& g, const std::vector<double>& cost, uint32_t workers) {
    const uint32_t n = g.n;
    DagSim s;
    std::vector<uint32_t> indeg(n, 0);
    for (uint32_t c : g.cols) ++indeg[c];
    std::priority_queue<uint32_t, std::vector<uint32_t>, std::greater<uint32_t>> ready;
    using Ev = std::pair<double, uint32_t>;
    std::priority_queue<Ev, std::vector<Ev>, std::greater<Ev>> running;
    for (uint32_t v = 0; v < n; ++v) {
        s.serial += cost[v];
        if (!indeg[v]) ready.push(v);
    }
    double now = 0;
    uint32_t done = 0;
    while (done < n) {
        while (!ready.empty() && (workers == 0 || running.size() < workers)) {
            const uint32_t t = ready.top();
            ready.pop();
            running.push({now + cost[t], t});
        }
        const Ev e = running.top();
        running.pop();
        now = e.first;
        ++done;
        for (uint64_t k = g.row_offsets[e.second]; k < g.row_offsets[size_t(e.second) + 1]; ++k)
            if (--indeg[g.cols[k]] == 0) ready.push(g.cols[k]);
    }
    s.makespan = now;
    s.speedup = now > 0 ? s.serial / now : 0;
    return s;
}

}  // namespace conduit
