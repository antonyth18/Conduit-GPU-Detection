#!/usr/bin/env python3
"""
Phase 10 profiler summary: turns the Nsight Compute raw CSV (ncu --csv --page
raw) and the Nsight Systems stats CSVs written by tools/profile.sh into one
markdown table per policy:

  kernel | launches | time (us) | share | achieved / theoretical occupancy |
  SM % | memory % | DRAM % | DRAM GB/s | warp efficiency | branch uniformity

Warp efficiency = smsp__thread_inst_executed_per_inst_executed.ratio / 32:
the average fraction of a warp's 32 lanes active per executed instruction, so
1.0 means no divergence (Nsight Compute Kernel Profiling Guide). Per-kernel
values are time-weighted averages over that kernel's launches.

    python3 report_profile.py results/phase10/<host>-profile
"""
import csv
import sys
from collections import OrderedDict, defaultdict
from pathlib import Path

M = {
    "time": "gpu__time_duration.sum",
    "occ": "sm__warps_active.avg.pct_of_peak_sustained_active",
    "occ_max": "sm__maximum_warps_per_active_cycle_pct",
    "sm": "sm__throughput.avg.pct_of_peak_sustained_elapsed",
    "mem": "gpu__compute_memory_throughput.avg.pct_of_peak_sustained_elapsed",
    "dram": "dram__throughput.avg.pct_of_peak_sustained_elapsed",
    "dram_bw": "dram__bytes.sum.per_second",
    "thr": "smsp__thread_inst_executed_per_inst_executed.ratio",
    "branch": "smsp__sass_average_branch_targets_threads_uniform.pct",
    "grid": "launch__grid_size",
    "block": "launch__block_size",
    "regs": "launch__registers_per_thread",
}
TIME_SCALE = {"nsecond": 1e-3, "ns": 1e-3, "usecond": 1.0, "us": 1.0, "msecond": 1e3, "ms": 1e3}
BW_SCALE = {"byte/second": 1e-9, "Kbyte/second": 1e-6, "Mbyte/second": 1e-3,
            "Gbyte/second": 1.0, "Tbyte/second": 1e3}


def num(s):
    try:
        return float(str(s).replace(",", ""))
    except ValueError:
        return None


def short(name):
    n = name.replace("(anonymous namespace)::", "").replace("<unnamed>::", "").strip()
    if n.startswith("void "):
        n = n[5:]
    n = n.split("(")[0].split("<")[0].strip()
    return n.split("::")[-1]


def parse_ncu(path):
    rows = list(csv.reader(open(path, newline="", errors="replace")))
    start = next((i for i, r in enumerate(rows) if r and r[0] == "ID"), None)
    if start is None:
        return None
    head = rows[start]
    col = {h: i for i, h in enumerate(head)}
    units = rows[start + 1] if start + 1 < len(rows) else []
    kern = col.get("Kernel Name")
    agg = OrderedDict()
    for r in rows[start + 1:]:
        if not r or num(r[0]) is None or kern is None:
            continue   # units row / blank
        name = short(r[kern])
        a = agg.setdefault(name, defaultdict(float))
        a["launches"] += 1
        t = num(r[col[M["time"]]]) if M["time"] in col else None
        if t is None:
            continue
        u = units[col[M["time"]]] if units and len(units) > col[M["time"]] else "nsecond"
        t *= TIME_SCALE.get(u, 1e-3)
        a["time"] += t
        for k in ("occ", "occ_max", "sm", "mem", "dram", "thr", "branch", "dram_bw"):
            c = col.get(M[k])
            v = num(r[c]) if c is not None else None
            if v is None:
                continue
            if k == "dram_bw" and units and len(units) > c:
                v *= BW_SCALE.get(units[c], 1e-9)
            a[k] += v * t
            a[k + "_w"] += t
        for k in ("grid", "block", "regs"):
            c = col.get(M[k])
            v = num(r[c]) if c is not None else None
            if v is not None:
                a[k] = max(a[k], v)
    return agg


