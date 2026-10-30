#include "conduit/pipeline.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace conduit {

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
    uint64_t read_comm = 0;
    const size_t N = r.size();
    size_t i = 0;
    while (i < N) {
        const uint64_t key = rec_key(r[i]);
        size_t end = i;
        while (end < N && rec_key(r[end]) == key) ++end;

        // runs inside [i, end): consecutive READs, consecutive COMMs, or one WRITE
        size_t prev_begin = 0, prev_end = 0;
        uint8_t prev_mode = 0;
        size_t p = i;
        while (p < end) {
            const uint8_t mode = rec_mode(r[p]);
            size_t q = p + 1;
            if (mode != WRITE)
                while (q < end && rec_mode(r[q]) == mode) ++q;
            if (prev_end > prev_begin) {
                const bool rc = (prev_mode | mode) == (READ | COMMUTATIVE);
                for (size_t a = prev_begin; a < prev_end; ++a)
                    for (size_t b = p; b < q; ++b)
                        edges.push_back((uint64_t(rec_tx(r[a])) << 32) | rec_tx(r[b]));
                if (rc) read_comm += uint64_t(prev_end - prev_begin) * (q - p);
            }
            prev_begin = p;
            prev_end = q;
            prev_mode = mode;
            p = q;
        }
        i = end;
    }
    if (stats) {
        stats->emitted = edges.size();
        stats->read_comm_edges = read_comm;
    }
    return edges;
}

Graph build_csr(uint32_t n, std::vector<uint64_t>& edges) {
    std::sort(edges.begin(), edges.end());
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

std::vector<uint32_t> level(const Graph& g) {
    std::vector<uint32_t> lv(g.n, 0);
    for (uint32_t u = 0; u < g.n; ++u) {
        const uint32_t next = lv[u] + 1;
        for (uint64_t e = g.row_offsets[u]; e < g.row_offsets[size_t(u) + 1]; ++e) {
            uint32_t& t = lv[g.cols[e]];
            if (t < next) t = next;
        }
    }
    return lv;
}

Result run_pipeline(const Tx* tx, size_t n, Policy policy, const Selectors& sels,
                    Timings* timings) {
    using clock = std::chrono::steady_clock;
    auto secs = [](clock::time_point a, clock::time_point b) {
        return std::chrono::duration<double>(b - a).count();
    };
    Result res;
    auto t0 = clock::now();
    std::vector<Record> records = extract(tx, n, policy, sels);
    auto t1 = clock::now();
    sort_records(records);
    auto t2 = clock::now();
    std::vector<uint64_t> edges = detect(records, &res.stats);
    auto t3 = clock::now();
    res.graph = build_csr(uint32_t(n), edges);
    auto t4 = clock::now();
    res.levels = level(res.graph);
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

}  // namespace conduit
