// Phase 8-10 driver for the GPU pipeline.
//
//   conduit_gpu info
//       Build configuration and the visible CUDA device.
//
//   conduit_gpu verify [cases] [--policy P|all] [--backend cuda|emu] [--brute N]
//       NO TIMING WITHOUT CORRECTNESS. For every case and policy, every GPU
//       configuration (detect group|balanced x level sweep|kahn|jp-index, two
//       tail thresholds) must equal seq-std bit for bit (records, edges, CSR,
//       levels, stats). jp-random must produce conflict-free batches
//       (check_batches on the access sets), identical across thresholds and,
//       with a GPU, identical between cuda and emu. Levels must pass both
//       schedule checks, and on the first N transactions (default 2000) must
//       equal the brute-force O(n^2) oracle built from the conflict definition.
//
//   conduit_gpu bench --csv FILE [cases] [--limits 1000,2000,5000] [--threads 1,4,8]
//                     [--runs 7] [--warmup 2] [--configs quick|full] [--backend cuda|emu]
//                     [--tail T] [--chunk K]   (GPU round controls, gpu.hpp)
//       Crossover sweep. CPU: seq-std, seq-radix, omp (hybrid: parallel stages
//       1-3, sweep) per thread count. GPU: balanced x {sweep, kahn, jp-index,
//       jp-random} plus group x sweep (quick) or every detect x level (full).
//       Each configuration is checked against seq-std before it is timed.
//       Medians of --runs after --warmup discarded runs.
//
//   conduit_gpu schedule --csv FILE [cases] [--workers 8,16,32,64,0] [--seeds 1,2,3]
//                        [--barrier GAS]
//       Schedule quality: order-preserving levels vs Jones-Plassmann batches
//       (random priorities, reordering), each executed on W simulated workers
//       (0 = unbounded) with gas as the cost model.
//
//   conduit_gpu execute [cases, typically --full] [--threads 1,2,4,8] [--gas-per-iter 200]
//                       [--exec-runs 3] [--csv FILE]
//       Runs EVERY transaction: serial, level-batched (barrier per batch) and DAG
//       dataflow executors on T threads, using the GPU-computed schedule. Each
//       parallel run must end in the serial run's state (execute.hpp). Prints
//       preprocessing times, execution speedups, gas-weighted models and the
//       end-to-end comparison (serial vs GPU preprocessing + parallel execution).
//
//   conduit_gpu stages [cases]          Phase 8: each GPU stage (extract, sort,
//       detect, CSR, levels) against the CPU stage, for every detect mapping
//   conduit_gpu pcie [--csv FILE]       Phase 8: PCIe bandwidth (pinned/pageable,
//       both directions), launch and sync latency, for the transfer arithmetic
//   conduit_gpu skew [cases] [--csv FILE]   Phase 9: hot-key statistics, per-thread
//       work imbalance and detect time of the group (before), record and
//       balanced (after) mappings; recovery = group time / mapping time
//
// cases: --full (whole-day super-blocks) | --median (default: each day's median super-block at every target)
//        | --all [--date D] [--target T] | --id ID ...
// common: [--corpus DIR] [--manifest PATH] [--policy P1|P2|P3|all]
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "conduit/corpus.hpp"
#include "conduit/execute.hpp"
#include "conduit/gpu.hpp"
#include "conduit/pipeline.hpp"
#include "conduit/schedule.hpp"

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

double median(std::vector<double> v) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    size_t k = v.size() / 2;
    return v.size() % 2 ? v[k] : (v[k - 1] + v[k]) / 2;
}

bool same(const Result& a, const Result& b) {
    return a.levels == b.levels && a.graph.cols == b.graph.cols &&
           a.graph.row_offsets == b.graph.row_offsets && a.stats.emitted == b.stats.emitted &&
           a.stats.read_comm_edges == b.stats.read_comm_edges &&
           a.stats.groups == b.stats.groups && a.records == b.records;
}

uint32_t num_batches(const std::vector<uint32_t>& lv) {
    uint32_t L = 0;
    for (uint32_t v : lv) L = std::max(L, v + 1);
    return L;
}

uint32_t sender_floor(const Tx* tx, size_t n) {
    std::vector<uint32_t> f;
    f.reserve(n);
    for (size_t i = 0; i < n; ++i)
        if (tx[i].frm != ABSENT) f.push_back(tx[i].frm);
    std::sort(f.begin(), f.end());
    uint32_t best = 0;
    for (size_t i = 0; i < f.size();) {
        size_t j = i;
        while (j < f.size() && f[j] == f[i]) ++j;
        best = std::max(best, uint32_t(j - i));
        i = j;
    }
    return best;
}

struct Case {
    const SuperBlock* sb;
    size_t n;
};

struct Args {
    std::string corpus = default_corpus_dir(), manifest, csv;
    std::vector<std::string> ids;
    bool all = false;
    std::string date;
    uint32_t target = 0;
    std::vector<Policy> policies = {Policy::P1, Policy::P2, Policy::P3};
    std::vector<int> limits = {1000, 2000, 5000};
    bool limits_set = false;
    std::vector<int> threads;
    std::vector<int> workers = {8, 16, 32, 64, 0};
    std::vector<int> seeds = {1, 2, 3};
    double barrier = 0;
    int runs = 7, warmup = 2;
    size_t brute = 2000;
    std::string configs = "quick";
    GpuBackend backend = GpuBackend::Cuda;
    bool backend_set = false;
    uint32_t tail = 8192, chunk = 32;
    size_t jp_max = 250000;      // Jones-Plassmann is O(rounds x active): skip above this
    double gas_per_iter = 200;   // execute: simulated work per tx = gas / this
    bool full_only = false;
    int exec_runs = 3;
};

bool jp_level(GpuLevel l) { return l == GpuLevel::JpIndex || l == GpuLevel::JpRandom; }

