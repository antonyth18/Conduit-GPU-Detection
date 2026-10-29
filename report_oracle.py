#!/usr/bin/env python3
"""Phase 4 exit criteria. Runs the oracle on small slices from both days under
every policy: edges, levels, batch sizes, validator self-check, critical path
against the nonce-chain floor. Writes the reference outputs to oracle_ref/ so
later phases can be compared against them. Slow on purpose; never timed."""
import time
from pathlib import Path

import numpy as np

from access_sets import POLICIES, Selectors, infer, load_corpus
from compare import compare
from oracle import access_lists, batch_sizes, critical_path, run, validate
from superblocks import SuperBlocks, max_sender

SIZES = (500, 1000, 2000)
SOURCE_TARGET = 10_000
REF = Path(__file__).parent / "oracle_ref"


def slices(sb):
    """First N tx of each day's median 10k super-block (Phase 3's
    representative sample). Cut at exactly N, which may split a block."""
    dates = list(dict.fromkeys(e["date"] for e in sb.manifest["superblocks"]))
    for date in dates:
        es = sb.entries(date, SOURCE_TARGET)
        rep = es[len(es) // 2]
        seg = sb.load(rep)
        for n in SIZES:
            yield date, rep["id"], n, np.array(seg[:n])


def main():
    sb = SuperBlocks()
    _, _, addrs, sel_names = load_corpus()
    sels = Selectors(sel_names)
    REF.mkdir(exist_ok=True)
    t0 = time.time()

    print("Oracle: brute-force O(n^2) reference. Times are informative only.\n")
    print(f"{'slice':<32}{'pol':>4}{'edges':>11}{'levels':>8}{'floor':>7}"
          f"{'lv/floor':>9}{'par':>7}  {'batch min/med/max':<19}{'validator':<10}"
          f"{'self-cmp':<10}{'broken':<9}{'s':>6}")
    summary = []
    for date, sid, n, tx in slices(sb):
        sender, floor = max_sender(tx["frm"])
        for policy in POLICIES:
            t1 = time.time()
            access = access_lists(infer(tx, policy, sels))
            out = run(access)
            lv = out["levels"]
            cp = critical_path(lv)
            sizes = batch_sizes(lv)

            ok, bad = validate(access, lv)
            same = compare(out, run(access), n)["verdict"]
            # a deliberately broken schedule: the first edge's endpoints
            # share a level. The validator must reject it.
            rejected = "n/a"
            if out["edges"]:
                broken = list(lv)
                i, j = out["edges"][0]
                broken[j] = broken[i]
                rejected = "rejected" if not validate(access, broken)[0] else "ACCEPTED"

            np.savez_compressed(
                REF / f"{date}_{n}_{policy}.npz",
                slice_id=sid, tx_start=sb.by_id[sid]["tx_start"], n=n,
                policy=policy, edges=np.array(out["edges"], np.int32).reshape(-1, 2),
                levels=np.array(lv, np.int32))

            label = f"{sid}[:{n}]"
            print(f"{label:<32}{policy:>4}{len(out['edges']):>11,}{cp:>8,}{floor:>7}"
                  f"{cp/floor:>9.2f}{n/cp:>7.1f}  "
                  f"{min(sizes):>4} {int(np.median(sizes)):>5} {max(sizes):>5}     "
                  f"{'accept' if ok else 'REJECT':<10}{same:<10}{rejected:<9}"
                  f"{time.time()-t1:>6.1f}")
            if not ok:
                print(f"    validator rejected the oracle's own output: {bad[:3]}")
            summary.append((date, n, policy, cp, floor))
        print(f"{'':<32}     floor = 0x{addrs[sender]} with {floor} tx")

    print("\ncritical path by policy (levels), floor = longest single-sender chain")
    print(f"{'day':<11}{'n':>6}{'P1':>7}{'P2':>7}{'P3':>7}{'floor':>7}"
          f"{'P1/P3':>7}{'P2/P3':>7}{'P3/floor':>9}")
    rows = {}
    for date, n, policy, cp, floor in summary:
        rows.setdefault((date, n), {"floor": floor})[policy] = cp
    for (date, n), r in rows.items():
        print(f"{date:<11}{n:>6}{r['P1']:>7}{r['P2']:>7}{r['P3']:>7}{r['floor']:>7}"
              f"{r['P1']/r['P3']:>7.2f}{r['P2']/r['P3']:>7.2f}{r['P3']/r['floor']:>9.2f}")
    print(f"\nreference outputs in {REF.name}/, total {time.time()-t0:.0f} s")


if __name__ == "__main__":
    main()
