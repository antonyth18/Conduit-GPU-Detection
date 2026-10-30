#!/usr/bin/env python3
"""
Phase 6 - Question A results. Reads results/phase6_runs.csv (from
measure_parallelism.py) and reports, as distributions over all 736
super-blocks x 3 policies:

  1. parallelism (tx / levels) vs granularity, per day and target size
  2. invariance to batch size (10k -> 100k)
  3. critical path against the single-sender nonce floor
  4. bursts: the super-blocks with the least parallelism, and who causes them
  5. batch shape: levels too small to fill an executor

Super-blocks are a modelling decision: consecutive real blocks concatenated in
original order to reach rollup / L2 batch sizes.

Writes results/phase6_summary.md and three figures (PNG) to results/.
"""
import csv
import json
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent
RES = ROOT / "results"
POLICIES = ("P1", "P2", "P3")
NAMES = {"P1": "P1 contract", "P2": "P2 slot", "P3": "P3 commutative"}

# reference palette, light mode: categorical slots 1-3 validated all-pairs
SURFACE = "#fcfcfb"
INK = "#0b0b0b"
INK_2 = "#52514e"
HAIRLINE = "#e4e3df"
COLOR = {"P1": "#2a78d6", "P2": "#eb6834", "P3": "#1baf7a"}


def load():
    rows = list(csv.DictReader(open(RES / "phase6_runs.csv")))
    for r in rows:
        for k in ("target", "sample", "tx_start", "n", "records", "edges", "emitted",
                  "read_comm_edges", "levels", "floor", "floor_sender", "lv_min", "lv_p5",
                  "lv_median", "lv_p95", "lv_max", "levels_lt32", "tx_level0", "deterministic"):
            r[k] = int(r[k])
        for k in ("t_extract_ms", "t_sort_ms", "t_detect_ms", "t_csr_ms", "t_level_ms",
                  "t_total_ms"):
            r[k] = float(r[k])
        r["par"] = r["n"] / r["levels"]
        r["cp_floor"] = r["levels"] / r["floor"]
    return rows


def dist(xs):
    xs = np.asarray(xs, float)
    return {"median": np.median(xs), "p5": np.percentile(xs, 5),
            "p95": np.percentile(xs, 95), "min": xs.min(), "max": xs.max(), "n": len(xs)}


def fmt(d, nd=1):
    return (f"{d['median']:.{nd}f} | {d['p5']:.{nd}f} | {d['p95']:.{nd}f} | "
            f"{d['min']:.{nd}f} | {d['max']:.{nd}f}")


class Doc:
    def __init__(self):
        self.lines = []

    def __call__(self, s=""):
        self.lines.append(s)
        print(s)


