// Records every intermediate of the pipeline on a SMALL block, as JavaScript
// for the visualizer (visualizer/index.html):
//
//   conduit_trace --example [--out visualizer/trace_data.js]
//   conduit_trace --corpus DIR --id SUPERBLOCK [--offset K | --auto] [--limit 14] [--append]
//
// Nothing here is re-implemented for the picture: stage 1 is the policy port
// used by the GPU (gk::k_tx_access), stages 2-4 are the CPU reference stages
// (extract, sort_records, detect, build_csr, level) checked against the GPU
// pipeline (run_pipeline_gpu, emulation) on the spot, Jones-Plassmann runs the
// GPU kernel bodies (gk::jp_phase_a/b/c) round by round, and execution uses
// execute.hpp. The JSON therefore shows what the real code computes.
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <numeric>
#include <queue>
#include <sstream>
#include <string>
#include <vector>

#include "../src/detail.hpp"
#include "../src/gpu/kernels.hpp"
#include "conduit/corpus.hpp"
#include "conduit/execute.hpp"
#include "conduit/gpu.hpp"
#include "conduit/pipeline.hpp"
#include "conduit/schedule.hpp"

using namespace conduit;

namespace {

// ---- tiny JSON writer -------------------------------------------------------------

std::string esc(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        if (c == '\n') { o += "\\n"; continue; }
        o += c;
    }
    return o + "\"";
}
template <class T>
std::string arr(const std::vector<T>& v) {
    std::ostringstream s;
    s << "[";
    for (size_t i = 0; i < v.size(); ++i) s << (i ? "," : "") << v[i];
    s << "]";
    return s.str();
}
std::string sarr(const std::vector<std::string>& v) {
    std::string s = "[";
    for (size_t i = 0; i < v.size(); ++i) s += (i ? "," : "") + v[i];
    return s + "]";
}

const char* mode_name(uint8_t m) { return m == WRITE ? "W" : m == READ ? "R" : "C"; }
const char* cat_name(uint8_t c) {
    switch (c) {
        case CAT_PLAIN: return "ETH transfer";
        case CAT_TRANSFER: return "token transfer";
        case CAT_APPROVE: return "approve";
        case CAT_TRANSFER_FROM: return "transferFrom";
    }
    return "contract call";
}

struct Names {
    std::vector<std::string> addr;
    std::string operator()(uint32_t id) const {
        if (id == ABSENT) return "-";
        if (id < addr.size()) return addr[id];
        return "#" + std::to_string(id);
    }
};

std::string key_label(uint64_t key, const Names& nm) {
    const uint64_t M = (1ull << 21) - 1;
    if (key >> 63)
        return "ALLOWANCE(" + nm(uint32_t((key >> 42) & M)) + "," + nm(uint32_t((key >> 21) & M)) +
               "→" + nm(uint32_t(key & M)) + ")";
    const uint64_t kind = (key >> 60) & 7;
    const uint32_t x = uint32_t((key >> 21) & M), y = uint32_t(key & M);
    switch (kind) {
        case 0: return "ACCOUNT(" + nm(x) + ")";
        case 1: return "NONCE(" + nm(x) + ")";
        case 2: return "ETH_BAL(" + nm(x) + ")";
        case 3: return "TOKEN_BAL(" + nm(x) + "," + nm(y) + ")";
    }
    return "KEY";
}

// ---- built-in example: 12 transactions that exercise every rule --------------------

struct Example {
    std::vector<Tx> tx;
    std::vector<std::string> sel_names;
    Names names;
};

