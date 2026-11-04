// Host emulation of the GPU pipeline: the kernel bodies of kernels.hpp run
// sequentially, launch by launch, with CUB's primitives replaced by their
// sequential definitions (stable sort, exclusive scan, unique). The control
// flow (stage order, tail/multi-block switch, round counting) mirrors
// cuda_pipeline.cu, so the emulation reports the same round counts.
#include <algorithm>
#include <chrono>
#include <numeric>
#include <stdexcept>
#include <vector>

#include "conduit/gpu.hpp"
#include "kernels.hpp"

namespace conduit::gpu_detail {

namespace {

using clock_t_ = std::chrono::steady_clock;
double secs(clock_t_::time_point a, clock_t_::time_point b) {
    return std::chrono::duration<double>(b - a).count();
}

template <class T>
void exclusive_scan_into(const std::vector<T>& in, std::vector<T>& out) {
    out.assign(in.size() + 1, 0);
    for (size_t i = 0; i < in.size(); ++i) out[i + 1] = out[i] + in[i];
}

template <class T>
void inclusive_scan_into(const std::vector<T>& in, std::vector<T>& out) {
    out.resize(in.size());
    T acc = 0;
    for (size_t i = 0; i < in.size(); ++i) out[i] = acc += in[i];
}

}  // namespace

Result run_emu(const Tx* tx, size_t n_in, Policy policy, const Selectors& sels,
               const GpuOptions& opts, GpuTimings* timings) {
    if (n_in > 0x7FFFFFFFull) throw std::runtime_error("slice too large for the GPU pipeline");
    const auto h0 = clock_t_::now();
    check_ids(tx, n_in);
    const uint32_t n = uint32_t(n_in);
    const gk::DevSelectors ds{sels.plain, sels.transfer, sels.approve, sels.transfer_from};
    const int pol = int(policy);
    GpuTimings tm;
    Result res;
    const auto w0 = h0;
    tm.host_prep = secs(h0, clock_t_::now());

    // ---- stage 1: extraction -------------------------------------------------
    auto t0 = clock_t_::now();
    std::vector<uint32_t> counts(n), off;
    for (uint32_t i = 0; i < n; ++i) gk::extract_count(i, tx, pol, ds, counts.data());
    exclusive_scan_into(counts, off);
    const uint32_t R = n ? off[n] : 0;
    std::vector<uint64_t> hi(R), lo(R);
    for (uint32_t i = 0; i < n; ++i) gk::extract_write(i, tx, pol, ds, off.data(), hi.data(), lo.data());
    tm.launches += 3;
    if (opts.capture) {
        opts.capture->extracted.resize(R);
        for (uint32_t i = 0; i < R; ++i) opts.capture->extracted[i] = {hi[i], lo[i]};
    }
    auto t1 = clock_t_::now();

    // ---- stage 2a: stable sort by key (CUB SortPairs is stable) ---------------
    {
        std::vector<uint32_t> idx(R);
        std::iota(idx.begin(), idx.end(), 0u);
        std::stable_sort(idx.begin(), idx.end(),
                         [&](uint32_t a, uint32_t b) { return hi[a] < hi[b]; });
        std::vector<uint64_t> h2(R), l2(R);
        for (uint32_t i = 0; i < R; ++i) { h2[i] = hi[idx[i]]; l2[i] = lo[idx[i]]; }
        hi.swap(h2);
        lo.swap(l2);
    }
    tm.launches += 1;
    if (opts.capture) {
        opts.capture->sorted.resize(R);
        for (uint32_t i = 0; i < R; ++i) opts.capture->sorted[i] = {hi[i], lo[i]};
    }
    auto t2 = clock_t_::now();

    // ---- stage 2b: detection ----------------------------------------------------
    std::vector<uint32_t> gflag(R), rflag(R), gscan, rscan;
    for (uint32_t i = 0; i < R; ++i) gk::head_flags(i, hi.data(), lo.data(), gflag.data(), rflag.data());
    inclusive_scan_into(gflag, gscan);
    inclusive_scan_into(rflag, rscan);
    const uint32_t G = R ? gscan[R - 1] : 0, NR = R ? rscan[R - 1] : 0;
    std::vector<uint32_t> gstart(G + 1), rstart(NR + 1);
    for (uint32_t i = 0; i < R; ++i)
        gk::scatter_starts(i, gflag.data(), rflag.data(), gscan.data(), rscan.data(),
                           gstart.data(), rstart.data());
    gstart[G] = R;
    rstart[NR] = R;
    uint32_t max_group = 0;
    for (uint32_t g = 0; g < G; ++g) gk::group_size_max(g, gstart.data(), &max_group);
    tm.launches += 5;

    uint64_t read_comm = 0, E = 0;
    std::vector<uint64_t> edges;
    if (opts.detect == DetectMode::Group) {
        std::vector<uint64_t> gcount(G), goff;
        for (uint32_t g = 0; g < G; ++g)
            gk::group_count(g, lo.data(), gstart.data(), gcount.data(), &read_comm);
        exclusive_scan_into(gcount, goff);
        E = goff[G];
        edges.resize(E);
        for (uint32_t g = 0; g < G; ++g)
            gk::group_emit(g, lo.data(), gstart.data(), goff.data(), edges.data());
    } else {
        std::vector<uint64_t> ecount(R), eoff;
        for (uint32_t i = 0; i < R; ++i)
            gk::record_count(i, lo.data(), gflag.data(), rscan.data(), rstart.data(),
                             ecount.data(), &read_comm);
        exclusive_scan_into(ecount, eoff);
        E = eoff[R];
        edges.resize(E);
        if (opts.detect == DetectMode::Record) {
            for (uint32_t c = 0; c < R; ++c)
                gk::record_emit(c, eoff.data(), lo.data(), gflag.data(), rscan.data(),
                                rstart.data(), edges.data());
        } else {
            for (uint64_t e = 0; e < E; ++e)
                gk::edge_emit(e, R, eoff.data(), lo.data(), rscan.data(), rstart.data(),
                              edges.data());
        }
    }
    if (opts.capture) opts.capture->edges = edges;
    tm.launches += 3;
    tm.host_syncs += 2;
    auto t3 = clock_t_::now();

    // ---- stage 3: CSR -------------------------------------------------------------
    std::vector<uint64_t> lin(E);
    for (uint64_t e = 0; e < E; ++e) gk::edge_to_linear(e, n, edges.data(), lin.data());
    std::sort(lin.begin(), lin.end());
    lin.erase(std::unique(lin.begin(), lin.end()), lin.end());
    const uint64_t M = lin.size();
    res.graph.n = n;
    res.graph.cols.resize(M);
    res.graph.row_offsets.resize(size_t(n) + 1);
    for (uint64_t e = 0; e < M; ++e) gk::csr_cols(e, n, lin.data(), res.graph.cols.data());
    for (uint32_t u = 0; u <= n; ++u) gk::csr_rows(u, n, M, lin.data(), res.graph.row_offsets.data());
    tm.launches += 5;
    tm.host_syncs += 1;
    auto t4 = clock_t_::now();

    // ---- stage 4: levelling ---------------------------------------------------------
    const Graph& g = res.graph;
    const uint32_t T = opts.tail_threshold;
    if (opts.level == GpuLevel::Sweep) {
        res.levels = level(g, &res.level_stats);
    } else if (opts.level == GpuLevel::Kahn) {
        std::vector<uint32_t> indeg(n, 0), lv(n, 0), fa(n), fb(n);
        for (uint64_t e = 0; e < M; ++e) gk::kahn_indeg(e, g.cols.data(), indeg.data());
        uint32_t cnt = 0;
        for (uint32_t v = 0; v < n; ++v) gk::kahn_init(v, indeg.data(), lv.data(), fa.data(), &cnt);
        tm.launches += 2;
        tm.host_syncs += 1;
        // round r reads buffer [r & 1]: same parity rule as the device
        uint32_t* f[2] = {fa.data(), fb.data()};
        uint32_t size = cnt, round = 0;
        auto kahn_round = [&](uint32_t& sz, uint32_t& r) {
            uint32_t next = 0;
            for (uint32_t i = 0; i < sz; ++i)
                gk::kahn_expand(i, f[r & 1], g.row_offsets.data(), g.cols.data(), indeg.data(),
                                lv.data(), r + 1, f[(r + 1) & 1], &next);
            sz = next;
            ++r;
        };
        const uint32_t K = std::max<uint32_t>(1, opts.chunk);
        while (size > 0) {
            if (size > T) {   // device-driven chunk of K rounds (no-ops once finished)
                for (uint32_t k = 0; k < K && size > 0; ++k) kahn_round(size, round);
                tm.launches += 2 * K;
                ++tm.host_syncs;
            } else {          // persistent block
                ++tm.launches;
                do {
                    kahn_round(size, round);
                    ++tm.tail_rounds;
                } while (size > 0 && size <= T);
                ++tm.host_syncs;
            }
        }
        res.levels = std::move(lv);
        res.level_stats = {M, 2 * M, round};
        tm.level_rounds = round;
    } else {
        const bool random = opts.level == GpuLevel::JpRandom;
        const uint32_t tx_bits = gk::bits_for(uint64_t(n) + 1);
        const uint32_t hash_bits = random ? std::min<uint32_t>(24, gk::JP_PRIO_BITS - tx_bits) : 0;
        std::vector<uint32_t> r_gid(R), r_tx(R);
        std::vector<uint8_t> r_mode(R);
        for (uint32_t i = 0; i < R; ++i)
            gk::jp_unpack(i, gscan.data(), lo.data(), r_gid.data(), r_tx.data(), r_mode.data());
        std::vector<uint64_t> maxA(G, 0), maxW(G, 0), maxR(G, 0), maxC(G, 0);
        std::vector<uint32_t> fail(n, 0), color(n, gk::JP_UNCOLORED);
        std::vector<uint32_t> at(n), at2(n), ar(R), ar2(R);
        std::iota(at.begin(), at.end(), 0u);
        std::iota(ar.begin(), ar.end(), 0u);
        gk::JpArgs a{r_gid.data(), r_tx.data(), r_mode.data(), maxA.data(), maxW.data(),
                     maxR.data(), maxC.data(), fail.data(), color.data(), tx_bits, hash_bits,
                     opts.jp_seed};
        tm.launches += 3;
        uint32_t ntx = n, nrec = R, round = 0;
        uint32_t* at_[2] = {at.data(), at2.data()};
        uint32_t* ar_[2] = {ar.data(), ar2.data()};
        uint64_t relax = 0;
        auto jp_round = [&]() {
            const uint32_t* car = ar_[round & 1];
            for (uint32_t j = 0; j < nrec; ++j) gk::jp_phase_a(j, car, a, round);
            for (uint32_t j = 0; j < nrec; ++j) gk::jp_phase_b(j, car, a, round);
            uint32_t nt = 0, nr = 0;
            const uint32_t span = std::max(ntx, nrec);
            for (uint32_t j = 0; j < span; ++j)
                gk::jp_phase_c(j, at_[round & 1], ntx, car, nrec, a, round, at_[(round + 1) & 1],
                               &nt, ar_[(round + 1) & 1], &nr);
            relax += nrec;
            ntx = nt;
            nrec = nr;
            ++round;
        };
        const uint32_t K = std::max<uint32_t>(1, opts.chunk);
        while (ntx > 0) {
            if (std::max(ntx, nrec) > T) {
                for (uint32_t k = 0; k < K && ntx > 0; ++k) jp_round();
                tm.launches += 4 * K;
                ++tm.host_syncs;
            } else {
                ++tm.launches;
                do {
                    jp_round();
                    ++tm.tail_rounds;
                } while (ntx > 0 && std::max(ntx, nrec) <= T);
                ++tm.host_syncs;
            }
        }
        res.levels = std::move(color);
        res.level_stats = {relax, 2 * relax, round};
        tm.level_rounds = round;
    }
    auto t5 = clock_t_::now();

    res.records = R;
    res.stats.emitted = E;
    res.stats.read_comm_edges = read_comm;
    res.stats.groups = G;
    res.stats.imbalance = 0;
    tm.max_group = max_group;
    tm.extract = secs(t0, t1);
    tm.sort = secs(t1, t2);
    tm.detect = secs(t2, t3);
    tm.csr = secs(t3, t4);
    tm.level = secs(t4, t5);
    tm.wall = secs(w0, t5);
    if (timings) *timings = tm;
    return res;
}

}  // namespace conduit::gpu_detail
