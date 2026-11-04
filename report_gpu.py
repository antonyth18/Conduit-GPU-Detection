#!/usr/bin/env python3
"""
Phase 8-10 report. Reads the files written by tools/run_phase8.sh or
tools/run_dataset.sh in one results directory and writes summary.md and
figures next to them.

    python3 report_gpu.py results/phase8/<host>-real-large

Conventions (handover.md, review of 2026-11-02):
  * HEADLINE = wall time (t_wall_ms): everything the caller waits for,
    including host-side staging. t_e2e_ms (H2D + kernels + D2H on the device
    timeline) is kept as the breakdown.
  * Speedups against seq-std, seq-radix and the best OpenMP run, labelled.
    Part of any gain over seq-std is the change to radix sort.
  * Transfers are charged to the GPU; the transfer arithmetic is reported.
  * The execution section is a MODEL (toy state machine, work proportional to
    gas), not a measurement of an Ethereum client.
"""
import sys
from pathlib import Path

import numpy as np

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
except ImportError:  # figures are optional
    plt = None


def load(path):
    if not path.exists() or path.stat().st_size == 0:
        return None
    a = np.genfromtxt(path, delimiter=",", names=True, dtype=None, encoding="utf-8")
    return np.atleast_1d(a)


def fmt(x, d=2):
    if x is None:
        return "–"
    try:
        if np.isnan(x) or np.isinf(x):
            return "–"
    except TypeError:
        pass
    return f"{x:.{d}f}"


def has(arr, col):
    return arr is not None and col in arr.dtype.names


def fit_line(x, y):
    """least squares y = a + b x"""
    x, y = np.asarray(x, float), np.asarray(y, float)
    if len(x) < 2:
        return None
    b, a = np.polyfit(x, y, 1)
    return a, b