Example make_example() {
    Example e;
    e.sel_names = {"0x", "0xa9059cbb", "0x095ea7b3", "0x23b872dd", "0x3593564c", "0x1249c58b"};
    e.names.addr = {"Alice", "Bob", "Carol", "Dave", "Erin", "Exchange", "USDT", "USDC", "Router",
                    "NFT"};
    enum { ALICE, BOB, CAROL, DAVE, ERIN, EXCH, USDT, USDC, ROUTER, NFT };
    auto t = [&](uint16_t sel, uint32_t frm, uint32_t to, uint32_t a1, uint32_t a2, uint32_t gas,
                 uint8_t flags) {
        Tx x{};
        x.block = 25878705 + uint32_t(e.tx.size() / 6);
        x.idx = uint16_t(e.tx.size() % 6);
        x.sel = sel;
        x.frm = frm;
        x.to = to;
        x.a1 = a1;
        x.a2 = a2;
        x.gas = gas;
        x.status = 1;
        x.ttype = 2;
        x.flags = flags;
        e.tx.push_back(x);
    };
    const uint8_t D = FLAG_DECODED;
    t(0, ALICE, BOB, ABSENT, ABSENT, 21000, 0);           // 0 ETH Alice -> Bob
    t(1, EXCH, USDT, CAROL, ABSENT, 52000, D);           // 1 USDT Exchange -> Carol
    t(1, EXCH, USDT, DAVE, ABSENT, 52000, D);            // 2 USDT Exchange -> Dave (nonce chain)
    t(1, BOB, USDC, ALICE, ABSENT, 48000, D);            // 3 USDC Bob -> Alice
    t(4, ERIN, ROUTER, ABSENT, ABSENT, 165000, 0);       // 4 swap via Router (opaque call)
    t(1, CAROL, USDT, ERIN, ABSENT, 51000, D);           // 5 USDT Carol -> Erin (Carol's balance)
    t(4, DAVE, ROUTER, ABSENT, ABSENT, 180000, 0);       // 6 swap via Router (opaque call)
    t(2, ALICE, USDT, ROUTER, ABSENT, 46000, D);         // 7 approve(Router) on USDT
    t(3, BOB, USDT, ALICE, BOB, 61000, D);               // 8 USDT transferFrom(Alice -> Bob)
    t(5, ERIN, NFT, ABSENT, ABSENT, 120000, 0);          // 9 NFT mint (opaque call)
    t(0, DAVE, ALICE, ABSENT, ABSENT, 21000, 0);         // 10 ETH Dave -> Alice
    t(1, EXCH, USDT, BOB, ABSENT, 52000, D);             // 11 USDT Exchange -> Bob
    return e;
}

// ---- schedules for the execution view ---------------------------------------------

struct Slot {
    uint32_t tx, worker;
    double start, end;
};

// batches in order, LPT inside a batch, barrier between batches
std::vector<Slot> batch_schedule(const std::vector<uint32_t>& batch, const std::vector<double>& c,
                                 uint32_t W, std::vector<double>* barriers) {
    uint32_t B = 0;
    for (uint32_t b : batch) B = std::max(B, b + 1);
    std::vector<std::vector<uint32_t>> by(B);
    for (uint32_t t = 0; t < batch.size(); ++t) by[batch[t]].push_back(t);
    std::vector<Slot> out;
    double now = 0;
    for (auto& v : by) {
        std::sort(v.begin(), v.end(), [&](uint32_t a, uint32_t b) {
            return c[a] != c[b] ? c[a] > c[b] : a < b;
        });
        std::vector<double> free(W, now);
        for (uint32_t t : v) {
            const uint32_t w = uint32_t(std::min_element(free.begin(), free.end()) - free.begin());
            out.push_back({t, w, free[w], free[w] + c[t]});
            free[w] += c[t];
        }
        now = *std::max_element(free.begin(), free.end());
        if (barriers) barriers->push_back(now);
    }
    return out;
}

// dataflow list scheduling on the DAG (ready transactions in block order)
std::vector<Slot> dag_schedule(const Graph& g, const std::vector<double>& c, uint32_t W) {
    const uint32_t n = g.n;
    std::vector<uint32_t> indeg(n, 0);
    for (uint32_t v : g.cols) ++indeg[v];
    std::priority_queue<uint32_t, std::vector<uint32_t>, std::greater<uint32_t>> ready;
    for (uint32_t v = 0; v < n; ++v)
        if (!indeg[v]) ready.push(v);
    using Ev = std::tuple<double, uint32_t, uint32_t>;   // finish, tx, worker
    std::priority_queue<Ev, std::vector<Ev>, std::greater<Ev>> run;
    std::vector<uint32_t> idle;
    for (uint32_t w = 0; w < W; ++w) idle.push_back(W - 1 - w);
    std::vector<Slot> out;
    double now = 0;
    uint32_t done = 0;
    while (done < n) {
        while (!ready.empty() && !idle.empty()) {
            const uint32_t t = ready.top();
            ready.pop();
            const uint32_t w = idle.back();
            idle.pop_back();
            out.push_back({t, w, now, now + c[t]});
            run.push({now + c[t], t, w});
        }
        auto [f, t, w] = run.top();
        run.pop();
        now = f;
        ++done;
        idle.push_back(w);
        std::sort(idle.begin(), idle.end(), std::greater<uint32_t>());
        for (uint64_t k = g.row_offsets[t]; k < g.row_offsets[size_t(t) + 1]; ++k)
            if (--indeg[g.cols[k]] == 0) ready.push(g.cols[k]);
    }
    return out;
}