std::vector<Case> select_cases(const Manifest& m, const Args& a, bool with_limits) {
    std::vector<Case> cases;
    if (a.full_only) {   // whole-day super-blocks written by ingest.py --tx
        for (const auto& s : m.superblocks)
            if (s.id.size() > 5 && s.id.compare(s.id.size() - 5, 5, "/full") == 0)
                cases.push_back({&s, s.tx_count});
        if (cases.empty()) throw std::runtime_error("--full: manifest has no <date>/full entries");
        return cases;
    }
    if (!a.ids.empty()) {
        for (const auto& id : a.ids) {
            const SuperBlock& sb = m.get(id);
            cases.push_back({&sb, sb.tx_count});
        }
        return cases;
    }
    if (a.all) {
        for (const auto& s : m.superblocks)
            if ((a.date.empty() || s.date == a.date) && (!a.target || s.target == a.target))
                cases.push_back({&s, s.tx_count});
        return cases;
    }
    for (const auto& date : m.dates()) {
        if (!a.date.empty() && date != a.date) continue;
        for (uint32_t t : m.targets) {
            if (a.target && t != a.target) continue;
            auto es = m.entries(date, t);
            if (es.empty()) continue;
            const SuperBlock* med = es[es.size() / 2];
            if (with_limits && t == m.targets.front())
                for (int lim : a.limits)
                    if (size_t(lim) < med->tx_count) cases.push_back({med, size_t(lim)});
            cases.push_back({med, med->tx_count});
        }
    }
    return cases;
}

std::string label(const Case& c) {
    std::ostringstream s;
    s << c.sb->id;
    if (c.n != c.sb->tx_count) s << "[:" << c.n << "]";
    return s.str();
}

// ---- verify --------------------------------------------------------------------

int run_verify(const Corpus& corpus, const Manifest& m, const Selectors& sels, const Args& a) {
    const std::vector<Case> cases = select_cases(m, a, false);
    uint64_t checks = 0, failures = 0;
    auto fail = [&](const std::string& what) {
        ++failures;
        std::cerr << "FAIL " << what << "\n";
    };
    std::printf("%-28s %4s %8s %9s %9s %7s %7s %7s %7s  %s\n", "super-block", "pol", "n",
                "records", "edges", "levels", "jp-rnd", "bound", "floor", "checks");
    for (const Case& c : cases) {
        const Tx* tx = corpus.tx() + c.sb->tx_start;
        for (Policy p : a.policies) {
            const Result ref = run_pipeline(tx, c.n, p, sels);
            const AccessSets sets = infer(tx, c.n, p, sels);
            const std::string tag = label(c) + " " + policy_name(p);
            uint64_t before = checks;

            // schedule checks on the reference levels
            ++checks;
            if (!check_order(ref.graph, ref.levels)) fail(tag + " levels violate an edge");
            ++checks;
            const BatchCheck lc = check_batches(sets, ref.levels);
            if (!lc.ok) fail(tag + " levels not conflict-free: " + lc.first);
            if (a.brute) {
                const size_t k = std::min(a.brute, c.n);
                const AccessSets small = infer(tx, k, p, sels);
                const Result rs = run_pipeline(tx, k, p, sels);
                ++checks;
                if (brute_levels(small) != rs.levels) fail(tag + " brute-force oracle disagrees");
            }

            // deterministic GPU configurations must equal seq-std
            for (DetectMode d : {DetectMode::Group, DetectMode::Record, DetectMode::Balanced})
                for (GpuLevel l : {GpuLevel::Sweep, GpuLevel::Kahn, GpuLevel::JpIndex})
                    for (uint32_t T : {0u, 8192u}) {
                        if (d != DetectMode::Balanced && l != GpuLevel::Sweep) continue;
                        if (jp_level(l) && c.n > a.jp_max) continue;
                        if (T == 0 && c.n > a.jp_max) continue;
                        GpuOptions o;
                        o.backend = a.backend;
                        o.detect = d;
                        o.level = l;
                        o.tail_threshold = T;
                        const Result r = run_pipeline_gpu(tx, c.n, p, sels, o);
                        ++checks;
                        if (!same(ref, r))
                            fail(tag + " " + gpu_backend_name(a.backend) + " " + detect_name(d) +
                                 " " + gpu_level_name(l) + " T=" + std::to_string(T));
                    }

            if (c.n > a.jp_max) {
                std::printf("%-28s %4s %8zu %9zu %9llu %7u %7s %7u %7u  %llu %s\n",
                            label(c).c_str(), policy_name(p), c.n, ref.records,
                            (unsigned long long)ref.graph.num_edges(), num_batches(ref.levels),
                            "skip", clique_bound(sets), sender_floor(tx, c.n),
                            (unsigned long long)(checks - before),
                            failures ? "(failures)" : "ok");
                std::fflush(stdout);
                continue;
            }
            // jp-random: conflict-free, threshold- and backend-independent
            GpuOptions o;
            o.backend = a.backend;
            o.level = GpuLevel::JpRandom;
            o.tail_threshold = 8192;
            const Result jr = run_pipeline_gpu(tx, c.n, p, sels, o);
            o.tail_threshold = 0;
            const Result jr0 = run_pipeline_gpu(tx, c.n, p, sels, o);
            ++checks;
            if (jr.levels != jr0.levels) fail(tag + " jp-random depends on the tail threshold");
            ++checks;
            const BatchCheck bc = check_batches(sets, jr.levels);
            if (!bc.ok) fail(tag + " jp-random batch conflict: " + bc.first);
            if (a.backend == GpuBackend::Cuda) {
                o.backend = GpuBackend::Emu;
                o.tail_threshold = 8192;
                const Result je = run_pipeline_gpu(tx, c.n, p, sels, o);
                ++checks;
                if (je.levels != jr.levels) fail(tag + " jp-random cuda != emu");
            }
            const uint32_t bound = clique_bound(sets);
            ++checks;
            if (num_batches(ref.levels) < bound || num_batches(jr.levels) < bound)
                fail(tag + " a schedule beats the clique lower bound (impossible)");
            std::printf("%-28s %4s %8zu %9zu %9llu %7u %7u %7u %7u  %llu %s\n", label(c).c_str(),
                        policy_name(p), c.n, ref.records,
                        (unsigned long long)ref.graph.num_edges(), num_batches(ref.levels),
                        num_batches(jr.levels), bound, sender_floor(tx, c.n),
                        (unsigned long long)(checks - before), failures ? "(failures)" : "ok");
            std::fflush(stdout);
        }
    }
    std::printf("\n%llu checks, %llu failures\n%s\n", (unsigned long long)checks,
                (unsigned long long)failures, failures ? "GPU OUTPUT DIFFERS" : "ALL GPU CHECKS PASSED");
    return failures ? 1 : 0;
}