def main():
    rows = load()
    manifest = json.load(open(ROOT / "superblocks.json"))
    block_first = {e["id"]: e["block_first"] for e in manifest["superblocks"]}
    addrs = open(ROOT / "corpus" / "addresses.txt").read().split("\n")[:-1]
    valid = list(csv.DictReader(open(RES / "phase6_validation.csv")))
    dates = list(dict.fromkeys(r["date"] for r in rows))
    targets = sorted({r["target"] for r in rows})
    by = {}
    for r in rows:
        by.setdefault((r["date"], r["target"], r["policy"]), []).append(r)
    run = {(r["id"], r["policy"]): r for r in rows}
    ids = list(dict.fromkeys(r["id"] for r in rows))

    out = Doc()
    out("# Phase 6 — Question A: parallelism in real Ethereum blocks")
    out()
    out(f"{len(rows):,} runs: {len(ids)} super-blocks x {len(POLICIES)} policies. "
        "Super-blocks are consecutive real blocks concatenated in original order "
        "(a modelling decision).")
    out()

    out("## Verification")
    out()
    n_valid = sum(int(v["valid"]) for v in valid)
    n_floor = sum(int(v["at_or_above_floor"]) for v in valid)
    out(f"- validate_fast accepted {n_valid:,} of {len(valid):,} schedules")
    out(f"- critical path >= nonce floor in {n_floor:,} of {len(valid):,} runs")
    out(f"- identical output across repeated runs in "
        f"{sum(r['deterministic'] for r in rows):,} of {len(rows):,} runs")
    out(f"- READ<->COMMUTATIVE run-transition edges: "
        f"{sum(r['read_comm_edges'] for r in rows):,} (predicted 0)")
    out()

    # 1. parallelism vs granularity ------------------------------------------------
    out("## 1. Parallelism (transactions / levels) by policy")
    out()
    out("| Day | Target | Samples | Policy | Median | p5 | p95 | Min | Max |")
    out("|---|---|---|---|---|---|---|---|---|")
    for date in dates:
        for t in targets:
            for p in POLICIES:
                rs = by[date, t, p]
                out(f"| {date} | {t // 1000}k | {len(rs)} | {p} | "
                    f"{fmt(dist([r['par'] for r in rs]))} |")
    out()
    out("Per super-block ratio of P3 parallelism to P1 and to P2 (all targets pooled):")
    out()
    out("| Day | Ratio | Median | p5 | p95 | Min | Max |")
    out("|---|---|---|---|---|---|---|")
    for date in dates:
        day_ids = [i for i in ids if run[i, "P1"]["date"] == date]
        for a in ("P1", "P2"):
            d = dist([run[i, "P3"]["par"] / run[i, a]["par"] for i in day_ids])
            out(f"| {date} | P3 / {a} | {fmt(d, 2)} |")
    out()

    # 2. invariance -----------------------------------------------------------------
    out("## 2. Invariance to batch size")
    out()
    out("Elasticity = slope of log(parallelism) on log(transactions) across every "
        "super-block of a day; 0 means flat, 1 means parallelism grows in proportion "
        "to batch size.")
    out()
    out("| Day | Policy | Median at 10k | Median at 100k | 100k / 10k | Elasticity |")
    out("|---|---|---|---|---|---|")
    for date in dates:
        for p in POLICIES:
            rs = [r for r in rows if r["date"] == date and r["policy"] == p]
            slope = np.polyfit(np.log([r["n"] for r in rs]), np.log([r["par"] for r in rs]), 1)[0]
            m10 = np.median([r["par"] for r in by[date, targets[0], p]])
            m100 = np.median([r["par"] for r in by[date, targets[-1], p]])
            out(f"| {date} | {p} | {m10:.1f} | {m100:.1f} | {m100 / m10:.2f} | {slope:+.3f} |")
    out()

    # 3. nonce floor ---------------------------------------------------------------
    out("## 3. Critical path against the nonce floor")
    out()
    out("Ratio = levels / longest single-sender chain in the same super-block.")
    out()
    out("| Day | Policy | Median | p5 | p95 | Min | Max | Exactly on floor | Within 5% |")
    out("|---|---|---|---|---|---|---|---|---|")
    for date in dates:
        for p in POLICIES:
            rs = [r for r in rows if r["date"] == date and r["policy"] == p]
            x = np.array([r["cp_floor"] for r in rs])
            out(f"| {date} | {p} | {fmt(dist(x), 2)} | {np.mean(x == 1) * 100:.1f}% | "
                f"{np.mean(x <= 1.05) * 100:.1f}% |")
    out()

    # 4. bursts --------------------------------------------------------------------
    out("## 4. Bursts: least parallel super-blocks under P3")
    out()
    out("| Super-block | Tx | P3 levels | P3 parallelism | Floor | Floor sender |")
    out("|---|---|---|---|---|---|")
    p3 = sorted((r for r in rows if r["policy"] == "P3"), key=lambda r: (r["par"], r["id"]))
    for r in p3[:10]:
        out(f"| {r['id']} | {r['n']:,} | {r['levels']:,} | {r['par']:.1f} | {r['floor']:,} | "
            f"`0x{addrs[r['floor_sender']]}` |")
    out()
    senders = {}
    for r in p3:
        senders[r["floor_sender"]] = senders.get(r["floor_sender"], 0) + 1
    top = sorted(senders.items(), key=lambda kv: (-kv[1], kv[0]))[:5]
    out("Which sender sets the floor (share of the 736 super-blocks):")
    out()
    for s, c in top:
        out(f"- `0x{addrs[s]}`: {c} ({c / len(p3) * 100:.1f}%)")
    out()

    # 5. batch shape ---------------------------------------------------------------
    out("## 5. Batch shape")
    out()
    out("| Day | Target | Policy | Levels < 32 tx (median) | Tx in level 0 (median) | "
        "Median level size (median) |")
    out("|---|---|---|---|---|---|")
    for date in dates:
        for t in (targets[0], targets[-1]):
            for p in POLICIES:
                rs = by[date, t, p]
                out(f"| {date} | {t // 1000}k | {p} | "
                    f"{np.median([r['levels_lt32'] / r['levels'] for r in rs]) * 100:.1f}% | "
                    f"{np.median([r['tx_level0'] / r['n'] for r in rs]) * 100:.1f}% | "
                    f"{np.median([r['lv_median'] for r in rs]):.0f} |")
    out()

    out("## Sequential pipeline time (median per super-block, ms)")
    out()
    out("| Target | P1 | P2 | P3 |")
    out("|---|---|---|---|")
    for t in targets:
        cells = [np.median([r["t_total_ms"] for d in dates for r in by[d, t, p]])
                 for p in POLICIES]
        out(f"| {t // 1000}k | " + " | ".join(f"{c:.1f}" for c in cells) + " |")
    out()

    attribute_above_floor(out, rows, dates, addrs)

    (RES / "phase6_summary.md").write_text("\n".join(out.lines) + "\n")
    figures(rows, by, dates, targets, block_first)
    print(f"wrote {RES.relative_to(ROOT)}/phase6_summary.md and figures")



