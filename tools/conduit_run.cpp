// Runs the sequential pipeline on super-blocks: per-stage timings (median of
// --runs after --warmup discarded runs), schedule statistics, and optionally
// a dump of the output for tools/verify_cpp.py.
//
//   conduit_run [--policy P1|P2|P3|all] (--id ID ... | --median) [--limit N]
//               [--runs 7] [--warmup 2] [--dump DIR] [--corpus DIR] [--manifest PATH]
//
// --median picks each day's median super-block at every target size.
// --limit N keeps the first N transactions (may split a block; for oracle slices).
//
// Dump format (little-endian), one file per (super-block, limit, policy):
//   "CDXP" | u32 version=1 | u32 policy (1..3) | u32 n | u64 tx_start | u64 m |
//   u32 levels[n] | u64 row_offsets[n+1] | u32 cols[m]
#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "conduit/corpus.hpp"
#include "conduit/pipeline.hpp"

using namespace conduit;

namespace {

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    size_t k = v.size() / 2;
    return v.size() % 2 ? v[k] : (v[k - 1] + v[k]) / 2;
}

// (sender id, tx count) of the busiest sender; ties go to the lowest id.
std::pair<uint32_t, uint32_t> max_sender(const Tx* tx, size_t n) {
    std::vector<uint32_t> f(n);
    for (size_t i = 0; i < n; ++i) f[i] = tx[i].frm;
    std::sort(f.begin(), f.end());
    uint32_t best = 0, best_id = 0;
    for (size_t i = 0; i < n;) {
        size_t j = i;
        while (j < n && f[j] == f[i]) ++j;
        if (j - i > best) { best = uint32_t(j - i); best_id = f[i]; }
        i = j;
    }
    return {best_id, best};
}

void write_or_die(std::FILE* f, const void* p, size_t bytes) {
    if (bytes && std::fwrite(p, 1, bytes, f) != bytes) throw std::runtime_error("write failed");
}

void dump(const std::string& path, Policy policy, uint64_t tx_start, const Result& r) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) throw std::runtime_error("cannot open " + path);
    const uint32_t version = 1, pol = uint32_t(policy) + 1, n = r.graph.n;
    const uint64_t m = r.graph.num_edges();
    write_or_die(f, "CDXP", 4);
    write_or_die(f, &version, 4);
    write_or_die(f, &pol, 4);
    write_or_die(f, &n, 4);
    write_or_die(f, &tx_start, 8);
    write_or_die(f, &m, 8);
    write_or_die(f, r.levels.data(), r.levels.size() * 4);
    write_or_die(f, r.graph.row_offsets.data(), r.graph.row_offsets.size() * 8);
    write_or_die(f, r.graph.cols.data(), r.graph.cols.size() * 4);
    std::fclose(f);
}