def gate(out, name, pattern):
    p = out / name
    if not p.exists():
        return f"| {name} | not run | |"
    last = [ln for ln in p.read_text(errors="replace").strip().splitlines() if ln.strip()]
    tail = last[-1] if last else ""
    ok = pattern in p.read_text(errors="replace")
    return f"| {name} | {'PASS' if ok else '**FAIL**'} | {tail[:110]} |"


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    bench = load(out / "bench.csv")
    sched = load(out / "schedule.csv")
    exe = load(out / "execute.csv")
    skew = load(out / "skew.csv")
    pcie = load(out / "pcie.csv")
    md = ["# Phases 8-10 results", ""]
    mt = out / "machine.txt"
    if mt.exists():
        md += ["## Machine (non-negotiable 4)", "", "```", mt.read_text().strip()[:4000], "```", ""]

    md += ["## Correctness gates (non-negotiable 1: no timing without correctness)", "",
           "| check | result | last line |", "|---|---|---|",
           gate(out, "tests.txt", " 0 failed checks"),
           gate(out, "stages.txt", "ALL STAGES IDENTICAL"),
           gate(out, "verify.txt", "ALL GPU CHECKS PASSED"),
           gate(out, "identical.txt", "ALL IDENTICAL"),
           gate(out, "validator.txt", "ALL CHECKS PASSED"), ""]

    if bench is not None:
        gpu_impl = "cuda" if "cuda" in set(bench["impl"]) else "emu"
        wall = "t_wall_ms"
        if gpu_impl == "emu":
            md += ["> **WARNING: GPU rows come from the host EMULATION. Their times are not GPU "
                   "times; only the CPU rows and the counters are meaningful.**", ""]
        if has(bench, "runs"):
            rs = sorted(set(int(x) for x in bench["runs"]))
            ws = sorted(set(int(x) for x in bench["warmup"]))
            md += [f"Repetitions: warm-up {ws}, measured {rs} (medians; super-blocks above 250k "
                   "use at most 1 + 3). " + ("" if max(rs) >= 7 else
                                             "**Below the 2 + 7 standard: smoke test only.**"), ""]

        def rows(**kw):
            m = np.ones(len(bench), bool)
            for k, v in kw.items():
                m &= bench[k] == v
            return bench[m]

        def t(r, col=wall):
            return float(r[col][0]) if len(r) else float("nan")

        keys = []
        for r in bench:
            k = (str(r["date"]), int(r["n"]), str(r["policy"]))
            if k not in keys:
                keys.append(k)
        keys.sort(key=lambda k: (k[0], k[2], k[1]))

        md += ["## Phase 8: end to end, wall time (ms, median)", "",
               "GPU = balanced detection + sequential sweep for levelling (as the handover "
               "asks), transfers and host staging included. `device` = H2D + kernels + D2H on "
               "the GPU timeline; `kernels` = stages 1-3 kernels only.", "",
               "| day | n | pol | seq-std | seq-radix | OpenMP best (threads) | GPU wall | GPU device | "
               "GPU kernels | vs seq-std | vs seq-radix | vs OpenMP |",
               "|---|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
        cross = {}
        for (d, n, p) in keys:
            base = dict(date=d, n=n, policy=p)
            ss = t(rows(impl="seq-std", **base))
            sr = t(rows(impl="seq-radix", **base))
            om = rows(impl="omp", **base)
            if len(om):
                i = int(np.argmin(om[wall]))
                ob, obt = float(om[wall][i]), int(om["threads"][i])
            else:
                ob, obt = float("nan"), 0
            g = rows(impl=gpu_impl, detect="balanced", level="sweep", **base)
            gw, ge, gk = t(g), t(g, "t_e2e_ms"), t(g, "t_stages13_ms")
            md.append(f"| {d} | {n:,} | {p} | {fmt(ss)} | {fmt(sr)} | {fmt(ob)} ({obt}) | "
                      f"{fmt(gw)} | {fmt(ge)} | {fmt(gk)} | {fmt(ss / gw)}x | {fmt(sr / gw)}x | "
                      f"{fmt(ob / gw) if obt else '–'}x |")
            cross.setdefault((d, p), []).append((n, gw, sr, ob, ss))

        md += ["", "### Crossover (wall time, transfers charged)", "",
               "Measured = smallest tested size where the GPU is faster. Model = least-squares "
               "lines t = a + b·n through each implementation's points at n ≤ 20k (the region "
               "around the crossover); the model crossover is where the lines meet, "
               "n* = (a_gpu − a_cpu) / (b_cpu − b_gpu). a_gpu is the GPU's fixed cost per call "
               "(launches, synchronisations, transfer latency), b the cost per transaction.", "",
               "| day | pol | beats seq-radix at n ≥ (measured) | model n* vs seq-radix | "
               "beats OpenMP at n ≥ (measured) | model n* vs OpenMP | GPU fixed cost a (ms) |",
               "|---|---|---:|---:|---:|---:|---:|"]
        for (d, p), v in sorted(cross.items()):
            v.sort()
            small = [x for x in v if x[0] <= 20000]
            fitv = small if len(small) >= 3 else v
            ns = [x[0] for x in fitv]
            a1 = next((n for n, gw, sr, ob, ss in v if gw < sr), None)
            a2 = next((n for n, gw, sr, ob, ss in v if gw < ob), None)
            fg = fit_line(ns, [x[1] for x in fitv])
            fr = fit_line(ns, [x[2] for x in fitv])
            fo = fit_line(ns, [x[3] for x in fitv])

            def nstar(fc):
                if not fg or not fc or fc[1] <= fg[1]:
                    return "never"
                n0 = (fg[0] - fc[0]) / (fc[1] - fg[1])
                return f"{max(n0, 0):,.0f}"
            md.append(f"| {d} | {p} | {a1 if a1 else 'never'} | {nstar(fr)} | "
                      f"{a2 if a2 else 'never'} | {nstar(fo)} | {fmt(fg[0] if fg else None, 3)} |")

        # transfer arithmetic
        nmax = {}
        for (d, n, p) in keys:
            nmax[(d, p)] = max(nmax.get((d, p), 0), n)
        md += ["", "### Transfer-cost arithmetic (largest super-block per day and policy)", "",
               "Bytes moved: H2D = 40·n (the transaction records); D2H = 8·(n+1) row offsets "
               "+ 4·m column indices (+ 4·n levels when levelling on the GPU). Measured times "
               "are cudaEvent intervals; the share is of GPU wall time. Host prep = id check + "
               "copy into the pinned staging buffer; host post = copy out of the pinned buffers.",
               ""]
        bw = {}
        if pcie is not None and len(pcie) >= 3:
            for k in ("h2d_pinned_s", "d2h_pinned_s"):
                big = pcie["bytes"] >= (1 << 20)
                f = fit_line(pcie["bytes"][big], pcie[k][big])
                if f:
                    bw[k] = f
            md += ["PCIe probe (pinned, fitted t = latency + bytes / bandwidth on transfers ≥ 1 MB): " +
                   ", ".join(f"{k.split('_')[0].upper()} {1e-9 / f[1]:.1f} GB/s, latency "
                             f"{f[0] * 1e6:.1f} µs" for k, f in bw.items()) +
                   (f"; empty-kernel launch + sync {float(pcie['launch_s'][0]) * 1e6:.1f} µs, one "
                    f"host round trip {float(pcie['sync_s'][0]) * 1e6:.1f} µs." if has(pcie, "launch_s") else ""),
                   ""]
        md += ["| day | pol | n | H2D MB | D2H MB | H2D ms (pred.) | D2H ms (pred.) | transfer % of wall | "
               "host prep ms | host post ms | wall − device ms |",
               "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
        for (d, p), n in sorted(nmax.items()):
            g = rows(impl=gpu_impl, detect="balanced", level="sweep", date=d, n=n, policy=p)
            if not len(g):
                continue
            r = g[0]
            h2d_b = 40.0 * n
            d2h_b = 8.0 * (n + 1) + 4.0 * float(r["edges"])
            ph = (bw["h2d_pinned_s"][0] + h2d_b * bw["h2d_pinned_s"][1]) * 1e3 if "h2d_pinned_s" in bw else None
            pd = (bw["d2h_pinned_s"][0] + d2h_b * bw["d2h_pinned_s"][1]) * 1e3 if "d2h_pinned_s" in bw else None
            tr = float(r["t_h2d_ms"]) + float(r["t_d2h_ms"])
            hp = float(r["t_host_prep_ms"]) if has(bench, "t_host_prep_ms") else None
            hq = float(r["t_host_post_ms"]) if has(bench, "t_host_post_ms") else None
            md.append(f"| {d} | {p} | {n:,} | {h2d_b / 1e6:.1f} | {d2h_b / 1e6:.1f} | "
                      f"{fmt(float(r['t_h2d_ms']))} ({fmt(ph)}) | {fmt(float(r['t_d2h_ms']))} ({fmt(pd)}) | "
                      f"{100 * tr / float(r[wall]):.0f}% | {fmt(hp)} | {fmt(hq)} | "
                      f"{fmt(float(r[wall]) - float(r['t_e2e_ms']))} |")

        md += ["", "## GPU stage breakdown at the largest super-block (ms)", "",
               "| day | pol | n | H2D | extract | sort | detect | csr | D2H | level (CPU sweep) | "
               "device | wall | launches | host syncs | device MB |",
               "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
        for (d, p), n in sorted(nmax.items()):
            g = rows(impl=gpu_impl, detect="balanced", level="sweep", date=d, n=n, policy=p)
            if not len(g):
                continue
            r = g[0]
            md.append(f"| {d} | {p} | {n:,} | {fmt(r['t_h2d_ms'])} | {fmt(r['t_extract_ms'])} | "
                      f"{fmt(r['t_sort_ms'])} | {fmt(r['t_detect_ms'])} | {fmt(r['t_csr_ms'])} | "
                      f"{fmt(r['t_d2h_ms'])} | {fmt(r['t_level_ms'])} | {fmt(r['t_e2e_ms'])} | "
                      f"{fmt(r[wall])} | {r['launches']} | {r['host_syncs']} | "
                      f"{fmt(r['device_mb'], 1)} |")

        # Phase 9
        md += ["", "## Phase 9: skew (detection stage, three thread mappings)", "",
               "Before = one thread per key group (`group`); `record` = one thread per record; "
               "after = one thread per output edge (`balanced`, load-balanced search). Imbalance "
               "= max ÷ mean work per thread, computed from the key groups (1.0 = even). "
               "Recovery = group time ÷ mapping time.", ""]
        if skew is not None:
            md += ["| super-block | pol | records | hottest key | its records (%) | imbalance group / record / edge | "
                   "detect ms group (before) | record | balanced (after) | recovery record / balanced |",
                   "|---|---|---:|---|---:|---|---:|---:|---:|---|"]
            for r in skew:
                md.append(f"| {r['id']} | {r['policy']} | {int(r['records']):,} | `{r['hot_key']}` | "
                          f"{int(r['max_group']):,} ({fmt(float(r['max_group_pct']), 1)}%) | "
                          f"{fmt(float(r['imb_group']), 0)} / {fmt(float(r['imb_record']), 0)} / 1 | "
                          f"{fmt(float(r['detect_group_ms']), 3)} | {fmt(float(r['detect_record_ms']), 3)} | "
                          f"{fmt(float(r['detect_balanced_ms']), 3)} | {fmt(float(r['recovery_record']))}x / "
                          f"{fmt(float(r['recovery_balanced']))}x |")
            md.append("")
        md += ["Detect time across sizes (from the factorial sweep):", "",
               "| day | pol | n | largest group | group ms | record ms | balanced ms | recovery |",
               "|---|---|---:|---:|---:|---:|---:|---:|"]
        for (d, n, p) in keys:
            gg = rows(impl=gpu_impl, detect="group", level="sweep", date=d, n=n, policy=p)
            gr = rows(impl=gpu_impl, detect="record", level="sweep", date=d, n=n, policy=p)
            gb = rows(impl=gpu_impl, detect="balanced", level="sweep", date=d, n=n, policy=p)
            if len(gg) and len(gb):
                a, b = t(gg, "t_detect_ms"), t(gb, "t_detect_ms")
                c = t(gr, "t_detect_ms") if len(gr) else float("nan")
                md.append(f"| {d} | {p} | {n:,} | {int(gb['max_group'][0]):,} | {fmt(a, 3)} | "
                          f"{fmt(c, 3)} | {fmt(b, 3)} | {fmt(a / b)}x |")

        md += ["", "## Stage 4: levelling / batching, GPU methods vs the CPU sweep", "",
               "`rounds` = batches; `tail` = rounds run inside the persistent single block. "
               "Jones-Plassmann is skipped above its size cap.", "",
               "| day | pol | n | method | batches | rounds | tail | launches | level ms | wall ms |",
               "|---|---|---:|---|---:|---:|---:|---:|---:|---:|"]
        for (d, p), n in sorted(nmax.items()):
            for lv in ("sweep", "kahn", "jp-index", "jp-random"):
                g = rows(impl=gpu_impl, detect="balanced", level=lv, date=d, n=n, policy=p)
                if len(g):
                    r = g[0]
                    md.append(f"| {d} | {p} | {n:,} | {lv} | {r['batches']:,} | {r['rounds']:,} | "
                              f"{r['tail_rounds']:,} | {r['launches']:,} | {fmt(r['t_level_ms'], 3)} | "
                              f"{fmt(r[wall])} |")

        if plt is not None:
            scen = sorted(set(str(x) for x in bench["date"]))
            pols = sorted(set(str(x) for x in bench["policy"]))
            fig, axes = plt.subplots(len(scen), len(pols), figsize=(4.4 * len(pols), 3.3 * len(scen)),
                                     squeeze=False)
            for i, d in enumerate(scen):
                for j, p in enumerate(pols):
                    ax = axes[i][j]
                    for lab, sel in (("seq-std", dict(impl="seq-std")),
                                     ("seq-radix", dict(impl="seq-radix")),
                                     (f"{gpu_impl} wall", dict(impl=gpu_impl, detect="balanced",
                                                               level="sweep"))):
                        r = rows(date=d, policy=p, **sel)
                        if len(r):
                            o = np.argsort(r["n"])
                            ax.plot(r["n"][o], r[wall][o], marker="o", ms=3, label=lab)
                    om = rows(date=d, policy=p, impl="omp")
                    if len(om):
                        ns = sorted(set(om["n"]))
                        ax.plot(ns, [om[om["n"] == k][wall].min() for k in ns], marker="o",
                                ms=3, label="OpenMP best")
                    ax.set_xscale("log")
                    ax.set_yscale("log")
                    ax.set_title(f"{d} {p}", fontsize=9)
                    ax.set_xlabel("transactions")
                    ax.set_ylabel("wall ms")
                    ax.grid(alpha=.3)
            axes[0][0].legend(fontsize=7)
            fig.tight_layout()
            fig.savefig(out / "fig_e2e_vs_size.png", dpi=130)
            plt.close(fig)
            md += ["", "![wall time vs size](fig_e2e_vs_size.png)"]

            fig, axes = plt.subplots(1, len(scen), figsize=(5 * len(scen), 3.4), squeeze=False)
            for ax, d in zip(axes[0], scen):
                for mode, ls in (("group", "-"), ("record", "--"), ("balanced", ":")):
                    for p in pols:
                        r = rows(date=d, policy=p, impl=gpu_impl, detect=mode, level="sweep")
                        if len(r):
                            o = np.argsort(r["n"])
                            ax.plot(r["n"][o], r["t_detect_ms"][o], ls=ls, marker=".",
                                    label=f"{mode} {p}")
                ax.set_xscale("log")
                ax.set_yscale("log")
                ax.set_title(f"{d}: detect stage by thread mapping", fontsize=9)
                ax.set_xlabel("transactions")
                ax.set_ylabel("ms")
                ax.grid(alpha=.3)
            axes[0][0].legend(fontsize=6, ncol=3)
            fig.tight_layout()
            fig.savefig(out / "fig_skew.png", dpi=130)
            plt.close(fig)
            md += ["", "![skew](fig_skew.png)"]

    if sched is not None:
        md += ["", "## Schedule quality and the batch-execution model", "",
               "Speedup = serial gas ÷ modelled makespan on W workers (gas as cost, LPT list "
               "scheduling inside a batch, batches in sequence). `bound` = per-key lower bound on "
               "batches for any schedule. `levels` preserve block order; `jp-random` may reorder.",
               ""]
        ws = sorted(set(int(w) for w in sched["workers"]), key=lambda w: (w == 0, w))
        md.append("| block | n | pol | method | batches | bound | tx/batch | " +
                  " | ".join(f"W={w if w else 'inf'}" for w in ws) + " |")
        md.append("|---|---:|---|---|---:|---:|---:|" + "---:|" * len(ws))
        seen = []
        for r in sched:
            k = (str(r["id"]), int(r["n"]), str(r["policy"]), str(r["method"]), int(r["seed"]))
            if k in seen:
                continue
            seen.append(k)
            m = ((sched["id"] == k[0]) & (sched["policy"] == k[2]) &
                 (sched["method"] == k[3]) & (sched["seed"] == k[4]))
            sub = sched[m]
            sp = {int(x["workers"]): float(x["sim_speedup"]) for x in sub}
            md.append(f"| {k[0]} | {k[1]:,} | {k[2]} | {k[3]}{k[4] if k[4] else ''} | "
                      f"{int(sub['batches'][0]):,} | {int(sub['bound'][0]):,} | "
                      f"{fmt(float(sub['parallelism'][0]), 1)} | " +
                      " | ".join(fmt(sp.get(w)) for w in ws) + " |")

    if exe is not None:
        md += ["", "## Execution MODEL: every transaction of the day", "",
               "**This is a model, not an Ethereum client.** Each transaction does work "
               "proportional to its gas on a toy state of one word per state key, over its real "
               "read/write set; every parallel run must reproduce the serial run's final state and "
               "all outputs. `levels` = batch by batch with a barrier (bulk-synchronous); `DAG` = "
               "dataflow list scheduling on the dependency DAG, a standard technique (Kwok & "
               "Ahmad 1999; Graham 1969). Times are measured on this machine's CPU threads; the "
               "W = ∞ columns are model bounds.", "",
               "| block | pol | n | batches | preprocessing seq-std / OpenMP / GPU wall (ms) | "
               "serial (s) | best levels (s, x) | best DAG (s, x) | preprocessing + DAG vs serial | "
               "bound levels W=∞ | bound DAG W=∞ |",
               "|---|---|---:|---:|---|---:|---|---|---:|---:|---:|"]
        done = []
        for r in exe:
            k = (str(r["id"]), str(r["policy"]))
            if k in done:
                continue
            done.append(k)
            sub = exe[(exe["id"] == k[0]) & (exe["policy"] == k[1])]
            names = sub["name"].astype(str)
            kinds = sub["kind"].astype(str)
            pre = sub[kinds == "pre"]
            pre_ms = {str(x["name"]): float(x["seconds"]) * 1e3 for x in pre}
            ser = sub[(kinds == "exec") & (names == "serial")]
            lv = sub[(kinds == "exec") & np.char.startswith(names, "level")]
            dg = sub[(kinds == "exec") & np.char.startswith(names, "DAG")]
            e2e = sub[kinds == "e2e"]
            ml = sub[(kinds == "model") & (names == "levels") & (sub["workers"] == 0)]
            mdg = sub[(kinds == "model") & (names == "dag") & (sub["workers"] == 0)]
            bl = lv[int(np.argmin(lv["seconds"]))] if len(lv) else None
            bd = dg[int(np.argmin(dg["seconds"]))] if len(dg) else None
            gpu_name = [x for x in pre_ms if x not in ("seq-std", "omp")]
            md.append(
                f"| {k[0]} | {k[1]} | {int(sub['executed'][0]):,} | {int(sub['batches'][0]):,} | "
                f"{fmt(pre_ms.get('seq-std'), 0)} / {fmt(pre_ms.get('omp'), 0)} / "
                f"{fmt(pre_ms.get(gpu_name[0]) if gpu_name else None, 0)} | "
                f"{fmt(float(ser['seconds'][0]), 3) if len(ser) else '–'} | "
                + (f"{fmt(float(bl['seconds']), 3)} ({fmt(float(bl['speedup']))}x, {bl['threads']}t) | " if bl is not None else "– | ")
                + (f"{fmt(float(bd['seconds']), 3)} ({fmt(float(bd['speedup']))}x, {bd['threads']}t) | " if bd is not None else "– | ")
                + f"{fmt(float(e2e['speedup'][0])) if len(e2e) else '–'}x | "
                f"{fmt(float(ml['speedup'][0])) if len(ml) else '–'}x | "
                f"{fmt(float(mdg['speedup'][0])) if len(mdg) else '–'}x |")
        if plt is not None:
            ids = sorted(set(zip(exe["id"].astype(str), exe["policy"].astype(str))))
            fig, axes = plt.subplots(1, len(ids), figsize=(3.6 * len(ids), 3.2), squeeze=False)
            for ax, (i, p) in zip(axes[0], ids):
                sub = exe[(exe["id"] == i) & (exe["policy"] == p) & (exe["kind"] == "exec")]
                for lab, pre in (("DAG dataflow", "DAG"), ("level batches", "level")):
                    m = np.char.startswith(sub["name"].astype(str), pre)
                    o = np.argsort(sub[m]["threads"])
                    ax.plot(sub[m]["threads"][o], sub[m]["speedup"][o], marker="o", label=lab)
                ts = sorted(set(sub["threads"]))
                ax.plot(ts, ts, ls=":", c="gray", label="ideal")
                ax.set_title(f"{i} {p}", fontsize=8)
                ax.set_xlabel("threads")
                ax.set_ylabel("speedup (model)")
                ax.grid(alpha=.3)
            axes[0][0].legend(fontsize=7)
            fig.tight_layout()
            fig.savefig(out / "fig_execution_speedup.png", dpi=130)
            plt.close(fig)
            md += ["", "![execution model](fig_execution_speedup.png)"]

    prof = out / "profile" / "profile_summary.md"
    if prof.exists():
        md += ["", "## Phase 10 profiler pass", "", f"See [{prof.relative_to(out)}]({prof.relative_to(out)}).", ""]

    (out / "summary.md").write_text("\n".join(md) + "\n")
    print(f"wrote {out / 'summary.md'}")


if __name__ == "__main__":
    main()
