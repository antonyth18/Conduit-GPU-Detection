// OpenMP implementation of the four stages (Phase 7). Every stage writes to
// offsets computed by an exclusive scan, or is a stable sort, so the output
// is identical to the sequential pipeline and independent of thread count.
// Loop indices are signed for portability (OpenMP 2.0 / MSVC).
#include <algorithm>
#include <atomic>
#include <memory>
#include <stdexcept>

#include "conduit/pipeline.hpp"
#include "conduit/radix.hpp"
#include "detail.hpp"

#ifdef CONDUIT_HAVE_OPENMP
#include <omp.h>
#endif

namespace conduit {

#ifdef CONDUIT_HAVE_OPENMP

bool have_openmp() { return true; }

namespace {

int team(int threads) { return threads > 0 ? threads : omp_get_max_threads(); }

// out[i] = in[0] + ... + in[i-1] for i in [0, n]; out has n + 1 slots.
template <class T>
void exclusive_scan(const T* in, uint64_t* out, int64_t n, int nt) {
    std::vector<uint64_t> part(size_t(nt) + 1, 0);
#pragma omp parallel num_threads(nt)
    {
        const int t = omp_get_thread_num();
        const int64_t lo = n * t / nt, hi = n * (t + 1) / nt;
        uint64_t s = 0;
        for (int64_t i = lo; i < hi; ++i) s += in[i];
        part[size_t(t) + 1] = s;
#pragma omp barrier
#pragma omp single
        for (int u = 0; u < nt; ++u) part[size_t(u) + 1] += part[u];
        s = part[t];
        for (int64_t i = lo; i < hi; ++i) {
            out[i] = s;
            s += in[i];
        }
    }
    out[n] = part[nt];
}

void set_schedule(Schedule s, int chunk) {
    switch (s) {
        case Schedule::Static: omp_set_schedule(omp_sched_static, chunk); break;
        case Schedule::Dynamic: omp_set_schedule(omp_sched_dynamic, chunk); break;
        case Schedule::Guided: omp_set_schedule(omp_sched_guided, chunk); break;
    }
}

}  // namespace

std::vector<Record> extract_omp(const Tx* tx, size_t n_, Policy policy, const Selectors& sels,
                                int threads) {
    const int nt = team(threads);
    const int64_t n = int64_t(n_);
    if (n_ > 0xFFFFFFFFull) throw std::runtime_error("slice too large for 32-bit tx index");

    int bad = 0;
#pragma omp parallel for num_threads(nt) reduction(| : bad) schedule(static)
    for (int64_t i = 0; i < n; ++i) {
        const uint32_t ids[4] = {tx[i].frm, tx[i].to, tx[i].a1, tx[i].a2};
        for (uint32_t id : ids) bad |= (id != ABSENT && id > MAX_ID);
    }
    if (bad) check_ids(tx, n_);   // throws with the offending id

    constexpr int K = MAX_TX_ACCESSES;
    std::vector<uint64_t> keys(size_t(n) * K);
    std::vector<uint8_t> modes(size_t(n) * K);
    std::vector<uint32_t> cnt(static_cast<size_t>(n));
#pragma omp parallel for num_threads(nt) schedule(static)
    for (int64_t i = 0; i < n; ++i)
        cnt[i] = uint32_t(tx_access(tx[i], policy, sels, &keys[size_t(i) * K],
                                    &modes[size_t(i) * K], nullptr));

    std::vector<uint64_t> off(size_t(n) + 1);
    exclusive_scan(cnt.data(), off.data(), n, nt);
    std::vector<Record> out(off[n]);
#pragma omp parallel for num_threads(nt) schedule(static)
    for (int64_t i = 0; i < n; ++i)
        for (uint32_t j = 0; j < cnt[i]; ++j)
            out[off[i] + j] = pack(keys[size_t(i) * K + j], uint32_t(i), modes[size_t(i) * K + j]);
    return out;
}

std::vector<uint64_t> detect_omp(const std::vector<Record>& r, int threads, Schedule schedule,
                                 int chunk, EdgeStats* stats) {
    const int nt = team(threads);
    const int64_t N = int64_t(r.size());

    // group starts: flag where the key changes, compact by per-thread scan
    std::vector<uint64_t> tcount(size_t(nt), 0), toff(size_t(nt) + 1);
#pragma omp parallel num_threads(nt)
    {
        const int t = omp_get_thread_num();
        const int64_t lo = N * t / nt, hi = N * (t + 1) / nt;
        uint64_t c = 0;
        for (int64_t i = lo; i < hi; ++i) c += (i == 0 || r[i].hi != r[i - 1].hi);
        tcount[t] = c;
    }
    toff[0] = 0;
    for (int t = 0; t < nt; ++t) toff[size_t(t) + 1] = toff[t] + tcount[t];
    const int64_t G = int64_t(toff[nt]);
    std::vector<uint64_t> gstart(size_t(G) + 1);
#pragma omp parallel num_threads(nt)
    {
        const int t = omp_get_thread_num();
        const int64_t lo = N * t / nt, hi = N * (t + 1) / nt;
        uint64_t w = toff[t];
        for (int64_t i = lo; i < hi; ++i)
            if (i == 0 || r[i].hi != r[i - 1].hi) gstart[w++] = uint64_t(i);
    }
    gstart[G] = uint64_t(N);

    // per-group edge counts, then offsets, then emit: identical order to detect()
    set_schedule(schedule, chunk);
    std::vector<uint64_t> ecount(static_cast<size_t>(G));
    std::vector<double> busy(size_t(nt), 0);
    uint64_t read_comm = 0;
#pragma omp parallel num_threads(nt) reduction(+ : read_comm)
    {
        const double t0 = omp_get_wtime();
#pragma omp for schedule(runtime) nowait
        for (int64_t g = 0; g < G; ++g)
            ecount[g] = detail::count_group(r.data(), gstart[g], gstart[g + 1], &read_comm);
        busy[omp_get_thread_num()] += omp_get_wtime() - t0;
    }
    std::vector<uint64_t> eoff(size_t(G) + 1);
    exclusive_scan(ecount.data(), eoff.data(), G, nt);
    std::vector<uint64_t> edges(eoff[G]);
#pragma omp parallel num_threads(nt)
    {
        const double t0 = omp_get_wtime();
#pragma omp for schedule(runtime) nowait
        for (int64_t g = 0; g < G; ++g) {
            uint64_t w = eoff[g];
            detail::walk_group(r.data(), gstart[g], gstart[g + 1], [&](uint32_t u, uint32_t v) {
                edges[w++] = (uint64_t(u) << 32) | v;
            });
        }
        busy[omp_get_thread_num()] += omp_get_wtime() - t0;
    }

    if (stats) {
        double mx = 0, sum = 0;
        for (double b : busy) {
            mx = std::max(mx, b);
            sum += b;
        }
        stats->emitted = edges.size();
        stats->read_comm_edges = read_comm;
        stats->groups = uint64_t(G);
        stats->imbalance = sum > 0 ? mx / (sum / nt) : 0;
    }
    return edges;
}

Graph build_csr_omp(uint32_t n, std::vector<uint64_t>& edges, int threads) {
    const int nt = team(threads);
    radix_sort_u64_omp(edges, nt);
    const int64_t M = int64_t(edges.size());

    std::vector<uint8_t> keep(static_cast<size_t>(M));
#pragma omp parallel for num_threads(nt) schedule(static)
    for (int64_t i = 0; i < M; ++i) keep[i] = (i == 0 || edges[i] != edges[i - 1]);
    std::vector<uint64_t> pos(size_t(M) + 1);
    exclusive_scan(keep.data(), pos.data(), M, nt);
    std::vector<uint64_t> uniq(pos[M]);
    int bad = 0;
#pragma omp parallel for num_threads(nt) schedule(static) reduction(| : bad)
    for (int64_t i = 0; i < M; ++i)
        if (keep[i]) {
            uniq[pos[i]] = edges[i];
            const uint32_t u = uint32_t(edges[i] >> 32), v = uint32_t(edges[i]);
            bad |= (u >= v || v >= n);
        }
    if (bad) throw std::runtime_error("edge is not forward within the slice");

    Graph g;
    g.n = n;
    g.row_offsets.resize(size_t(n) + 1);
    g.cols.resize(uniq.size());
    const int64_t U = int64_t(uniq.size());
#pragma omp parallel num_threads(nt)
    {
#pragma omp for schedule(static) nowait
        for (int64_t u = 0; u <= int64_t(n); ++u)
            g.row_offsets[u] = uint64_t(
                std::lower_bound(uniq.begin(), uniq.end(), uint64_t(u) << 32) - uniq.begin());
#pragma omp for schedule(static)
        for (int64_t e = 0; e < U; ++e) g.cols[e] = uint32_t(uniq[e]);
    }
    edges.swap(uniq);
    return g;
}

std::vector<uint32_t> level_kahn(const Graph& g, int threads, LevelStats* stats) {
    const int nt = team(threads);
    const int64_t n = g.n, m = int64_t(g.num_edges());
    std::unique_ptr<std::atomic<uint32_t>[]> indeg(new std::atomic<uint32_t>[size_t(n)]);
    const size_t sn = static_cast<size_t>(n);
    std::vector<uint32_t> lv(sn, 0), cur(sn), next(sn);
    std::atomic<int64_t> next_size{0};
    int64_t cur_size = 0;
    uint64_t rounds = 0;

#pragma omp parallel num_threads(nt)
    {
#pragma omp for schedule(static)
        for (int64_t v = 0; v < n; ++v) indeg[v].store(0, std::memory_order_relaxed);
#pragma omp for schedule(static)
        for (int64_t e = 0; e < m; ++e) indeg[g.cols[e]].fetch_add(1, std::memory_order_relaxed);
#pragma omp for schedule(static)
        for (int64_t v = 0; v < n; ++v)
            if (indeg[v].load(std::memory_order_relaxed) == 0)
                cur[next_size.fetch_add(1, std::memory_order_relaxed)] = uint32_t(v);
#pragma omp single
        {
            cur_size = next_size.load();
            next_size.store(0);
        }
        while (cur_size > 0) {
            const uint32_t lvl = uint32_t(rounds) + 1;   // level of vertices freed this round
#pragma omp for schedule(dynamic, 64)
            for (int64_t i = 0; i < cur_size; ++i) {
                const uint32_t u = cur[i];
                for (uint64_t e = g.row_offsets[u]; e < g.row_offsets[size_t(u) + 1]; ++e) {
                    const uint32_t v = g.cols[e];
                    if (indeg[v].fetch_sub(1, std::memory_order_acq_rel) == 1) {
                        lv[v] = lvl;
                        next[next_size.fetch_add(1, std::memory_order_relaxed)] = v;
                    }
                }
            }
#pragma omp single
            {
                cur.swap(next);
                cur_size = next_size.load();
                next_size.store(0);
                ++rounds;
            }
        }
    }
    if (stats) *stats = {uint64_t(m), 2 * uint64_t(m) + uint64_t(n), rounds};
    return lv;
}

std::vector<uint32_t> level_bellman_ford(const Graph& g, int threads, LevelStats* stats) {
    const int nt = team(threads);
    const int64_t n = g.n, m = int64_t(g.num_edges());
    // predecessor CSR (sequential counting transpose; included in the timing)
    std::vector<uint64_t> poff(size_t(n) + 1, 0);
    std::vector<uint32_t> preds(static_cast<size_t>(m));
    for (int64_t e = 0; e < m; ++e) ++poff[size_t(g.cols[e]) + 1];
    for (int64_t v = 0; v < n; ++v) poff[size_t(v) + 1] += poff[v];
    {
        std::vector<uint64_t> fill(poff.begin(), poff.end() - 1);
        for (int64_t u = 0; u < n; ++u)
            for (uint64_t e = g.row_offsets[u]; e < g.row_offsets[size_t(u) + 1]; ++e)
                preds[fill[g.cols[e]]++] = uint32_t(u);
    }

    std::vector<uint32_t> a(size_t(n), 0), b(size_t(n), 0);
    uint32_t* cur = a.data();
    uint32_t* nxt = b.data();
    int changed = 0, done = 0;
    uint64_t rounds = 0;
#pragma omp parallel num_threads(nt)
    while (!done) {
#pragma omp for schedule(static) reduction(| : changed)
        for (int64_t v = 0; v < n; ++v) {
            uint32_t best = 0;
            for (uint64_t e = poff[v]; e < poff[size_t(v) + 1]; ++e)
                best = std::max(best, cur[preds[e]] + 1);
            nxt[v] = best;
            changed |= (best != cur[v]);
        }
#pragma omp single
        {
            std::swap(cur, nxt);
            ++rounds;
            done = !changed;
            changed = 0;
        }
    }
    if (stats) *stats = {rounds * uint64_t(m), 0, rounds};
    return std::vector<uint32_t>(cur, cur + n);
}

#else  // !CONDUIT_HAVE_OPENMP

bool have_openmp() { return false; }

namespace {
[[noreturn]] void no_openmp() {
    throw std::runtime_error("built without OpenMP; the omp implementation is unavailable");
}
}  // namespace

std::vector<Record> extract_omp(const Tx*, size_t, Policy, const Selectors&, int) { no_openmp(); }
std::vector<uint64_t> detect_omp(const std::vector<Record>&, int, Schedule, int, EdgeStats*) {
    no_openmp();
}
Graph build_csr_omp(uint32_t, std::vector<uint64_t>&, int) { no_openmp(); }
std::vector<uint32_t> level_kahn(const Graph&, int, LevelStats*) { no_openmp(); }
std::vector<uint32_t> level_bellman_ford(const Graph&, int, LevelStats*) { no_openmp(); }

#endif

}  // namespace conduit