def attribute_above_floor(out, rows, dates, addrs):
    """For each P3 super-block, compare the longest WRITE chain on a single
    ACCOUNT key (under P3 only fallback calls write whole accounts) with the
    nonce floor. A count, not a trace of the critical path."""
    import sys
    sys.path.insert(0, str(ROOT))
    from access_sets import WRITE, Selectors, decode_key, infer, load_corpus
    from superblocks import SuperBlocks
    sb = SuperBlocks()
    sels = Selectors(load_corpus()[3])
    stats, contracts = {}, {}
    for r in (r for r in rows if r["policy"] == "P3"):
        s = infer(np.array(sb.load(r["id"])), "P3", sels)
        acct = ((s.keys >> np.uint64(63)) == 0) & (((s.keys >> np.uint64(60)) & np.uint64(7)) == 0)
        u, c = np.unique(s.keys[(s.modes == WRITE) & acct], return_counts=True)
        top = int(c.max()) if len(c) else 0
        above = r["cp_floor"] > 1.05
        d = stats.setdefault(r["date"], [0, 0, 0])
        d[0] += 1
        d[1] += above
        if above and top > r["floor"]:
            d[2] += 1
            a = addrs[decode_key(u[int(np.argmax(c))])[1][0]]
            contracts[a] = contracts.get(a, 0) + 1

    out("## 6. What holds P3 above the floor")
    out()
    out("Longest WRITE chain on one ACCOUNT key (fallback calls) against the nonce floor.")
    out()
    out("| Day | Super-blocks > 5% above floor | With a single-contract chain longer than the floor |")
    out("|---|---|---|")
    for date in dates:
        n, a, e = stats[date]
        out(f"| {date} | {a} of {n} ({a / n * 100:.1f}%) | {e} ({e / a * 100:.1f}%) |")
    out()
    out("| Contract | Above-floor super-blocks |")
    out("|---|---|")
    for a, c in sorted(contracts.items(), key=lambda kv: (-kv[1], kv[0]))[:8]:
        out(f"| `0x{a}` | {c} |")
    out()


# ---- figures -----------------------------------------------------------------------

def style(plt):
    plt.rcParams.update({
        "figure.facecolor": SURFACE, "axes.facecolor": SURFACE, "savefig.facecolor": SURFACE,
        "font.family": "sans-serif", "font.size": 10,
        "text.color": INK, "axes.labelcolor": INK_2, "axes.edgecolor": HAIRLINE,
        "xtick.color": INK_2, "ytick.color": INK_2, "axes.linewidth": 0.8,
        "axes.grid": True, "grid.color": HAIRLINE, "grid.linewidth": 0.6,
        "grid.linestyle": "-", "axes.spines.top": False, "axes.spines.right": False,
        "legend.frameon": False,
    })


def spread(values, gap):
    """Nudge label positions (in log10 space) apart by at least `gap`,
    keeping their order; returns positions in data space."""
    order = np.argsort(values)
    pos = np.log10(np.asarray(values, float))
    out = pos.copy()
    for a, b in zip(order, order[1:]):
        out[b] = max(out[b], out[a] + gap)
    return 10 ** out