// ---- bench ----------------------------------------------------------------------

struct Config {
    bool gpu = false;
    Options cpu;
    GpuOptions g;
    std::string impl_label() const {
        return gpu ? std::string(gpu_backend_name(g.backend)) : std::string(impl_name(cpu.impl));
    }
};

int run_bench(const Corpus& corpus, const Manifest& m, const Selectors& sels, const Args& a) {
    if (a.csv.empty()) throw std::runtime_error("bench needs --csv FILE");
    if (a.backend == GpuBackend::Emu)
        std::cerr << "warning: GPU configurations run on the host emulation; their times are "
                     "NOT GPU times\n";
    const std::vector<Case> cases = select_cases(m, a, true);
    std::vector<int> threads = a.threads;
#ifdef CONDUIT_HAVE_OPENMP
    if (threads.empty()) {
        const int P = omp_get_num_procs();
        threads = {1};
        for (int t = 2; t < P; t *= 2) threads.push_back(t);
        if (P > 1) threads.push_back(P);
    }
#else
    threads.clear();
#endif

    std::vector<Config> grid;
    {
        Config c;
        c.cpu.impl = Impl::SeqStd;
        grid.push_back(c);
        c.cpu.impl = Impl::SeqRadix;
        grid.push_back(c);
        if (have_openmp())
            for (int t : threads) {
                Config o;
                o.cpu.impl = Impl::Omp;
                o.cpu.threads = t;
                o.cpu.level = LevelAlgo::Sweep;   // hybrid: parallel 1-3, sweep
                grid.push_back(o);
            }
        std::vector<std::pair<DetectMode, GpuLevel>> gv;
        if (a.configs == "full") {
            for (DetectMode d : {DetectMode::Group, DetectMode::Record, DetectMode::Balanced})
                for (GpuLevel l : {GpuLevel::Sweep, GpuLevel::Kahn, GpuLevel::JpIndex,
                                   GpuLevel::JpRandom})
                    gv.push_back({d, l});
        } else {
            gv = {{DetectMode::Group, GpuLevel::Sweep},
                  {DetectMode::Record, GpuLevel::Sweep},
                  {DetectMode::Balanced, GpuLevel::Sweep},
                  {DetectMode::Balanced, GpuLevel::Kahn},
                  {DetectMode::Balanced, GpuLevel::JpIndex},
                  {DetectMode::Balanced, GpuLevel::JpRandom}};
        }
        for (auto [d, l] : gv) {
            Config g;
            g.gpu = true;
            g.g.backend = a.backend;
            g.g.detect = d;
            g.g.level = l;
            g.g.tail_threshold = a.tail;
            g.g.chunk = a.chunk;
            grid.push_back(g);
        }
    }

    std::FILE* csv = std::fopen(a.csv.c_str(), "wb");
    if (!csv) throw std::runtime_error("cannot open " + a.csv);
    std::fputs("id,date,target,n,policy,impl,threads,detect,level,records,edges,batches,"
               "max_group,launches,host_syncs,rounds,tail_rounds,device_mb,t_h2d_ms,"
               "t_extract_ms,t_sort_ms,t_detect_ms,t_csr_ms,t_d2h_ms,t_level_ms,"
               "t_stages13_ms,t_e2e_ms,t_wall_ms,t_host_prep_ms,t_host_post_ms,runs,warmup\n",
               csv);

    for (const Case& cs : cases) {
        const Tx* tx = corpus.tx() + cs.sb->tx_start;
        for (Policy p : a.policies) {
            const Result ref = run_pipeline(tx, cs.n, p, sels);
            const AccessSets sets = infer(tx, cs.n, p, sels);
            for (const Config& cf : grid) {
                if (cf.gpu && jp_level(cf.g.level) && cs.n > a.jp_max) continue;
                const bool big = cs.n > 250000;
                const int runs = big ? std::min(a.runs, 3) : a.runs;
                const int warmup = big ? std::min(a.warmup, 1) : a.warmup;
                // correctness gate
                if (cf.gpu) {
                    const Result r = run_pipeline_gpu(tx, cs.n, p, sels, cf.g);
                    const bool ok = cf.g.level == GpuLevel::JpRandom
                                        ? check_batches(sets, r.levels).ok
                                        : same(ref, r);
                    if (!ok)
                        throw std::runtime_error("GPU output differs from seq-std on " +
                                                 label(cs) + " " + policy_name(p) + " " +
                                                 detect_name(cf.g.detect) + " " +
                                                 gpu_level_name(cf.g.level) +
                                                 ": refusing to time it");
                } else if (!same(ref, run_pipeline(tx, cs.n, p, sels, cf.cpu))) {
                    throw std::runtime_error(std::string("CPU output differs: ") +
                                             impl_name(cf.cpu.impl));
                }
                std::vector<double> th, te, ts, td, tc, tdd, tl, t13, tee, tw, thp, thq;
                Result last;
                GpuTimings lg;
                for (int r = 0; r < warmup + runs; ++r) {
                    Result res;
                    if (cf.gpu) {
                        GpuTimings g;
                        res = run_pipeline_gpu(tx, cs.n, p, sels, cf.g, &g);
                        if (r >= warmup) {
                            th.push_back(g.h2d * 1e3);
                            te.push_back(g.extract * 1e3);
                            ts.push_back(g.sort * 1e3);
                            td.push_back(g.detect * 1e3);
                            tc.push_back(g.csr * 1e3);
                            tdd.push_back(g.d2h * 1e3);
                            tl.push_back(g.level * 1e3);
                            t13.push_back((g.extract + g.sort + g.detect + g.csr) * 1e3);
                            tee.push_back(g.with_transfer() * 1e3);
                            tw.push_back(g.wall * 1e3);
                            thp.push_back(g.host_prep * 1e3);
                            thq.push_back(g.host_post * 1e3);
                        }
                        lg = g;
                    } else {
                        Timings t;
                        const auto w0 = std::chrono::steady_clock::now();
                        res = run_pipeline(tx, cs.n, p, sels, cf.cpu, &t);
                        const double wall = std::chrono::duration<double>(
                                                std::chrono::steady_clock::now() - w0)
                                                .count();
                        if (r >= warmup) {
                            th.push_back(0);
                            te.push_back(t.extract * 1e3);
                            ts.push_back(t.sort * 1e3);
                            td.push_back(t.detect * 1e3);
                            tc.push_back(t.csr * 1e3);
                            tdd.push_back(0);
                            tl.push_back(t.level * 1e3);
                            t13.push_back((t.extract + t.sort + t.detect + t.csr) * 1e3);
                            tee.push_back(t.total() * 1e3);
                            tw.push_back(wall * 1e3);
                            thp.push_back(0);
                            thq.push_back(0);
                        }
                    }
                    if (r == warmup + runs - 1) last = std::move(res);
                }
                std::fprintf(
                    csv,
                    "%s,%s,%u,%zu,%s,%s,%d,%s,%s,%zu,%llu,%u,%u,%llu,%llu,%llu,%llu,%.2f,%.4f,"
                    "%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%d,%d\n",
                    cs.sb->id.c_str(), cs.sb->date.c_str(), cs.sb->target, cs.n, policy_name(p),
                    cf.impl_label().c_str(),
                    cf.gpu ? 0 : (cf.cpu.impl == Impl::Omp ? cf.cpu.threads : 1),
                    cf.gpu ? detect_name(cf.g.detect) : "-",
                    cf.gpu ? gpu_level_name(cf.g.level) : level_name(cf.cpu.level), last.records,
                    (unsigned long long)last.graph.num_edges(), num_batches(last.levels),
                    cf.gpu ? lg.max_group : 0, (unsigned long long)(cf.gpu ? lg.launches : 0),
                    (unsigned long long)(cf.gpu ? lg.host_syncs : 0),
                    (unsigned long long)(cf.gpu ? lg.level_rounds : 0),
                    (unsigned long long)(cf.gpu ? lg.tail_rounds : 0),
                    cf.gpu ? double(lg.device_bytes) / (1 << 20) : 0.0, median(th), median(te),
                    median(ts), median(td), median(tc), median(tdd), median(tl), median(t13),
                    median(tee), median(tw), median(thp), median(thq), runs, warmup);
                std::fflush(csv);
            }
            std::cout << "  " << label(cs) << " " << policy_name(p) << ": " << grid.size()
                      << " configurations\n"
                      << std::flush;
        }
    }
    std::fclose(csv);
    std::cout << "wrote " << a.csv << "\n";
    return 0;
}

