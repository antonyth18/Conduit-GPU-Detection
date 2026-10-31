// Phase 7 benchmark driver.
//
//   conduit_bench identical [--threads 1,2,4,8] [--bf-every K] [--date D] [--target T]
//       For every super-block (optionally filtered) and every policy, runs seq-std,
//       seq-radix and omp at each thread count and requires identical sorted
//       records, edges, CSR and levels. Every K-th super-block also checks
//       Bellman-Ford levelling (default K = 25; 0 disables).
//
//   conduit_bench scale --csv FILE [--threads 1,2,4,8] [--schedules dynamic,static,guided]
//                       [--limits 1000,2000,5000] [--bf] [--runs 7] [--warmup 2]
//                       [--policy P1|P2|P3|all]
//       Each day's median super-block at every target, plus prefixes of the median
//       10k super-block (--limits) to locate the crossover. For each configuration:
//       median per-stage times, level instrumentation, detection imbalance.
//
// Common: [--corpus DIR] [--manifest PATH]
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "conduit/corpus.hpp"
#include "conduit/pipeline.hpp"
#include "conduit/radix.hpp"

#ifdef CONDUIT_HAVE_OPENMP
#include <omp.h>
#endif

using namespace conduit;

namespace {

std::vector<int> parse_ints(const std::string& s) {
    std::vector<int> out;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) out.push_back(std::stoi(tok));
    return out;
}

std::vector<std::string> parse_strs(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) out.push_back(tok);
    return out;
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    size_t k = v.size() / 2;
    return v.size() % 2 ? v[k] : (v[k - 1] + v[k]) / 2;
}

bool same(const Result& a, const Result& b) {
    return a.levels == b.levels && a.graph.cols == b.graph.cols &&
           a.graph.row_offsets == b.graph.row_offsets && a.stats.emitted == b.stats.emitted &&
           a.stats.read_comm_edges == b.stats.read_comm_edges && a.records == b.records;
}

struct Common {
    std::string corpus = "corpus", manifest = "superblocks.json";
    std::vector<int> threads = {1, 2, 4, 8};
    std::vector<Policy> policies = {Policy::P1, Policy::P2, Policy::P3};
};

int run_identical(const Corpus& corpus, const Manifest& m, const Selectors& sels,
                  const Common& c, int bf_every, const std::string& date, uint32_t target) {
    uint64_t runs = 0, mismatches = 0, k = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (const auto& sb : m.superblocks) {
        if (!date.empty() && sb.date != date) continue;
        if (target && sb.target != target) continue;
        const Tx* tx = corpus.tx() + sb.tx_start;
        const size_t n = sb.tx_count;
        const bool bf = bf_every > 0 && k++ % uint64_t(bf_every) == 0;
        for (Policy p : c.policies) {
            Options base;
            const Result ref = run_pipeline(tx, n, p, sels, base);

            // the three record sorts must agree element for element
            std::vector<Record> a = extract(tx, n, p, sels), b = a, d = a;
            sort_records(a);
            radix_sort_records_seq(b);
            bool sorts_ok = a == b;

            std::vector<Options> variants;
            Options sr;
            sr.impl = Impl::SeqRadix;
            variants.push_back(sr);
            if (have_openmp()) {
                d = extract_omp(tx, n, p, sels, c.threads.back());
                radix_sort_records_omp(d, c.threads.back());
                sorts_ok = sorts_ok && a == d;
                for (int t : c.threads) {
                    Options o;
                    o.impl = Impl::Omp;
                    o.level = LevelAlgo::Kahn;
                    o.threads = t;
                    variants.push_back(o);
                    if (bf && t == c.threads.back()) {
                        o.level = LevelAlgo::BellmanFord;
                        variants.push_back(o);
                    }
                }
            }
            if (!sorts_ok) {
                ++mismatches;
                std::cerr << "MISMATCH sorted records " << sb.id << " " << policy_name(p) << "\n";
            }
            for (const Options& o : variants) {
                const Result r = run_pipeline(tx, n, p, sels, o);
                ++runs;
                if (!same(ref, r)) {
                    ++mismatches;
                    std::cerr << "MISMATCH " << sb.id << " " << policy_name(p) << " "
                              << impl_name(o.impl) << " t=" << o.threads << " "
                              << level_name(o.level) << "\n";
                }
            }
        }
    }
    const double secs =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << runs << " comparisons against seq-std, " << mismatches << " mismatches ("
              << secs << " s)\n"
              << (mismatches ? "OUTPUT DIFFERS" : "ALL IDENTICAL") << "\n";
    return mismatches ? 1 : 0;
}