def figures(rows, by, dates, targets, block_first):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.ticker import FuncFormatter, LogLocator, NullFormatter
    style(plt)
    plain = FuncFormatter(lambda v, _: f"{v:g}")

    # 1+2. parallelism vs batch size: median line, p5-p95 band, one panel per day
    fig, axes = plt.subplots(1, len(dates), figsize=(11, 4.4), sharey=True)
    x = np.array(targets) / 1000
    for ax, date in zip(axes, dates):
        for p in POLICIES:
            d = [dist([r["par"] for r in by[date, t, p]]) for t in targets]
            med = [v["median"] for v in d]
            ax.fill_between(x, [v["p5"] for v in d], [v["p95"] for v in d],
                            color=COLOR[p], alpha=0.14, linewidth=0)
            ax.plot(x, med, color=COLOR[p], linewidth=2, marker="o", markersize=5,
                    markeredgecolor=SURFACE, markeredgewidth=1.5, label=NAMES[p])
            ax.annotate(f"{NAMES[p]}  {med[-1]:.0f}x", (x[-1], med[-1]),
                        xytext=(8, 0), textcoords="offset points", va="center",
                        color=INK, fontsize=9)
        ax.set_yscale("log")
        ax.yaxis.set_major_locator(LogLocator(subs=(1, 2, 5)))
        ax.yaxis.set_major_formatter(plain)
        ax.yaxis.set_minor_formatter(NullFormatter())
        ax.set_xticks(x)
        ax.set_xticklabels([f"{t:g}k" for t in x])
        ax.set_xlim(x[0] - 4, x[-1] + 38)
        ax.set_title(date, loc="left", fontsize=10, color=INK_2)
        ax.set_xlabel("super-block size (transactions)")
    axes[0].set_ylabel("parallelism (transactions / levels)")
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="lower center", ncol=3, fontsize=9)
    fig.suptitle("Parallelism stays flat from 10k to 100k transactions; "
                 "line = median, band = p5–p95", x=0.01, ha="left", fontsize=11)
    fig.tight_layout(rect=(0, 0.07, 1, 1))
    fig.savefig(RES / "fig_parallelism_vs_size.png", dpi=200)
    plt.close(fig)

    # 3. critical path / floor, every run as a dot (deterministic vertical offset)
    fig, axes = plt.subplots(1, len(dates), figsize=(11, 3.6), sharex=True, sharey=True)
    for ax, date in zip(axes, dates):
        for k, p in enumerate(POLICIES):
            v = np.array([r["cp_floor"] for r in rows if r["date"] == date and r["policy"] == p])
            y = k + ((np.arange(len(v)) % 11) - 5) * 0.045
            ax.scatter(v, y, s=9, color=COLOR[p], alpha=0.5, linewidths=0)
            ax.plot([np.median(v)] * 2, [k - 0.32, k + 0.32], color=INK, linewidth=1.5)
        ax.set_xscale("log")
        ax.xaxis.set_major_locator(LogLocator(subs=(1, 2, 5)))
        ax.xaxis.set_major_formatter(plain)
        ax.xaxis.set_minor_formatter(NullFormatter())
        ax.axvline(1, color=INK_2, linewidth=1)
        ax.set_yticks(range(len(POLICIES)))
        ax.set_yticklabels([NAMES[p] for p in POLICIES])
        ax.grid(axis="y", visible=False)
        ax.set_title(date, loc="left", fontsize=10, color=INK_2)
        ax.set_xlabel("critical path / nonce floor (1 = on the floor)")
    fig.suptitle("Under P3 the critical path is the busiest sender's chain; "
                 "dot = super-block, bar = median", x=0.01, ha="left", fontsize=11)
    fig.tight_layout()
    fig.savefig(RES / "fig_floor_ratio.png", dpi=200)
    plt.close(fig)

    # 4. bursts over the day: 10k super-blocks in block order
    fig, axes = plt.subplots(len(dates), 1, figsize=(11, 6.2), sharey=True)
    t10 = targets[0]
    for ax, date in zip(axes, dates):
        rs0 = sorted(by[date, t10, "P1"], key=lambda r: r["sample"])
        b0 = block_first[rs0[0]["id"]]
        hrs = np.array([(block_first[r["id"]] - b0) * 12 / 3600 for r in rs0])
        ends = []
        for p in POLICIES:
            rs = sorted(by[date, t10, p], key=lambda r: r["sample"])
            par = [r["par"] for r in rs]
            ax.plot(hrs, par, color=COLOR[p], linewidth=1.5, label=NAMES[p])
            ends.append(par[-1])
        for p, y in zip(POLICIES, spread(ends, 0.13)):
            ax.annotate(NAMES[p], (hrs[-1], y), xytext=(8, 0),
                        textcoords="offset points", va="center", color=INK, fontsize=9)
        ax.set_yscale("log")
        ax.yaxis.set_major_locator(LogLocator(subs=(1, 2, 5)))
        ax.yaxis.set_major_formatter(plain)
        ax.yaxis.set_minor_formatter(NullFormatter())
        ax.set_xlim(0, 27.5)
        ax.set_title(date, loc="left", fontsize=10, color=INK_2)
        ax.set_ylabel("tx / levels")
    axes[-1].set_xlabel("hours since the day's first block (about 12 s per block)")
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="upper right", ncol=3, fontsize=9)
    fig.suptitle("Parallelism through the day, 10k super-blocks in block order", x=0.01,
                 ha="left", fontsize=11)
    fig.tight_layout(rect=(0, 0, 1, 0.97))
    fig.savefig(RES / "fig_bursts.png", dpi=200)
    plt.close(fig)


if __name__ == "__main__":
    main()