std::string slots_json(const std::vector<Slot>& s) {
    std::vector<std::string> v;
    for (const Slot& x : s) {
        std::ostringstream o;
        o << "{\"tx\":" << x.tx << ",\"w\":" << x.worker << ",\"s\":" << x.start
          << ",\"e\":" << x.end << "}";
        v.push_back(o.str());
    }
    return sarr(v);
}

// ---- Jones-Plassmann, round by round, with the GPU kernel bodies --------------------

std::string jp_trace(const std::vector<Record>& sorted, uint32_t n, bool random, uint32_t seed,
                     const Names& nm, std::vector<uint32_t>* colors_out) {
    const uint32_t R = uint32_t(sorted.size());
    std::vector<uint32_t> gid(R), rtx(R);
    std::vector<uint8_t> rmode(R);
    uint32_t G = 0;
    std::vector<uint64_t> gkey;
    for (uint32_t i = 0; i < R; ++i) {
        if (i == 0 || rec_key(sorted[i]) != rec_key(sorted[i - 1])) {
            ++G;
            gkey.push_back(rec_key(sorted[i]));
        }
        gid[i] = G - 1;
        rtx[i] = rec_tx(sorted[i]);
        rmode[i] = rec_mode(sorted[i]);
    }
    const uint32_t tx_bits = gk::bits_for(uint64_t(n) + 1);
    const uint32_t hash_bits = random ? std::min<uint32_t>(24, gk::JP_PRIO_BITS - tx_bits) : 0;
    std::vector<uint64_t> maxA(G, 0), maxW(G, 0), maxR(G, 0), maxC(G, 0);
    std::vector<uint32_t> fail(n, 0), color(n, gk::JP_UNCOLORED);
    gk::JpArgs a{gid.data(), rtx.data(), rmode.data(), maxA.data(), maxW.data(), maxR.data(),
                 maxC.data(), fail.data(), color.data(), tx_bits, hash_bits, seed};
    std::vector<uint32_t> at(n), ar(R), nat(n), nar(R);
    std::iota(at.begin(), at.end(), 0u);
    std::iota(ar.begin(), ar.end(), 0u);
    uint32_t ntx = n, nrec = R, round = 0;
    // priority rank for display: 0 = highest
    std::vector<uint64_t> prio(n);
    for (uint32_t t = 0; t < n; ++t) prio[t] = gk::jp_prio(t, tx_bits, hash_bits, seed);
    std::vector<uint32_t> order(n);
    std::iota(order.begin(), order.end(), 0u);
    std::sort(order.begin(), order.end(), [&](uint32_t x, uint32_t y) { return prio[x] > prio[y]; });
    std::vector<uint32_t> rank(n);
    for (uint32_t k = 0; k < n; ++k) rank[order[k]] = k;
    std::vector<std::string> rounds;
    while (ntx > 0) {
        for (uint32_t j = 0; j < nrec; ++j) gk::jp_phase_a(j, ar.data(), a, round);
        for (uint32_t j = 0; j < nrec; ++j) gk::jp_phase_b(j, ar.data(), a, round);
        // per key group: active accessors, whether each record is a local maximum
        std::map<uint32_t, std::vector<std::string>> per_group;
        for (uint32_t j = 0; j < nrec; ++j) {
            const uint32_t i = ar[j], g = gid[i], t = rtx[i];
            const uint64_t pk = gk::jp_pack(round, prio[t]);
            bool ok;
            if (rmode[i] == WRITE) ok = maxA[g] == pk;
            else if (rmode[i] == READ) ok = pk > maxW[g] && pk > maxC[g];
            else ok = pk > maxW[g] && pk > maxR[g];
            std::ostringstream o;
            o << "{\"tx\":" << t << ",\"m\":\"" << mode_name(rmode[i]) << "\",\"ok\":"
              << (ok ? "true" : "false") << "}";
            per_group[g].push_back(o.str());
        }
        std::vector<uint32_t> active(at.begin(), at.begin() + ntx);
        uint32_t nt = 0, nr = 0;
        for (uint32_t j = 0; j < std::max(ntx, nrec); ++j)
            gk::jp_phase_c(j, at.data(), ntx, ar.data(), nrec, a, round, nat.data(), &nt, nar.data(),
                           &nr);
        std::vector<uint32_t> won;
        for (uint32_t t : active)
            if (color[t] == round) won.push_back(t);
        std::sort(active.begin(), active.end());
        std::sort(won.begin(), won.end());
        std::vector<std::string> groups;
        for (auto& [g, v] : per_group)
            if (v.size() > 1)   // only keys where something competes
                groups.push_back("{\"key\":" + esc(key_label(gkey[g], nm)) + ",\"acc\":" + sarr(v) + "}");
        std::ostringstream o;
        o << "{\"round\":" << round << ",\"active\":" << arr(active) << ",\"winners\":" << arr(won)
          << ",\"groups\":" << sarr(groups) << "}";
        rounds.push_back(o.str());
        ntx = nt;
        nrec = nr;
        std::copy(nat.begin(), nat.begin() + nt, at.begin());
        std::copy(nar.begin(), nar.begin() + nr, ar.begin());
        ++round;
    }
    *colors_out = color;
    std::ostringstream o;
    o << "{\"random\":" << (random ? "true" : "false") << ",\"rank\":" << arr(rank)
      << ",\"rounds\":" << sarr(rounds) << ",\"color\":" << arr(color) << "}";
    return o.str();
}