def avg(a, k):
    return a[k] / a[k + "_w"] if a.get(k + "_w") else None


def f(x, d=1):
    return "–" if x is None else f"{x:.{d}f}"


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    md = ["# Phase 10 profiler pass", ""]
    mt = out / "profile_machine.txt"
    if mt.exists():
        md += ["```", mt.read_text().strip(), "```", ""]
    md += ["Metrics from Nsight Compute (`--page raw`), one pipeline pass (warm-up 0, 1 run) "
           "on the largest super-block. Occupancy = achieved / theoretical active warps per "
           "SM; memory % = `gpu__compute_memory_throughput` of peak; warp efficiency = average "
           "active lanes per executed instruction ÷ 32 (1.00 = no divergence); branch "
           "uniformity = % of branches where all active threads take the same target. Time-"
           "weighted over a kernel's launches.", ""]
    found = False
    for p in sorted(out.glob("ncu_P*.csv")):
        agg = parse_ncu(p)
        if not agg:
            md += [f"`{p.name}`: no kernel rows (see the matching .log)", ""]
            continue
        found = True
        total = sum(a["time"] for a in agg.values()) or 1
        md += [f"## {p.stem.replace('ncu_', 'Policy ')}", "",
               "| kernel | launches | time µs | share | occupancy % (ach / theo) | SM % | "
               "memory % | DRAM % | DRAM GB/s | warp eff. | branch unif. % | grid × block, regs |",
               "|---|---:|---:|---:|---|---:|---:|---:|---:|---:|---:|---|"]
        for name, a in sorted(agg.items(), key=lambda kv: -kv[1]["time"]):
            thr = avg(a, "thr")
            md.append(
                f"| `{name}` | {int(a['launches'])} | {f(a['time'])} | {100 * a['time'] / total:.1f}% | "
                f"{f(avg(a, 'occ'))} / {f(avg(a, 'occ_max'))} | {f(avg(a, 'sm'))} | "
                f"{f(avg(a, 'mem'))} | {f(avg(a, 'dram'))} | {f(avg(a, 'dram_bw'))} | "
                f"{f(thr / 32 if thr else None, 2)} | {f(avg(a, 'branch'))} | "
                f"{int(a['grid'])} × {int(a['block'])}, {int(a['regs'])} |")
        md.append("")
    if not found:
        md += ["No Nsight Compute data found (run `bash tools/profile.sh` on the GPU machine).", ""]
    empty = []
    for pat, title, cap in (("nsys_P*_cuda_gpu_kern_sum*.csv", "kernel summary", 4000),
                            ("nsys_P*_cuda_gpu_mem_time_sum*.csv", "memcpy summary", 3000),
                            ("nsys_P*_cuda_api_sum*.csv", "CUDA API summary (host side)", 3000)):
        for p in sorted(out.glob(pat)):
            text = p.read_text().strip()
            if not text:
                empty.append(p.name)
                continue
            md += [f"## Nsight Systems {title}: {p.name}", "", "```", text[:cap], "```", ""]
    if empty:
        md += ["## Nsight Systems: no GPU activity recorded", "",
               "These Nsight Systems reports are empty: " + ", ".join(f"`{e}`" for e in empty) + ". "
               "The capture contains host-side CUDA API calls but no GPU kernel or memcpy records "
               "(no `CUPTI_ACTIVITY_KIND_KERNEL` table), which happens when Nsight Systems cannot trace "
               "GPU activity under WSL2. The per-kernel times above (Nsight Compute) and the cudaEvent "
               "stage timings in `results/phase8/<host>-real-large/` cover the same ground.", ""]
    (out / "profile_summary.md").write_text("\n".join(md) + "\n")
    print(f"wrote {out / 'profile_summary.md'}")


if __name__ == "__main__":
    main()