// ---- schedule ------------------------------------------------------------------------

int run_schedule(const Corpus& corpus, const Manifest& m, const Selectors& sels, const Args& a) {
    if (a.csv.empty()) throw std::runtime_error("schedule needs --csv FILE");
    const std::vector<Case> cases = select_cases(m, a, false);
    std::FILE* csv = std::fopen(a.csv.c_str(), "wb");
    if (!csv) throw std::runtime_error("cannot open " + a.csv);
    std::fputs("id,date,target,n,policy,method,seed,order_preserving,batches,bound,floor,parallelism,"
               "batch_p50,batches_lt32_pct,workers,sim_speedup,sim_utilisation\n",
               csv);
    std::printf("%-28s %4s %-10s %7s %7s %7s %8s", "super-block", "pol", "method", "batches",
                "bound", "floor", "tx/batch");
    for (int w : a.workers) std::printf("  W=%-4s", w ? std::to_string(w).c_str() : "inf");
    std::printf("\n");
    for (const Case& cs : cases) {
        const Tx* tx = corpus.tx() + cs.sb->tx_start;
        const std::vector<double> cost = gas_costs(tx, cs.n);
        const uint32_t floor = sender_floor(tx, cs.n);
        for (Policy p : a.policies) {
            const AccessSets sets = infer(tx, cs.n, p, sels);
            const uint32_t bound = clique_bound(sets);
            struct Sched {
                std::string method;
                int seed;
                std::vector<uint32_t> batch;
            };
            std::vector<Sched> sch;
            sch.push_back({"levels", 0, run_pipeline(tx, cs.n, p, sels).levels});
            for (int sd : a.seeds) {
                if (cs.n > a.jp_max) break;
                GpuOptions o;
                o.backend = a.backend;
                o.level = GpuLevel::JpRandom;
                o.jp_seed = uint32_t(sd) * 0x9E3779B9u;
                sch.push_back({"jp-random", sd, run_pipeline_gpu(tx, cs.n, p, sels, o).levels});
            }
            for (const Sched& s : sch) {
                if (!check_batches(sets, s.batch).ok)
                    throw std::runtime_error("schedule " + s.method + " has a conflict");
                const uint32_t B = num_batches(s.batch);
                std::vector<uint32_t> sizes(B, 0);
                for (uint32_t b : s.batch) ++sizes[b];
                std::vector<uint32_t> ss = sizes;
                std::sort(ss.begin(), ss.end());
                size_t small = 0;
                for (uint32_t z : sizes) small += z < 32;
                std::printf("%-28s %4s %-10s %7u %7u %7u %8.1f", label(cs).c_str(),
                            policy_name(p),
                            (s.method + (s.seed ? std::to_string(s.seed) : "")).c_str(), B, bound,
                            floor, double(cs.n) / B);
                for (int w : a.workers) {
                    const ExecSim es = simulate(s.batch, cost, uint32_t(w), a.barrier);
                    std::printf("  %6.2f", es.speedup);
                    std::fprintf(csv,
                                 "%s,%s,%u,%zu,%s,%s,%d,%d,%u,%u,%u,%.3f,%u,%.2f,%d,%.4f,%.4f\n",
                                 cs.sb->id.c_str(), cs.sb->date.c_str(), cs.sb->target, cs.n,
                                 policy_name(p), s.method.c_str(), s.seed,
                                 s.method == "levels" ? 1 : 0, B, bound, floor, double(cs.n) / B,
                                 ss[ss.size() / 2], 100.0 * small / B, w, es.speedup,
                                 es.utilisation);
                }
                std::printf("\n");
                std::fflush(stdout);
            }
        }
    }
    std::fclose(csv);
    std::cout << "wrote " << a.csv << "  (speedup = serial gas / simulated makespan)\n";
    return 0;
}