// ---- one policy ------------------------------------------------------------------------

std::string trace_policy(const Tx* tx, uint32_t n, Policy p, const Selectors& sels,
                         const Names& nm) {
    std::ostringstream o;
    const gk::DevSelectors ds{sels.plain, sels.transfer, sels.approve, sels.transfer_from};
    // stage 1: per-thread access sets (the GPU port of the policy)
    std::vector<std::string> acc;
    std::vector<uint32_t> counts(n), offsets(n + 1, 0);
    for (uint32_t i = 0; i < n; ++i) {
        uint64_t keys[gk::K_MAX_ACC];
        uint8_t modes[gk::K_MAX_ACC];
        const int k = gk::k_tx_access(tx[i], int(p), ds, keys, modes);
        counts[i] = uint32_t(k);
        offsets[i + 1] = offsets[i] + uint32_t(k);
        std::vector<std::string> a;
        for (int j = 0; j < k; ++j)
            a.push_back("{\"key\":" + esc(key_label(keys[j], nm)) + ",\"m\":\"" + mode_name(modes[j]) + "\"}");
        acc.push_back(sarr(a));
    }
    std::vector<Record> rec = extract(tx, n, p, sels);
    auto recs = [&](const std::vector<Record>& v) {
        std::vector<std::string> s;
        for (const Record& r : v)
            s.push_back("{\"key\":" + esc(key_label(rec_key(r), nm)) + ",\"tx\":" +
                        std::to_string(rec_tx(r)) + ",\"m\":\"" + mode_name(rec_mode(r)) + "\"}");
        return sarr(s);
    };
    std::vector<Record> sorted = rec;
    sort_records(sorted);
    const uint32_t R = uint32_t(sorted.size());
    // flags and scans exactly as the GPU computes them
    std::vector<uint64_t> hi(R), lo(R);
    for (uint32_t i = 0; i < R; ++i) { hi[i] = sorted[i].hi; lo[i] = sorted[i].lo; }
    std::vector<uint32_t> gflag(R), rflag(R), gscan(R), rscan(R);
    for (uint32_t i = 0; i < R; ++i) gk::head_flags(i, hi.data(), lo.data(), gflag.data(), rflag.data());
    std::partial_sum(gflag.begin(), gflag.end(), gscan.begin());
    std::partial_sum(rflag.begin(), rflag.end(), rscan.begin());
    const uint32_t G = R ? gscan[R - 1] : 0, NR = R ? rscan[R - 1] : 0;
    std::vector<uint32_t> gstart(G + 1, R), rstart(NR + 1, R);
    for (uint32_t i = 0; i < R; ++i)
        gk::scatter_starts(i, gflag.data(), rflag.data(), gscan.data(), rscan.data(), gstart.data(),
                           rstart.data());
    std::vector<uint64_t> ecount(R), eoff(R + 1, 0);
    uint64_t rc = 0;
    for (uint32_t i = 0; i < R; ++i)
        gk::record_count(i, lo.data(), gflag.data(), rscan.data(), rstart.data(), ecount.data(), &rc);
    for (uint32_t i = 0; i < R; ++i) eoff[i + 1] = eoff[i] + ecount[i];
    const uint64_t E = eoff[R];
    std::vector<uint64_t> edges(E);
    for (uint64_t e = 0; e < E; ++e)
        gk::edge_emit(e, R, eoff.data(), lo.data(), rscan.data(), rstart.data(), edges.data());
    // which record produced each edge (load-balanced search), for the picture
    std::vector<std::string> emap;
    for (uint64_t e = 0; e < E; ++e) {
        const uint32_t c = uint32_t(std::upper_bound(eoff.begin(), eoff.begin() + R, e) - eoff.begin() - 1);
        const uint32_t a = rstart[rscan[c] - 2] + uint32_t(e - eoff[c]);
        emap.push_back("[" + std::to_string(edges[e] >> 32) + "," + std::to_string(edges[e] & 0xFFFFFFFF) +
                       "," + std::to_string(a) + "," + std::to_string(c) + "]");
    }
    // stage 3 and 4 (CPU reference), and the same through the GPU pipeline
    EdgeStats st;
    std::vector<uint64_t> det = detect(sorted, &st);
    std::vector<uint64_t> lin;
    for (uint64_t x : det) lin.push_back((x >> 32) * n + (x & 0xFFFFFFFF));
    std::vector<uint64_t> lin_sorted = lin;
    std::sort(lin_sorted.begin(), lin_sorted.end());
    std::vector<uint64_t> uniq = lin_sorted;
    uniq.erase(std::unique(uniq.begin(), uniq.end()), uniq.end());
    std::vector<uint64_t> tmp = det;
    const Graph g = build_csr(n, tmp);
    const std::vector<uint32_t> lv = level(g);
    GpuOptions go;
    go.backend = GpuBackend::Emu;
    const Result gr = run_pipeline_gpu(tx, n, p, sels, go);
    const bool gpu_same = gr.levels == lv && gr.graph.cols == g.cols && gr.graph.row_offsets == g.row_offsets;
    // full conflict graph (what Jones-Plassmann colours)
    const AccessSets sets = infer(tx, n, p, sels);
    std::vector<std::string> cedges;
    for (uint32_t i = 0; i < n; ++i)
        for (uint32_t j = i + 1; j < n; ++j) {
            std::string why;
            for (int64_t x = sets.offsets[i]; x < sets.offsets[i + 1]; ++x)
                for (int64_t y = sets.offsets[j]; y < sets.offsets[j + 1]; ++y)
                    if (sets.keys[x] == sets.keys[y] &&
                        !((sets.modes[x] == READ && sets.modes[y] == READ) ||
                          (sets.modes[x] == COMMUTATIVE && sets.modes[y] == COMMUTATIVE)) &&
                        why.empty())
                        why = key_label(sets.keys[x], nm);
            if (!why.empty())
                cedges.push_back("[" + std::to_string(i) + "," + std::to_string(j) + "," + esc(why) + "]");
        }
    std::vector<uint32_t> jcol_i, jcol_r;
    const std::string jp_i = jp_trace(sorted, n, false, 0, nm, &jcol_i);
    const std::string jp_r = jp_trace(sorted, n, true, 0x9E3779B9u, nm, &jcol_r);
    // execution: schedules on 3 workers and the real executors' state hashes
    const uint32_t W = 3;
    std::vector<double> cost(n);
    for (uint32_t i = 0; i < n; ++i) cost[i] = std::max<double>(tx[i].gas, 21000.0) / 1000.0;
    std::vector<double> bar_l, bar_j;
    const auto s_lv = batch_schedule(lv, cost, W, &bar_l);
    const auto s_jp = batch_schedule(jcol_r, cost, W, &bar_j);
    const auto s_dag = dag_schedule(g, cost, W);
    std::vector<Slot> s_ser;
    double now = 0;
    for (uint32_t i = 0; i < n; ++i) { s_ser.push_back({i, 0, now, now + cost[i]}); now += cost[i]; }
    const ExecPlan plan = make_plan(sets, tx, 1000);
    const ExecResult er = exec_serial(plan), el = exec_levels(plan, lv, 3), ed = exec_dag(plan, g, 3),
                     ej = exec_levels(plan, jcol_r, 3), ejs = exec_serial_order(plan, batch_order(jcol_r));
    auto hx = [](uint64_t h) { std::ostringstream s; s << std::hex << h; return esc(s.str()); };
    std::ostringstream ex;
    ex << "{\"workers\":" << W << ",\"cost\":" << arr(cost) << ",\"serial\":" << slots_json(s_ser)
       << ",\"levels\":" << slots_json(s_lv) << ",\"levels_barriers\":" << arr(bar_l)
       << ",\"jp\":" << slots_json(s_jp) << ",\"jp_barriers\":" << arr(bar_j)
       << ",\"dag\":" << slots_json(s_dag) << ",\"hash\":{\"serial\":" << hx(er.state_hash ^ er.output_hash)
       << ",\"levels\":" << hx(el.state_hash ^ el.output_hash) << ",\"dag\":" << hx(ed.state_hash ^ ed.output_hash)
       << ",\"jp\":" << hx(ej.state_hash ^ ej.output_hash) << ",\"jp_serial_order\":"
       << hx(ejs.state_hash ^ ejs.output_hash) << "}}";
    std::vector<uint32_t> cats(n);
    for (uint32_t i = 0; i < n; ++i) cats[i] = classify(tx[i], sels);

    o << "{\"access\":" << sarr(acc) << ",\"counts\":" << arr(counts) << ",\"offsets\":" << arr(offsets)
      << ",\"records\":" << recs(rec) << ",\"sorted\":" << recs(sorted) << ",\"gflag\":" << arr(gflag)
      << ",\"rflag\":" << arr(rflag) << ",\"gscan\":" << arr(gscan) << ",\"rscan\":" << arr(rscan)
      << ",\"gstart\":" << arr(gstart) << ",\"rstart\":" << arr(rstart) << ",\"ecount\":" << arr(ecount)
      << ",\"eoff\":" << arr(eoff) << ",\"edges\":" << sarr(emap) << ",\"lin\":" << arr(lin)
      << ",\"lin_sorted\":" << arr(lin_sorted) << ",\"unique\":" << arr(uniq)
      << ",\"row_offsets\":" << arr(g.row_offsets) << ",\"cols\":" << arr(g.cols) << ",\"levels\":" << arr(lv)
      << ",\"conflicts\":" << sarr(cedges) << ",\"jp_index\":" << jp_i << ",\"jp_random\":" << jp_r
      << ",\"exec\":" << ex.str() << ",\"stats\":{\"records\":" << R << ",\"groups\":" << G
      << ",\"runs\":" << NR << ",\"emitted\":" << E << ",\"edges\":" << g.num_edges()
      << ",\"batches\":" << (lv.empty() ? 0 : *std::max_element(lv.begin(), lv.end()) + 1)
      << ",\"bound\":" << clique_bound(sets) << ",\"gpu_identical\":" << (gpu_same ? "true" : "false")
      << ",\"jp_index_equals_levels\":" << (jcol_i == lv ? "true" : "false")
      << ",\"jp_random_conflict_free\":" << (check_batches(sets, jcol_r).ok ? "true" : "false")
      << "},\"category\":" << arr(cats) << "}";
    return o.str();
}