int run_scale(const Corpus& corpus, const Manifest& m, const Selectors& sels, const Common& c,
              const std::string& csv_path, const std::vector<std::string>& schedules,
              const std::vector<int>& limits, bool with_bf, int runs, int warmup) {
    struct Case {
        const SuperBlock* sb;
        size_t n;
    };
    std::vector<Case> cases;
    for (const auto& date : m.dates()) {
        for (uint32_t t : m.targets) {
            auto es = m.entries(date, t);
            const SuperBlock* med = es[es.size() / 2];
            if (t == m.targets.front())
                for (int lim : limits) cases.push_back({med, size_t(lim)});
            cases.push_back({med, med->tx_count});
        }
    }

    std::FILE* csv = std::fopen(csv_path.c_str(), "wb");
    if (!csv) throw std::runtime_error("cannot open " + csv_path);
    std::fputs("id,date,target,n,policy,impl,threads,schedule,level_algo,records,edges,levels,"
               "relaxations,atomics,rounds,imbalance,t_extract_ms,t_sort_ms,t_detect_ms,"
               "t_csr_ms,t_level_ms,t_total_ms\n",
               csv);

    for (const Case& cs : cases) {
        const Tx* tx = corpus.tx() + cs.sb->tx_start;
        for (Policy p : c.policies) {
            std::vector<Options> grid;
            Options o;
            o.impl = Impl::SeqStd;
            grid.push_back(o);
            o.impl = Impl::SeqRadix;
            grid.push_back(o);
            if (have_openmp()) {
                for (int t : c.threads)
                    for (const auto& s : schedules) {
                        Options q;
                        q.impl = Impl::Omp;
                        q.level = LevelAlgo::Kahn;
                        q.threads = t;
                        q.schedule = parse_schedule(s);
                        grid.push_back(q);
                        // hybrid: parallel stages 1-4, sequential sweep for levelling
                        q.level = LevelAlgo::Sweep;
                        grid.push_back(q);
                        if (with_bf && s == schedules.front()) {
                            q.level = LevelAlgo::BellmanFord;
                            grid.push_back(q);
                        }
                    }
            }
            for (const Options& q : grid) {
                std::vector<double> te, ts, td, tc, tl, tt;
                Result last;
                for (int r = 0; r < warmup + runs; ++r) {
                    Timings tm;
                    Result res = run_pipeline(tx, cs.n, p, sels, q, &tm);
                    if (r >= warmup) {
                        te.push_back(tm.extract * 1e3);
                        ts.push_back(tm.sort * 1e3);
                        td.push_back(tm.detect * 1e3);
                        tc.push_back(tm.csr * 1e3);
                        tl.push_back(tm.level * 1e3);
                        tt.push_back(tm.total() * 1e3);
                    }
                    if (r == warmup + runs - 1) last = std::move(res);
                }
                uint32_t L = 0;
                for (uint32_t v : last.levels) L = std::max(L, v + 1);
                const bool omp = q.impl == Impl::Omp;
                std::fprintf(csv,
                             "%s,%s,%u,%zu,%s,%s,%d,%s,%s,%zu,%llu,%u,%llu,%llu,%llu,%.4f,%.4f,"
                             "%.4f,%.4f,%.4f,%.4f,%.4f\n",
                             cs.sb->id.c_str(), cs.sb->date.c_str(), cs.sb->target, cs.n,
                             policy_name(p), impl_name(q.impl), omp ? q.threads : 1,
                             omp ? schedule_name(q.schedule) : "-", level_name(q.level),
                             last.records, (unsigned long long)last.graph.num_edges(), L,
                             (unsigned long long)last.level_stats.relaxations,
                             (unsigned long long)last.level_stats.atomics,
                             (unsigned long long)last.level_stats.rounds, last.stats.imbalance,
                             median(te), median(ts), median(td), median(tc), median(tl),
                             median(tt));
                std::fflush(csv);
            }
            std::cout << "  " << cs.sb->id << " n=" << cs.n << " " << policy_name(p) << ": "
                      << grid.size() << " configurations\n"
                      << std::flush;
        }
    }
    std::fclose(csv);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: conduit_bench identical|scale [options]\n";
        return 2;
    }
    const std::string mode = argv[1];
    Common c;
    std::string csv, date;
    std::vector<std::string> schedules = {"dynamic", "static", "guided"};
    std::vector<int> limits = {1000, 2000, 5000};
    uint32_t target = 0;
    int bf_every = 25, runs = 7, warmup = 2;
    bool with_bf = false;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << a << " needs a value\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--corpus") c.corpus = next();
        else if (a == "--manifest") c.manifest = next();
        else if (a == "--threads") c.threads = parse_ints(next());
        else if (a == "--policy") {
            std::string p = next();
            c.policies = p == "all" ? std::vector<Policy>{Policy::P1, Policy::P2, Policy::P3}
                                    : std::vector<Policy>{parse_policy(p)};
        } else if (a == "--csv") csv = next();
        else if (a == "--schedules") schedules = parse_strs(next());
        else if (a == "--limits") limits = parse_ints(next());
        else if (a == "--bf") with_bf = true;
        else if (a == "--bf-every") bf_every = std::stoi(next());
        else if (a == "--runs") runs = std::stoi(next());
        else if (a == "--warmup") warmup = std::stoi(next());
        else if (a == "--date") date = next();
        else if (a == "--target") target = uint32_t(std::stoul(next()));
        else {
            std::cerr << "unknown argument " << a << "\n";
            return 2;
        }
    }
    try {
        Corpus corpus(c.corpus);
        Manifest m = load_manifest(c.manifest, corpus);
        Selectors sels(corpus.selectors);
        std::cout << "conduit_bench " << mode << ": OpenMP " << (have_openmp() ? "on" : "off");
#ifdef CONDUIT_HAVE_OPENMP
        std::cout << ", max threads " << omp_get_max_threads() << ", procs "
                  << omp_get_num_procs();
#endif
        std::cout << "\n";
        if (mode == "identical") return run_identical(corpus, m, sels, c, bf_every, date, target);
        if (mode == "scale") {
            if (csv.empty()) throw std::runtime_error("scale needs --csv FILE");
            return run_scale(corpus, m, sels, c, csv, schedules, limits, with_bf, runs, warmup);
        }
        std::cerr << "unknown mode " << mode << "\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