// ---- key labels (skew / trace) ----------------------------------------------------

struct AddrBook {
    std::vector<std::string> addr;
    explicit AddrBook(const std::string& corpus_dir) {
        std::FILE* f = std::fopen((corpus_dir + "/addresses.txt").c_str(), "rb");
        if (!f) return;
        char buf[256];
        while (std::fgets(buf, sizeof buf, f)) {
            std::string s(buf);
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            addr.push_back(s);
        }
        std::fclose(f);
    }
    std::string name(uint64_t id) const {
        if (id < addr.size() && addr[id].size() >= 8)
            return "0x" + addr[id].substr(0, 4) + ".." + addr[id].substr(addr[id].size() - 4);
        return "#" + std::to_string(id);
    }
};

std::string key_label(uint64_t key, const AddrBook& ab) {
    const uint64_t M = (1ull << 21) - 1;
    if (key >> 63)
        return "ALLOWANCE(" + ab.name((key >> 42) & M) + "," + ab.name((key >> 21) & M) + "," +
               ab.name(key & M) + ")";
    const uint64_t kind = (key >> 60) & 7, x = (key >> 21) & M, y = key & M;
    switch (kind) {
        case 0: return "ACCOUNT(" + ab.name(x) + ")";
        case 1: return "NONCE(" + ab.name(x) + ")";
        case 2: return "ETH_BAL(" + ab.name(x) + ")";
        case 3: return "TOKEN_BAL(" + ab.name(x) + "," + ab.name(y) + ")";
    }
    return "KEY";
}

// ---- stages (Phase 8: the port verified stage by stage) -----------------------------

int run_stages(const Corpus& corpus, const Manifest& m, const Selectors& sels, const Args& a) {
    const std::vector<Case> cases = select_cases(m, a, false);
    uint64_t checks = 0, failures = 0;
    std::printf("%-28s %4s %-9s %8s  %-8s %-8s %-9s %-8s %-8s\n", "super-block", "pol", "detect",
                "n", "1-extract", "2a-sort", "2b-detect", "3-csr", "4-levels");
    for (const Case& c : cases) {
        const Tx* tx = corpus.tx() + c.sb->tx_start;
        for (Policy p : a.policies) {
            std::vector<Record> rec0 = extract(tx, c.n, p, sels);
            std::vector<Record> sorted = rec0;
            sort_records(sorted);
            EdgeStats st;
            std::vector<uint64_t> e = detect(sorted, &st);
            std::vector<uint64_t> e_sorted = e;
            std::sort(e_sorted.begin(), e_sorted.end());
            std::vector<uint64_t> tmp = e;
            const Graph g = build_csr(uint32_t(c.n), tmp);
            const std::vector<uint32_t> lv = level(g);
            for (DetectMode d : {DetectMode::Group, DetectMode::Record, DetectMode::Balanced}) {
                GpuCapture cap;
                GpuOptions o;
                o.backend = a.backend;
                o.detect = d;
                o.capture = &cap;
                const Result r = run_pipeline_gpu(tx, c.n, p, sels, o);
                std::vector<uint64_t> ce = cap.edges;
                std::sort(ce.begin(), ce.end());
                const bool s1 = cap.extracted == rec0, s2 = cap.sorted == sorted,
                           s3 = ce == e_sorted && r.stats.emitted == st.emitted,
                           s4 = r.graph.cols == g.cols && r.graph.row_offsets == g.row_offsets,
                           s5 = r.levels == lv;
                checks += 5;
                failures += !s1 + !s2 + !s3 + !s4 + !s5;
                auto f = [](bool b) { return b ? "PASS" : "FAIL"; };
                std::printf("%-28s %4s %-9s %8zu  %-8s %-8s %-9s %-8s %-8s\n", label(c).c_str(),
                            policy_name(p), detect_name(d), c.n, f(s1), f(s2), f(s3), f(s4), f(s5));
                std::fflush(stdout);
            }
        }
    }
    std::printf("\n%llu stage checks, %llu failures\n%s\n", (unsigned long long)checks,
                (unsigned long long)failures,
                failures ? "STAGE OUTPUT DIFFERS" : "ALL STAGES IDENTICAL TO THE CPU REFERENCE");
    return failures ? 1 : 0;
}

// ---- pcie (Phase 8: transfer-cost arithmetic) ---------------------------------------

int run_pcie(const Args& a) {
    if (a.backend != GpuBackend::Cuda || !have_cuda())
        throw std::runtime_error("pcie needs a CUDA device");
    std::vector<size_t> sizes;
    for (size_t b = 4096; b <= (size_t(256) << 20); b *= 4) sizes.push_back(b);
    const PcieProbe pr = pcie_probe(sizes, std::max(5, a.runs));
    std::printf("%12s %12s %12s %12s %12s   (GB/s, median of %d)\n", "bytes", "H2D pinned",
                "D2H pinned", "H2D pageable", "D2H pageable", std::max(5, a.runs));
    std::FILE* csv = a.csv.empty() ? nullptr : std::fopen(a.csv.c_str(), "wb");
    if (csv)
        std::fputs("bytes,h2d_pinned_s,d2h_pinned_s,h2d_pageable_s,d2h_pageable_s,launch_s,sync_s\n",
                   csv);
    for (const PcieSample& s : pr.samples) {
        auto gbs = [&](double t) { return t > 0 ? double(s.bytes) / t / 1e9 : 0.0; };
        std::printf("%12zu %12.2f %12.2f %12.2f %12.2f\n", s.bytes, gbs(s.h2d_pinned),
                    gbs(s.d2h_pinned), gbs(s.h2d_pageable), gbs(s.d2h_pageable));
        if (csv)
            std::fprintf(csv, "%zu,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f\n", s.bytes, s.h2d_pinned,
                         s.d2h_pinned, s.h2d_pageable, s.d2h_pageable, pr.launch, pr.sync);
    }
    std::printf("empty kernel launch + sync: %.1f us; one device->host read (sync round trip): "
                "%.1f us\n",
                pr.launch * 1e6, pr.sync * 1e6);
    if (csv) {
        std::fclose(csv);
        std::printf("wrote %s\n", a.csv.c_str());
    }
    return 0;
}