std::string trace_block(const std::string& title, const std::string& source, const Tx* tx, uint32_t n,
                        const std::vector<std::string>& sel_names, const Names& nm) {
    const Selectors sels(sel_names);
    std::vector<std::string> txs;
    for (uint32_t i = 0; i < n; ++i) {
        const Tx& t = tx[i];
        const std::string sel = t.sel < sel_names.size() ? sel_names[t.sel] : "?";
        std::ostringstream o;
        o << "{\"i\":" << i << ",\"block\":" << t.block << ",\"idx\":" << t.idx << ",\"from\":"
          << esc(nm(t.frm)) << ",\"to\":" << esc(t.flags & FLAG_CREATE ? "(new contract)" : nm(t.to))
          << ",\"sel\":" << esc(sel) << ",\"a1\":" << esc(nm(t.a1)) << ",\"a2\":" << esc(nm(t.a2))
          << ",\"gas\":" << t.gas << ",\"decoded\":" << ((t.flags & FLAG_DECODED) ? "true" : "false")
          << ",\"kind\":" << esc(cat_name(classify(t, sels))) << "}";
        txs.push_back(o.str());
    }
    std::ostringstream o;
    o << "{\"title\":" << esc(title) << ",\"source\":" << esc(source) << ",\"n\":" << n
      << ",\"txs\":" << sarr(txs) << ",\"policies\":{";
    const Policy ps[3] = {Policy::P1, Policy::P2, Policy::P3};
    for (int k = 0; k < 3; ++k)
        o << (k ? "," : "") << "\"" << policy_name(ps[k]) << "\":" << trace_policy(tx, n, ps[k], sels, nm);
    o << "}}";
    return o.str();
}

}  // namespace