std::string file_tag(const std::string& id) {
    std::string s = id;
    std::replace(s.begin(), s.end(), '/', '_');
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    std::string corpus_dir = "corpus", manifest_path = "superblocks.json", policy_arg = "all";
    std::string dump_dir;
    std::vector<std::string> ids;
    bool use_median = false;
    size_t limit = 0;
    int runs = 7, warmup = 2;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { std::cerr << a << " needs a value\n"; std::exit(2); }
            return argv[++i];
        };
        if (a == "--corpus") corpus_dir = next();
        else if (a == "--manifest") manifest_path = next();
        else if (a == "--policy") policy_arg = next();
        else if (a == "--id") ids.push_back(next());
        else if (a == "--median") use_median = true;
        else if (a == "--limit") limit = std::stoul(next());
        else if (a == "--runs") runs = std::stoi(next());
        else if (a == "--warmup") warmup = std::stoi(next());
        else if (a == "--dump") dump_dir = next();
        else { std::cerr << "unknown argument " << a << "\n"; return 2; }
    }
    if (ids.empty() == !use_median || runs < 1 || warmup < 0) {
        std::cerr << "usage: conduit_run (--id ID ... | --median) [--policy P|all] [--limit N]"
                     " [--runs R] [--warmup W] [--dump DIR]\n";
        return 2;
    }

    try {
        Corpus corpus(corpus_dir);
        Manifest manifest = load_manifest(manifest_path, corpus);
        Selectors sels(corpus.selectors);

        std::vector<const SuperBlock*> chosen;
        for (const auto& id : ids) chosen.push_back(&manifest.get(id));
        if (use_median)
            for (const auto& date : manifest.dates())
                for (uint32_t t : manifest.targets) {
                    auto es = manifest.entries(date, t);
                    chosen.push_back(es[es.size() / 2]);
                }

        std::vector<Policy> policies;
        if (policy_arg == "all") policies = {Policy::P1, Policy::P2, Policy::P3};
        else policies = {parse_policy(policy_arg)};

        std::cout << "sequential C++ pipeline, single-threaded; times are medians of " << runs
                  << " runs after " << warmup << " warm-up runs (ms)\n\n";
        std::cout << std::left << std::setw(26) << "super-block" << std::right << std::setw(4)
                  << "pol" << std::setw(8) << "n" << std::setw(9) << "records" << std::setw(10)
                  << "edges" << std::setw(5) << "R-C" << std::setw(7) << "levels" << std::setw(7)
                  << "floor" << std::setw(7) << "lv/fl" << std::setw(8) << "tx/lv" << std::setw(7)
                  << "<32" << std::setw(7) << "lv0" << std::setw(17) << "batch min/med/max"
                  << std::setw(8) << "extract" << std::setw(7) << "sort" << std::setw(7)
                  << "detect" << std::setw(7) << "csr" << std::setw(7) << "level" << std::setw(8)
                  << "total" << std::setw(6) << "det" << "\n";

        for (const SuperBlock* sb : chosen) {
            size_t n = sb->tx_count;
            if (limit && limit < n) n = limit;
            const Tx* tx = corpus.tx() + sb->tx_start;
            auto [sender, floor] = max_sender(tx, n);
            if (n == sb->tx_count && floor != sb->max_sender_tx)
                throw std::runtime_error("sender chain disagrees with manifest for " + sb->id);

            for (Policy p : policies) {
                std::vector<double> te, ts, td, tc, tl, tt;
                Result first, last;
                for (int r = 0; r < warmup + runs; ++r) {
                    Timings t;
                    Result res = run_pipeline(tx, n, p, sels, &t);
                    if (r == 0) first = res;
                    if (r >= warmup) {
                        te.push_back(t.extract * 1e3);
                        ts.push_back(t.sort * 1e3);
                        td.push_back(t.detect * 1e3);
                        tc.push_back(t.csr * 1e3);
                        tl.push_back(t.level * 1e3);
                        tt.push_back(t.total() * 1e3);
                    }
                    if (r == warmup + runs - 1) last = std::move(res);
                }
                const bool deterministic = first.levels == last.levels &&
                                           first.graph.cols == last.graph.cols &&
                                           first.graph.row_offsets == last.graph.row_offsets;

                uint32_t L = 0;
                for (uint32_t v : last.levels) L = std::max(L, v + 1);
                std::vector<uint32_t> sizes(L, 0);
                for (uint32_t v : last.levels) ++sizes[v];
                size_t small = 0;
                for (uint32_t s : sizes) small += s < 32;
                std::vector<uint32_t> sorted_sizes = sizes;
                std::sort(sorted_sizes.begin(), sorted_sizes.end());

                std::ostringstream label;
                label << sb->id;
                if (n != sb->tx_count) label << "[:" << n << "]";
                std::cout << std::left << std::setw(26) << label.str() << std::right
                          << std::setw(4) << policy_name(p) << std::setw(8) << n << std::setw(9)
                          << last.records << std::setw(10) << last.graph.num_edges()
                          << std::setw(5) << last.stats.read_comm_edges << std::setw(7) << L
                          << std::setw(7) << floor << std::fixed << std::setprecision(2)
                          << std::setw(7) << double(L) / floor << std::setprecision(1)
                          << std::setw(8) << double(n) / L << std::setw(6)
                          << 100.0 * small / L << "%" << std::setw(6) << 100.0 * sizes[0] / n
                          << "%" << std::setw(6) << sorted_sizes.front() << std::setw(5)
                          << sorted_sizes[sorted_sizes.size() / 2] << std::setw(6)
                          << sorted_sizes.back() << std::setprecision(2) << std::setw(8)
                          << median(te) << std::setw(7) << median(ts) << std::setw(7)
                          << median(td) << std::setw(7) << median(tc) << std::setw(7)
                          << median(tl) << std::setw(8) << median(tt) << std::setw(6)
                          << (deterministic ? "yes" : "NO") << "\n";
                std::cout.unsetf(std::ios::fixed);

                if (!dump_dir.empty()) {
                    std::ostringstream path;
                    path << dump_dir << "/" << file_tag(sb->id);
                    if (n != sb->tx_count) path << "_" << n;
                    path << "_" << policy_name(p) << ".bin";
                    dump(path.str(), p, sb->tx_start, last);
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