// ---- skew (Phase 9: before-number, rebalance, recovery) -------------------------------

int run_skew(const Corpus& corpus, const Manifest& m, const Selectors& sels, const Args& a) {
    const std::vector<Case> cases = select_cases(m, a, false);
    const AddrBook ab(a.corpus);
    std::FILE* csv = a.csv.empty() ? nullptr : std::fopen(a.csv.c_str(), "wb");
    if (csv)
        std::fputs("id,date,n,policy,records,groups,max_group,max_group_pct,p99_group,"
                   "groups_ge_1024,hot_key,edges,imb_group,imb_record,imb_edge,detect_group_ms,"
                   "detect_record_ms,detect_balanced_ms,recovery_record,recovery_balanced,backend,"
                   "runs\n",
                   csv);
    std::printf("%-26s %4s %9s %8s %7s %-30s %7s %7s %9s %9s %9s %7s %7s\n", "super-block", "pol",
                "records", "maxgrp", "%rec", "hottest key", "imb-grp", "imb-rec", "grp ms",
                "rec ms", "edge ms", "rec x", "edge x");
    for (const Case& c : cases) {
        const Tx* tx = corpus.tx() + c.sb->tx_start;
        for (Policy p : a.policies) {
            std::vector<Record> r = extract(tx, c.n, p, sels);
            sort_records(r);
            // groups, runs, per-thread work of each mapping
            std::vector<uint32_t> gsize;
            std::vector<double> wg, wr;   // per-thread work: group walk / record in-edges
            uint64_t E = 0, hot_key = 0;
            uint32_t hot = 0;
            for (size_t i = 0; i < r.size();) {
                size_t j = i;
                while (j < r.size() && rec_key(r[j]) == rec_key(r[i])) ++j;
                const uint32_t sz = uint32_t(j - i);
                gsize.push_back(sz);
                if (sz > hot) { hot = sz; hot_key = rec_key(r[i]); }
                uint64_t ge = 0, prev = 0;
                for (size_t q = i; q < j;) {
                    const uint8_t mode = rec_mode(r[q]);
                    size_t e = q + 1;
                    if (mode != WRITE)
                        while (e < j && rec_mode(r[e]) == mode) ++e;
                    for (size_t k = q; k < e; ++k) wr.push_back(1.0 + double(prev));
                    ge += prev * (e - q);
                    prev = e - q;
                    q = e;
                }
                wg.push_back(double(sz) + double(ge));
                E += ge;
                i = j;
            }
            auto imb = [](const std::vector<double>& w) {
                if (w.empty()) return 0.0;
                double mx = 0, s = 0;
                for (double x : w) { mx = std::max(mx, x); s += x; }
                return mx / (s / double(w.size()));
            };
            std::vector<uint32_t> gs = gsize;
            std::sort(gs.begin(), gs.end());
            const uint32_t p99 = gs.empty() ? 0 : gs[size_t(0.99 * double(gs.size() - 1))];
            size_t big = 0;
            for (uint32_t z : gs) big += z >= 1024;
            // GPU detect time of each mapping, before-number first
            double td[3] = {0, 0, 0};
            int k = 0;
            for (DetectMode d : {DetectMode::Group, DetectMode::Record, DetectMode::Balanced}) {
                GpuOptions o;
                o.backend = a.backend;
                o.detect = d;
                std::vector<double> v;
                for (int rr = 0; rr < a.warmup + a.runs; ++rr) {
                    GpuTimings g;
                    run_pipeline_gpu(tx, c.n, p, sels, o, &g);
                    if (rr >= a.warmup) v.push_back(g.detect * 1e3);
                }
                td[k++] = median(v);
            }
            const std::string hk = key_label(hot_key, ab);
            std::printf("%-26s %4s %9zu %8u %6.1f%% %-30s %7.0f %7.0f %9.3f %9.3f %9.3f %6.1fx "
                        "%6.1fx\n",
                        label(c).c_str(), policy_name(p), r.size(), hot,
                        100.0 * hot / std::max<size_t>(1, r.size()), hk.substr(0, 30).c_str(),
                        imb(wg), imb(wr), td[0], td[1], td[2], td[0] / td[1], td[0] / td[2]);
            std::fflush(stdout);
            if (csv)
                std::fprintf(csv,
                             "%s,%s,%zu,%s,%zu,%zu,%u,%.3f,%u,%zu,%s,%llu,%.2f,%.2f,1.00,%.4f,"
                             "%.4f,%.4f,%.3f,%.3f,%s,%d\n",
                             c.sb->id.c_str(), c.sb->date.c_str(), c.n, policy_name(p), r.size(),
                             gsize.size(), hot, 100.0 * hot / std::max<size_t>(1, r.size()), p99,
                             big, hk.c_str(), (unsigned long long)E, imb(wg), imb(wr), td[0],
                             td[1], td[2], td[0] / td[1], td[0] / td[2],
                             gpu_backend_name(a.backend), a.runs);
        }
    }
    if (csv) {
        std::fclose(csv);
        std::printf("wrote %s\n", a.csv.c_str());
    }
    std::printf("imb = max / mean work per GPU thread (1.0 = perfectly even); x = detect-stage "
                "speedup over the group mapping (the before-number)\n");
    return 0;
}

// ---- execute ------------------------------------------------------------------------

std::vector<int> default_threads(const Args& a) {
    if (!a.threads.empty()) return a.threads;
    std::vector<int> t = {1};
#ifdef CONDUIT_HAVE_OPENMP
    const int P = omp_get_num_procs();
    for (int k = 2; k < P; k *= 2) t.push_back(k);
    if (P > 1) t.push_back(P);
#endif
    return t;
}