int main(int argc, char** argv) {
    std::string corpus_dir = default_corpus_dir(), manifest, id, out = "visualizer/trace_data.js";
    bool example = false, append = false, autow = false;
    size_t offset = 0, limit = 14;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { std::cerr << a << " needs a value\n"; std::exit(2); }
            return argv[++i];
        };
        if (a == "--example") example = true;
        else if (a == "--corpus") corpus_dir = next();
        else if (a == "--manifest") manifest = next();
        else if (a == "--id") id = next();
        else if (a == "--offset") offset = std::stoul(next());
        else if (a == "--auto") autow = true;
        else if (a == "--limit") limit = std::stoul(next());
        else if (a == "--out") out = next();
        else if (a == "--append") append = true;
        else { std::cerr << "unknown argument " << a << "\n"; return 2; }
    }
    try {
        std::string js;
        if (example) {
            Example e = make_example();
            js = trace_block("Built-in example: 12 transactions that exercise every rule",
                             "hand-made block (names instead of addresses)", e.tx.data(),
                             uint32_t(e.tx.size()), e.sel_names, e.names);
        } else {
            if (id.empty()) throw std::runtime_error("--example or --id SUPERBLOCK");
            Corpus corpus(corpus_dir);
            Manifest m = load_manifest(resolve_manifest(manifest, corpus_dir), corpus);
            const SuperBlock& sb = m.get(id);
            Names nm;
            {
                std::ifstream f(corpus_dir + "/addresses.txt");
                std::string s;
                while (std::getline(f, s))
                    nm.addr.push_back(s.size() >= 8 ? "0x" + s.substr(0, 4) + ".." + s.substr(s.size() - 4) : s);
            }
            if (limit > 40) throw std::runtime_error("--limit at most 40 (the views are for small blocks)");
            const Tx* base = corpus.tx() + sb.tx_start;
            if (autow) {
                // an instructive window among the first 3000: most P2 dependency edges
                // while P2 still has some parallelism (3 <= batches <= 60% of n) and
                // the policies disagree (P1 needs more batches than P3)
                const Selectors s(corpus.selectors);
                size_t best = 0;
                double best_score = -1;
                for (size_t off = 0; off + limit <= std::min<size_t>(sb.tx_count, 3000); ++off) {
                    auto batches = [&](Policy p, uint64_t* m) {
                        const Result r = run_pipeline(base + off, limit, p, s);
                        if (m) *m = r.graph.num_edges();
                        return 1 + *std::max_element(r.levels.begin(), r.levels.end());
                    };
                    uint64_t m2 = 0;
                    const uint32_t b1 = batches(Policy::P1, nullptr), b2 = batches(Policy::P2, &m2),
                                   b3 = batches(Policy::P3, nullptr);
                    if (b2 < 3 || b2 > 0.6 * double(limit) || b1 <= b3) continue;
                    const double score = double(m2) + 2.0 * (b1 - b3);
                    if (score > best_score) { best_score = score; best = off; }
                }
                offset = best;
            }
            if (offset + limit > sb.tx_count) throw std::runtime_error("window past the super-block");
            std::ostringstream title, src;
            title << "Real Ethereum: " << limit << " consecutive transactions of " << id;
            src << corpus_dir << ", super-block " << id << ", transactions " << offset << ".."
                << offset + limit - 1 << " (block " << base[offset].block << ")";
            js = trace_block(title.str(), src.str(), base + offset, uint32_t(limit), corpus.selectors, nm);
        }
        std::string prev;
        if (append) {
            std::ifstream f(out);
            std::stringstream ss;
            ss << f.rdbuf();
            prev = ss.str();
        }
        std::ofstream f(out);
        if (!append || prev.find("CONDUIT_TRACES") == std::string::npos) {
            f << "// generated by build/conduit_trace -- do not edit by hand\n"
                 "window.CONDUIT_TRACES = window.CONDUIT_TRACES || [];\n";
        } else {
            f << prev;
        }
        f << "window.CONDUIT_TRACES.push(" << js << ");\n";
        std::cout << "wrote " << out << "\n";
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
