#!/usr/bin/env python3
"""
Phase 8-10 report: reads the CSVs written by `conduit_gpu bench` and
`conduit_gpu schedule` in one results directory and writes summary.md plus
figures next to them.

    python3 report_gpu.py results/phase8/<host>

Speedups are reported against BOTH CPU baselines (seq-std and seq-radix) and
the best OpenMP run, as the handover's fairness rule asks. The GPU is charged
for its transfers: the headline column is t_e2e (H2D + all stages + D2H).
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
    if not path.exists():
        return None
    return np.genfromtxt(path, delimiter=",", names=True, dtype=None, encoding="utf-8")


def fmt(x, d=2):
    return "–" if x is None or (isinstance(x, float) and np.isnan(x)) else f"{x:.{d}f}"


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    bench = load(out / "bench.csv")
    sched = load(out / "schedule.csv")
    exe = load(out / "execute.csv")
    md = ["# Phase 8-10 results: GPU preprocessing pipeline", ""]
    mt = out / "machine.txt"
    if mt.exists():
        md += ["## Machine", "", "```", mt.read_text().strip()[:3000], "```", ""]

    if bench is not None:
        gpu_impl = "cuda" if "cuda" in set(bench["impl"]) else "emu"
        if gpu_impl == "emu":
            md += ["> **WARNING: GPU rows come from the host EMULATION. Their times are not "
                   "GPU times; only the CPU rows and the counters are meaningful.**", ""]

        def rows(**kw):
            m = np.ones(len(bench), bool)
            for k, v in kw.items():
                m &= bench[k] == v
            return bench[m]

        def t(r, col="t_e2e_ms"):
            return float(r[col][0]) if len(r) else float("nan")

        keys = []
        for r in bench:
            k = (r["date"], int(r["n"]), r["policy"])
            if k not in keys:
                keys.append(k)

        md += ["## End-to-end time per super-block (ms, median)", "",
               "GPU = balanced detection + CPU sweep, transfers included. "
               "`k-only` = GPU stages 1-3 kernels without transfers.", "",
               "| scenario | n | pol | seq-std | seq-radix | omp best (t) | GPU e2e | GPU k-only | "
               "vs seq-std | vs seq-radix | vs omp |",
               "|---|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|"]
        cross = {}
        for (d, n, p) in keys:
            base = dict(date=d, n=n, policy=p)
            ss = t(rows(impl="seq-std", **base))
            sr = t(rows(impl="seq-radix", **base))
            om = rows(impl="omp", **base)
            ob, obt = (float(om["t_e2e_ms"].min()), int(om["threads"][om["t_e2e_ms"].argmin()])) \
                if len(om) else (float("nan"), 0)
            g = rows(impl=gpu_impl, detect="balanced", level="sweep", **base)
            ge, gk = t(g), t(g, "t_stages13_ms")
            md.append(f"| {d} | {n} | {p} | {fmt(ss)} | {fmt(sr)} | {fmt(ob)} ({obt}) | "
                      f"{fmt(ge)} | {fmt(gk)} | {fmt(ss / ge)}x | {fmt(sr / ge)}x | "
                      f"{fmt(ob / ge) if obt else '–'}x |")
            cross.setdefault((d, p), []).append((n, ge, sr, ob))
        md += ["", "### Crossover (smallest tested n where the GPU, transfers charged, wins)", "",
               "| scenario | pol | beats seq-radix at n >= | beats best omp at n >= |",
               "|---|---|---:|---:|"]
        for (d, p), v in cross.items():
            v.sort()
            a = next((n for n, ge, sr, ob in v if ge < sr), None)
            b = next((n for n, ge, sr, ob in v if ge < ob), None)
            md.append(f"| {d} | {p} | {a if a else 'never'} | {b if b else 'never'} |")

        # stage breakdown at the largest n
        md += ["", "## GPU stage breakdown at the largest super-block (ms)", "",
               "| scenario | pol | n | H2D | extract | sort | detect | csr | D2H | level (sweep) | "
               "e2e | wall | launches | syncs | device MB |",
               "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
        nmax = {}
        for (d, n, p) in keys:
            nmax[(d, p)] = max(nmax.get((d, p), 0), n)
        for (d, p), n in nmax.items():
            g = rows(impl=gpu_impl, detect="balanced", level="sweep", date=d, n=n, policy=p)
            if not len(g):
                continue
            r = g[0]
            md.append(f"| {d} | {p} | {n} | {fmt(r['t_h2d_ms'])} | {fmt(r['t_extract_ms'])} | "
                      f"{fmt(r['t_sort_ms'])} | {fmt(r['t_detect_ms'])} | {fmt(r['t_csr_ms'])} | "
                      f"{fmt(r['t_d2h_ms'])} | {fmt(r['t_level_ms'])} | {fmt(r['t_e2e_ms'])} | "
                      f"{fmt(r['t_wall_ms'])} | {r['launches']} | {r['host_syncs']} | "
                      f"{fmt(r['device_mb'], 1)} |")

        # skew: group vs balanced
        md += ["", "## Phase 9: skew (detection, one thread per key vs one thread per edge)", "",
               "| scenario | pol | n | largest key group | detect group (ms) | detect balanced (ms) "
               "| recovery |", "|---|---|---:|---:|---:|---:|---:|"]
        for (d, p), n in nmax.items():
            gg = rows(impl=gpu_impl, detect="group", level="sweep", date=d, n=n, policy=p)
            gb = rows(impl=gpu_impl, detect="balanced", level="sweep", date=d, n=n, policy=p)
            if len(gg) and len(gb):
                a, b = t(gg, "t_detect_ms"), t(gb, "t_detect_ms")
                md.append(f"| {d} | {p} | {n} | {gb['max_group'][0]} | {fmt(a, 3)} | {fmt(b, 3)} | "
                          f"{fmt(a / b)}x |")

        # levelling
        md += ["", "## Stage 4: levelling / batching on the GPU vs the CPU sweep", "",
               "`rounds` = batches; `tail` = rounds run inside the persistent single block.", "",
               "| scenario | pol | n | method | batches | rounds | tail | launches | level ms | e2e ms |",
               "|---|---|---:|---|---:|---:|---:|---:|---:|---:|"]
        for (d, p), n in nmax.items():
            for lv in ("sweep", "kahn", "jp-index", "jp-random"):
                g = rows(impl=gpu_impl, detect="balanced", level=lv, date=d, n=n, policy=p)
                if len(g):
                    r = g[0]
                    md.append(f"| {d} | {p} | {n} | {lv} | {r['batches']} | {r['rounds']} | "
                              f"{r['tail_rounds']} | {r['launches']} | {fmt(r['t_level_ms'], 3)} | "
                              f"{fmt(r['t_e2e_ms'])} |")

        if plt is not None:
            scen = sorted(set(bench["date"]))
            pols = sorted(set(bench["policy"]))
            fig, axes = plt.subplots(len(scen), len(pols), figsize=(4.2 * len(pols), 3.2 * len(scen)),
                                     squeeze=False)
            for i, d in enumerate(scen):
                for j, p in enumerate(pols):
                    ax = axes[i][j]
                    for lab, sel in (("seq-std", dict(impl="seq-std")),
                                     ("seq-radix", dict(impl="seq-radix")),
                                     (f"{gpu_impl} e2e", dict(impl=gpu_impl, detect="balanced",
                                                              level="sweep"))):
                        r = rows(date=d, policy=p, **sel)
                        if len(r):
                            o = np.argsort(r["n"])
                            ax.plot(r["n"][o], r["t_e2e_ms"][o], marker="o", ms=3, label=lab)
                    om = rows(date=d, policy=p, impl="omp")
                    if len(om):
                        ns = sorted(set(om["n"]))
                        ax.plot(ns, [om[om["n"] == k]["t_e2e_ms"].min() for k in ns], marker="o",
                                ms=3, label="omp best")
                    ax.set_xscale("log")
                    ax.set_yscale("log")
                    ax.set_title(f"{d} {p}", fontsize=9)
                    ax.set_xlabel("transactions")
                    ax.set_ylabel("ms")
                    ax.grid(alpha=.3)
            axes[0][0].legend(fontsize=7)
            fig.tight_layout()
            fig.savefig(out / "fig_e2e_vs_size.png", dpi=130)
            md += ["", "![end-to-end](fig_e2e_vs_size.png)"]

    if sched is not None:
        md += ["", "## Schedule quality and simulated execution", "",
               "Speedup = serial gas / simulated makespan on W workers (gas as cost, LPT inside a "
               "batch, barrier between batches). `bound` = per-key lower bound on batches for any "
               "schedule. `levels` preserve block order; `jp-random` may reorder.", ""]
        ws = sorted(set(int(w) for w in sched["workers"]), key=lambda w: (w == 0, w))
        md.append("| scenario | n | pol | method | batches | bound | tx/batch | " +
                  " | ".join(f"W={w if w else 'inf'}" for w in ws) + " |")
        md.append("|---|---:|---|---|---:|---:|---:|" + "---:|" * len(ws))
        seen = []
        for r in sched:
            k = (r["date"], int(r["n"]), r["policy"], r["method"], int(r["seed"]))
            if k in seen:
                continue
            seen.append(k)
            m = ((sched["date"] == k[0]) & (sched["n"] == k[1]) & (sched["policy"] == k[2]) &
                 (sched["method"] == k[3]) & (sched["seed"] == k[4]))
            sub = sched[m]
            sp = {int(x["workers"]): float(x["sim_speedup"]) for x in sub}
            md.append(f"| {k[0]} | {k[1]} | {k[2]} | {k[3]}{k[4] if k[4] else ''} | "
                      f"{sub['batches'][0]} | {sub['bound'][0]} | {fmt(float(sub['parallelism'][0]), 1)} | " +
                      " | ".join(fmt(sp.get(w)) for w in ws) + " |")

    if exe is not None:
        md += ["", "## Parallel execution of every transaction", "",
               "Every transaction is executed (state model in `execute.hpp`); each parallel "
               "run must end in exactly the serial run's state. `levels` = batch by batch with "
               "a barrier; `DAG` = dataflow on the dependency DAG (a transaction starts when "
               "its predecessors finish). End-to-end = GPU preprocessing + best parallel "
               "execution, against plain serial execution.", "",
               "| block | pol | n executed | batches | preprocessing seq-std / OpenMP / GPU (ms) | "
               "serial exec (s) | best levels (s, x) | best DAG (s, x) | end-to-end x | "
               "model levels W=inf | model DAG W=inf |",
               "|---|---|---:|---:|---|---:|---|---|---:|---:|---:|"]
        done = []
        for r in exe:
            k = (r["id"], r["policy"])
            if k in done:
                continue
            done.append(k)
            sub = exe[(exe["id"] == k[0]) & (exe["policy"] == k[1])]

            def pick(kind, name=None):
                m = sub["kind"] == kind
                if name is not None:
                    m &= sub["name"] == name
                return sub[m]
            pre = pick("pre")
            pre_ms = {str(x["name"]): float(x["seconds"]) * 1e3 for x in pre}
            ser = pick("exec", "serial")
            lv = sub[(sub["kind"] == "exec") & np.char.startswith(sub["name"].astype(str), "level")]
            dg = sub[(sub["kind"] == "exec") & np.char.startswith(sub["name"].astype(str), "DAG")]
            e2e = pick("e2e")
            ml = sub[(sub["kind"] == "model") & (sub["name"] == "levels") & (sub["workers"] == 0)]
            mdg = sub[(sub["kind"] == "model") & (sub["name"] == "dag") & (sub["workers"] == 0)]
            bl = lv[lv["seconds"].argmin()] if len(lv) else None
            bd = dg[dg["seconds"].argmin()] if len(dg) else None
            gpu_name = [n for n in pre_ms if n not in ("seq-std", "omp")]
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
            ids = sorted(set(zip(exe["id"], exe["policy"])))
            fig, axes = plt.subplots(1, len(ids), figsize=(4.5 * len(ids), 3.4), squeeze=False)
            for ax, (i, p) in zip(axes[0], ids):
                sub = exe[(exe["id"] == i) & (exe["policy"] == p) & (exe["kind"] == "exec")]
                for lab, pre in (("DAG dataflow", "DAG"), ("level batches", "level")):
                    m = np.char.startswith(sub["name"].astype(str), pre)
                    o = np.argsort(sub[m]["threads"])
                    ax.plot(sub[m]["threads"][o], sub[m]["speedup"][o], marker="o", label=lab)
                ts = sorted(set(sub["threads"]))
                ax.plot(ts, ts, ls=":", c="gray", label="ideal")
                ax.set_title(f"{i} {p}", fontsize=9)
                ax.set_xlabel("threads")
                ax.set_ylabel("speedup over serial execution")
                ax.grid(alpha=.3)
            axes[0][0].legend(fontsize=7)
            fig.tight_layout()
            fig.savefig(out / "fig_execution_speedup.png", dpi=130)
            md += ["", "![execution](fig_execution_speedup.png)"]

    (out / "summary.md").write_text("\n".join(md) + "\n")
    print(f"wrote {out / 'summary.md'}")


if __name__ == "__main__":
    main()