int run_execute(const Corpus& corpus, const Manifest& m, const Selectors& sels, const Args& a) {
    const std::vector<Case> cases = select_cases(m, a, false);
    const std::vector<int> threads = default_threads(a);
    std::FILE* csv = nullptr;
    if (!a.csv.empty()) {
        csv = std::fopen(a.csv.c_str(), "wb");
        if (!csv) throw std::runtime_error("cannot open " + a.csv);
        std::fputs("id,date,n,policy,kind,name,threads,workers,seconds,speedup,executed,"
                   "state_ok,batches\n",
                   csv);
    }
    auto med = [&](auto fn) {
        std::vector<double> v;
        ExecResult last;
        for (int r = 0; r < std::max(1, a.exec_runs); ++r) {
            last = fn();
            v.push_back(last.seconds);
        }
        last.seconds = median(v);
        return last;
    };
    for (const Case& cs : cases) {
        const Tx* tx = corpus.tx() + cs.sb->tx_start;
        for (Policy p : a.policies) {
            std::printf("\n=== %s  policy %s  (%zu transactions) ===\n", label(cs).c_str(),
                        policy_name(p), cs.n);
            // 1. preprocessing: CPU reference and GPU, identical output required
            Timings tc;
            Result ref;
            {
                std::vector<double> v;
                for (int r = 0; r < 2; ++r) {
                    ref = run_pipeline(tx, cs.n, p, sels, Options{}, &tc);
                    v.push_back(tc.total());
                }
                tc.extract = median(v);   // reuse as the median total
            }
            const double t_cpu_pre = tc.extract;
            double t_omp_pre = 0;
            if (have_openmp()) {
                Options o;
                o.impl = Impl::Omp;
                o.level = LevelAlgo::Sweep;
                o.threads = threads.back();
                Timings t;
                run_pipeline(tx, cs.n, p, sels, o, &t);
                run_pipeline(tx, cs.n, p, sels, o, &t);
                t_omp_pre = t.total();
            }
            GpuOptions go;
            go.backend = a.backend;
            go.tail_threshold = a.tail;
            go.chunk = a.chunk;
            Result r = run_pipeline_gpu(tx, cs.n, p, sels, go);   // warm-up / allocation
            std::vector<double> gv;
            for (int k = 0; k < 3; ++k) {
                GpuTimings g;
                r = run_pipeline_gpu(tx, cs.n, p, sels, go, &g);
                gv.push_back(g.wall);
            }
            const double t_gpu_pre = median(gv);
            if (!same(ref, r))
                throw std::runtime_error("GPU schedule differs from seq-std: refusing to execute");
            const uint32_t B = num_batches(r.levels);
            std::printf("preprocessing (conflicts + DAG + levels): seq-std %.1f ms",
                        t_cpu_pre * 1e3);
            if (t_omp_pre > 0) std::printf(", OpenMP(%d) %.1f ms", threads.back(), t_omp_pre * 1e3);
            std::printf(", %s %.1f ms  -> %u batches, %llu edges, identical output\n",
                        gpu_backend_name(a.backend), t_gpu_pre * 1e3, B,
                        (unsigned long long)r.graph.num_edges());
            if (csv) {
                std::fprintf(csv, "%s,%s,%zu,%s,pre,seq-std,1,0,%.6f,1,%zu,1,%u\n",
                             cs.sb->id.c_str(), cs.sb->date.c_str(), cs.n, policy_name(p),
                             t_cpu_pre, cs.n, B);
                if (t_omp_pre > 0)
                    std::fprintf(csv, "%s,%s,%zu,%s,pre,omp,%d,0,%.6f,%.4f,%zu,1,%u\n",
                                 cs.sb->id.c_str(), cs.sb->date.c_str(), cs.n, policy_name(p),
                                 threads.back(), t_omp_pre, t_cpu_pre / t_omp_pre, cs.n, B);
                std::fprintf(csv, "%s,%s,%zu,%s,pre,%s,0,0,%.6f,%.4f,%zu,1,%u\n",
                             cs.sb->id.c_str(), cs.sb->date.c_str(), cs.n, policy_name(p),
                             gpu_backend_name(a.backend), t_gpu_pre, t_cpu_pre / t_gpu_pre, cs.n,
                             B);
            }

            // 2. execution of every transaction
            const AccessSets sets = infer(tx, cs.n, p, sels);
            const ExecPlan plan = make_plan(sets, tx, a.gas_per_iter);
            const ExecResult ser = med([&] { return exec_serial(plan); });
            std::printf("%-26s %7s %10s %9s %12s  %s\n", "executor", "threads", "seconds",
                        "speedup", "executed", "final state == serial");
            std::printf("%-26s %7d %10.3f %9.2f %12llu  %s\n", "serial (block order)", 1,
                        ser.seconds, 1.0, (unsigned long long)ser.executed, "reference");
            auto row = [&](const char* name, int t, const ExecResult& e) {
                const bool ok = e.same_as(ser) && e.executed == cs.n;
                std::printf("%-26s %7d %10.3f %9.2f %12llu  %s\n", name, t, e.seconds,
                            ser.seconds / e.seconds, (unsigned long long)e.executed,
                            ok ? "yes" : "NO  <-- WRONG");
                if (csv)
                    std::fprintf(csv, "%s,%s,%zu,%s,exec,%s,%d,0,%.6f,%.4f,%llu,%d,%u\n",
                                 cs.sb->id.c_str(), cs.sb->date.c_str(), cs.n, policy_name(p),
                                 name, t, e.seconds, ser.seconds / e.seconds,
                                 (unsigned long long)e.executed, ok ? 1 : 0, B);
                if (!ok) throw std::runtime_error("parallel execution diverged from serial");
            };
            if (csv)
                std::fprintf(csv, "%s,%s,%zu,%s,exec,serial,1,0,%.6f,1,%llu,1,%u\n",
                             cs.sb->id.c_str(), cs.sb->date.c_str(), cs.n, policy_name(p),
                             ser.seconds, (unsigned long long)ser.executed, B);
            double best_dag = 1e30, best_lv = 1e30;
            for (int t : threads) {
                const ExecResult lv = med([&] { return exec_levels(plan, r.levels, t); });
                row("level batches + barriers", t, lv);
                best_lv = std::min(best_lv, lv.seconds);
            }
            for (int t : threads) {
                const ExecResult dg = med([&] { return exec_dag(plan, r.graph, t); });
                row("DAG dataflow (no barrier)", t, dg);
                best_dag = std::min(best_dag, dg.seconds);
            }

            // 3. models: what the schedule allows on W workers (gas as cost)
            const std::vector<double> cost = gas_costs(tx, cs.n);
            std::printf("model (gas-weighted), speedup over serial on W workers:\n  %-24s",
                        "W =");
            for (int w : a.workers) std::printf(" %8s", w ? std::to_string(w).c_str() : "inf");
            std::printf("\n  %-24s", "level batches");
            std::vector<double> lvs, dgs;
            for (int w : a.workers) {
                const double v = simulate(r.levels, cost, uint32_t(w)).speedup;
                lvs.push_back(v);
                std::printf(" %8.2f", v);
            }
            std::printf("\n  %-24s", "DAG dataflow");
            for (int w : a.workers) {
                const double v = simulate_dag(r.graph, cost, uint32_t(w)).speedup;
                dgs.push_back(v);
                std::printf(" %8.2f", v);
            }
            std::printf("\n");
            if (csv)
                for (size_t k = 0; k < a.workers.size(); ++k) {
                    std::fprintf(csv, "%s,%s,%zu,%s,model,levels,0,%d,0,%.4f,%zu,1,%u\n",
                                 cs.sb->id.c_str(), cs.sb->date.c_str(), cs.n, policy_name(p),
                                 a.workers[k], lvs[k], cs.n, B);
                    std::fprintf(csv, "%s,%s,%zu,%s,model,dag,0,%d,0,%.4f,%zu,1,%u\n",
                                 cs.sb->id.c_str(), cs.sb->date.c_str(), cs.n, policy_name(p),
                                 a.workers[k], dgs[k], cs.n, B);
                }

            // 4. end to end: serial execution vs (GPU preprocessing + parallel execution)
            const double e2e = t_gpu_pre + best_dag;
            std::printf("END TO END: serial execution %.3f s  vs  %s preprocessing + DAG execution "
                        "%.3f s  =  %.2fx faster (levels+barriers: %.2fx); all %zu transactions "
                        "executed, final state identical\n",
                        ser.seconds, gpu_backend_name(a.backend), e2e, ser.seconds / e2e,
                        ser.seconds / (t_gpu_pre + best_lv), cs.n);
            if (csv)
                std::fprintf(csv, "%s,%s,%zu,%s,e2e,pre+dag,%d,0,%.6f,%.4f,%zu,1,%u\n",
                             cs.sb->id.c_str(), cs.sb->date.c_str(), cs.n, policy_name(p),
                             threads.back(), e2e, ser.seconds / e2e, cs.n, B);
            std::fflush(stdout);
        }
    }
    if (csv) {
        std::fclose(csv);
        std::cout << "\nwrote " << a.csv << "\n";
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: conduit_gpu info|pcie|verify|stages|bench|skew|schedule|execute|trace [options]\n";
        return 2;
    }
    const std::string mode = argv[1];
    Args a;
    for (int i = 2; i < argc; ++i) {
        std::string s = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << s << " needs a value\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (s == "--corpus") a.corpus = next();
        else if (s == "--manifest") a.manifest = next();
        else if (s == "--csv") a.csv = next();
        else if (s == "--id") a.ids.push_back(next());
        else if (s == "--median") {}
        else if (s == "--all") a.all = true;
        else if (s == "--date") a.date = next();
        else if (s == "--target") a.target = uint32_t(std::stoul(next()));
        else if (s == "--policy") {
            std::string p = next();
            a.policies = p == "all" ? std::vector<Policy>{Policy::P1, Policy::P2, Policy::P3}
                                    : std::vector<Policy>{parse_policy(p)};
        } else if (s == "--limits") a.limits = parse_ints(next());
        else if (s == "--threads") a.threads = parse_ints(next());
        else if (s == "--workers") a.workers = parse_ints(next());
        else if (s == "--seeds") a.seeds = parse_ints(next());
        else if (s == "--barrier") a.barrier = std::stod(next());
        else if (s == "--runs") a.runs = std::stoi(next());
        else if (s == "--warmup") a.warmup = std::stoi(next());
        else if (s == "--brute") a.brute = std::stoul(next());
        else if (s == "--configs") a.configs = next();
        else if (s == "--jp-max") a.jp_max = std::stoul(next());
        else if (s == "--gas-per-iter") a.gas_per_iter = std::stod(next());
        else if (s == "--full") a.full_only = true;
        else if (s == "--exec-runs") a.exec_runs = std::stoi(next());
        else if (s == "--tail") a.tail = uint32_t(std::stoul(next()));
        else if (s == "--chunk") a.chunk = uint32_t(std::stoul(next()));
        else if (s == "--backend") {
            a.backend = parse_gpu_backend(next());
            a.backend_set = true;
        } else {
            std::cerr << "unknown argument " << s << "\n";
            return 2;
        }
    }
    try {
        if (!a.backend_set && !have_cuda()) a.backend = GpuBackend::Emu;
        std::cout << "conduit_gpu " << mode << ": GPU " << cuda_device_info() << "; backend "
                  << gpu_backend_name(a.backend) << "; OpenMP " << (have_openmp() ? "on" : "off")
                  << "\n";
        if (mode == "info") return 0;
        if (mode == "pcie") return run_pcie(a);
        Corpus corpus(a.corpus);
        Manifest m = load_manifest(resolve_manifest(a.manifest, a.corpus), corpus);
        Selectors sels(corpus.selectors);
        if (mode == "verify") return run_verify(corpus, m, sels, a);
        if (mode == "bench") return run_bench(corpus, m, sels, a);
        if (mode == "schedule") return run_schedule(corpus, m, sels, a);
        if (mode == "execute") return run_execute(corpus, m, sels, a);
        if (mode == "stages") return run_stages(corpus, m, sels, a);
        if (mode == "skew") return run_skew(corpus, m, sels, a);
        std::cerr << "unknown mode " << mode << "\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
