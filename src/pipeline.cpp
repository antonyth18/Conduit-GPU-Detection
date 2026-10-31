#include "conduit/pipeline.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>

#include "conduit/radix.hpp"
#include "detail.hpp"

namespace conduit {

Impl parse_impl(const std::string& s) {
    if (s == "seq-std") return Impl::SeqStd;
    if (s == "seq-radix") return Impl::SeqRadix;
    if (s == "omp") return Impl::Omp;
    throw std::runtime_error("unknown impl " + s + " (seq-std | seq-radix | omp)");
}

LevelAlgo parse_level(const std::string& s) {
    if (s == "sweep") return LevelAlgo::Sweep;
    if (s == "kahn") return LevelAlgo::Kahn;
    if (s == "bf") return LevelAlgo::BellmanFord;
    throw std::runtime_error("unknown level algorithm " + s + " (sweep | kahn | bf)");
}

Schedule parse_schedule(const std::string& s) {
    if (s == "static") return Schedule::Static;
    if (s == "dynamic") return Schedule::Dynamic;
    if (s == "guided") return Schedule::Guided;
    throw std::runtime_error("unknown schedule " + s + " (static | dynamic | guided)");
}

const char* impl_name(Impl i) {
    switch (i) {
        case Impl::SeqStd: return "seq-std";
        case Impl::SeqRadix: return "seq-radix";
        case Impl::Omp: return "omp";
    }
    return "?";
}

const char* level_name(LevelAlgo l) {
    switch (l) {
        case LevelAlgo::Sweep: return "sweep";
        case LevelAlgo::Kahn: return "kahn";
        case LevelAlgo::BellmanFord: return "bf";
    }
    return "?";
}

const char* schedule_name(Schedule s) {
    switch (s) {
        case Schedule::Static: return "static";
        case Schedule::Dynamic: return "dynamic";
        case Schedule::Guided: return "guided";
    }
    return "?";
}

LevelAlgo default_level(Impl i) { return i == Impl::Omp ? LevelAlgo::Kahn : LevelAlgo::Sweep; }

std::vector<Record> extract(const AccessSets& sets) {
    const size_t n = sets.size();
    if (n > 0xFFFFFFFFull) throw std::runtime_error("slice too large for 32-bit tx index");
    std::vector<Record> out;
    out.reserve(sets.keys.size());
    for (size_t i = 0; i < n; ++i)
        for (int64_t r = sets.offsets[i]; r < sets.offsets[i + 1]; ++r)
            out.push_back(pack(sets.keys[r], uint32_t(i), sets.modes[r]));
    return out;
}

std::vector<Record> extract(const Tx* tx, size_t n, Policy policy, const Selectors& sels) {
    return extract(infer(tx, n, policy, sels));
}

void sort_records(std::vector<Record>& records) {
    std::sort(records.begin(), records.end());
}

std::vector<uint64_t> detect(const std::vector<Record>& r, EdgeStats* stats) {
    std::vector<uint64_t> edges;
    edges.reserve(r.size() * 2);
    uint64_t read_comm = 0, groups = 0;
    const size_t N = r.size();
    size_t i = 0;
    while (i < N) {
        const uint64_t key = rec_key(r[i]);
        size_t end = i;
        while (end < N && rec_key(r[end]) == key) ++end;
        read_comm += detail::walk_group(r.data(), i, end, [&](uint32_t u, uint32_t v) {
            edges.push_back((uint64_t(u) << 32) | v);
        });
        ++groups;
        i = end;
    }
    if (stats) {
        stats->emitted = edges.size();
        stats->read_comm_edges = read_comm;
        stats->groups = groups;
        stats->imbalance = 0;
    }
    return edges;
}

namespace {

// CSR from edges already sorted ascending; deduplicates in place.
Graph csr_from_sorted(uint32_t n, std::vector<uint64_t>& edges) {
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    Graph g;
    g.n = n;
    g.row_offsets.assign(size_t(n) + 1, 0);
    g.cols.resize(edges.size());
    for (size_t e = 0; e < edges.size(); ++e) {
        const uint32_t u = uint32_t(edges[e] >> 32);
        const uint32_t v = uint32_t(edges[e]);
        if (u >= v || v >= n) throw std::runtime_error("edge is not forward within the slice");
        ++g.row_offsets[size_t(u) + 1];
        g.cols[e] = v;   // sorted by (u, v), so cols are already in CSR order
    }
    for (uint32_t u = 0; u < n; ++u) g.row_offsets[size_t(u) + 1] += g.row_offsets[u];
    return g;
}

}  // namespace

Graph build_csr(uint32_t n, std::vector<uint64_t>& edges) {
    std::sort(edges.begin(), edges.end());
    return csr_from_sorted(n, edges);
}

Graph build_csr_radix(uint32_t n, std::vector<uint64_t>& edges) {
    radix_sort_u64_seq(edges);
    return csr_from_sorted(n, edges);
}

std::vector<uint32_t> level(const Graph& g, LevelStats* stats) {
    std::vector<uint32_t> lv(g.n, 0);
    for (uint32_t u = 0; u < g.n; ++u) {
        const uint32_t next = lv[u] + 1;
        for (uint64_t e = g.row_offsets[u]; e < g.row_offsets[size_t(u) + 1]; ++e) {
            uint32_t& t = lv[g.cols[e]];
            if (t < next) t = next;
        }
    }
    if (stats) *stats = {g.num_edges(), 0, 0};
    return lv;
}

Result run_pipeline(const Tx* tx, size_t n, Policy policy, const Selectors& sels,
                    const Options& opts, Timings* timings) {
    using clock = std::chrono::steady_clock;
    auto secs = [](clock::time_point a, clock::time_point b) {
        return std::chrono::duration<double>(b - a).count();
    };
    const bool omp = opts.impl == Impl::Omp;
    Result res;

    auto t0 = clock::now();
    std::vector<Record> records =
        omp ? extract_omp(tx, n, policy, sels, opts.threads) : extract(tx, n, policy, sels);
    auto t1 = clock::now();
    switch (opts.impl) {
        case Impl::SeqStd: sort_records(records); break;
        case Impl::SeqRadix: radix_sort_records_seq(records); break;
        case Impl::Omp: radix_sort_records_omp(records, opts.threads); break;
    }
    auto t2 = clock::now();
    std::vector<uint64_t> edges = omp ? detect_omp(records, opts.threads, opts.schedule,
                                                   opts.chunk, &res.stats)
                                      : detect(records, &res.stats);
    auto t3 = clock::now();
    switch (opts.impl) {
        case Impl::SeqStd: res.graph = build_csr(uint32_t(n), edges); break;
        case Impl::SeqRadix: res.graph = build_csr_radix(uint32_t(n), edges); break;
        case Impl::Omp: res.graph = build_csr_omp(uint32_t(n), edges, opts.threads); break;
    }
    auto t4 = clock::now();
    switch (opts.level) {
        case LevelAlgo::Sweep: res.levels = level(res.graph, &res.level_stats); break;
        case LevelAlgo::Kahn:
            res.levels = level_kahn(res.graph, opts.threads, &res.level_stats);
            break;
        case LevelAlgo::BellmanFord:
            res.levels = level_bellman_ford(res.graph, opts.threads, &res.level_stats);
            break;
    }
    auto t5 = clock::now();
    res.records = records.size();
    if (timings) {
        timings->extract = secs(t0, t1);
        timings->sort = secs(t1, t2);
        timings->detect = secs(t2, t3);
        timings->csr = secs(t3, t4);
        timings->level = secs(t4, t5);
    }
    return res;
}

Result run_pipeline(const Tx* tx, size_t n, Policy policy, const Selectors& sels,
                    Timings* timings) {
    return run_pipeline(tx, n, policy, sels, Options{}, timings);
}

}  // namespace conduit
