#!/usr/bin/env python3
"""Phase 3 exit criteria. Super-block counts and sizes per (day, target), the
per-sender nonce-chain floor, and the cross-day comparability check.

Super-blocks are a modelling decision: consecutive real blocks concatenated in
original order to reach rollup / L2-sequencer batch sizes (10k-100k tx)."""
import numpy as np

from access_sets import load_corpus
from superblocks import SuperBlocks, max_sender


def mmm(xs):
    return f"{min(xs):>7,} {int(np.median(xs)):>7,} {max(xs):>7,}"


def main():
    sb = SuperBlocks()
    m = sb.manifest
    addrs = load_corpus()[2]
    print("Super-blocks: consecutive real blocks concatenated in original order, "
          "sized by tx count (modelling decision).")
    print(f"rule: {m['rule']}; seed: {m['seed']}")
    print(f"corpus tx.bin sha256 {m['corpus']['tx_bin_sha256'][:16]}..., "
          f"{len(m['superblocks'])} super-blocks\n")

    print(f"{'day':<11}{'target':>8}{'n':>5} | {'tx min/med/max':^23} | "
          f"{'blocks min/med/max':^23} | {'max-sender min/med/max':^23} | "
          f"{'tail':>7}")
    rows = {}
    for c in m["configs"]:
        es = sb.entries(c["date"], c["target"])
        tx = [e["tx_count"] for e in es]
        bl = [e["block_count"] for e in es]
        ms = [e["max_sender_tx"] for e in es]
        rows[c["date"], c["target"]] = (tx, bl, ms)
        print(f"{c['date']:<11}{c['target']:>8,}{c['samples']:>5} | {mmm(tx)} | "
              f"{mmm(bl)} | {mmm(ms)} | {c['dropped_tail_tx']:>7,}")

    print("\nnonce-chain floor: a sender with k tx in a super-block forces a "
          "dependency chain of length k\nunder every policy, so tx_count / "
          "max_sender_tx bounds the average parallelism.")
    print(f"\n{'day':<11}{'target':>8} | {'floor (max-sender) med':>22} | "
          f"{'parallelism bound med':>21} | representative (median sample)")
    for c in m["configs"]:
        es = sb.entries(c["date"], c["target"])
        bound = [e["tx_count"] / e["max_sender_tx"] for e in es]
        rep = es[len(es) // 2]
        sender, k = max_sender(sb.load(rep)["frm"])
        print(f"{c['date']:<11}{c['target']:>8,} | "
              f"{int(np.median([e['max_sender_tx'] for e in es])):>22,} | "
              f"{np.median(bound):>21,.0f} | {rep['id']}: 0x{addrs[sender]} "
              f"sends {k:,} of {rep['tx_count']:,}")

    dates = [d for d in dict.fromkeys(c["date"] for c in m["configs"])]
    if len(dates) == 2:
        a, b = dates
        print(f"\ncomparability at equal target ({a} vs {b}, medians)")
        print(f"{'target':>8} | {'tx':^19} | {'tx ratio':>8} | {'blocks':^15} | "
              f"{'block ratio':>11}")
        for t in m["targets"]:
            ta, ba = (int(np.median(x)) for x in rows[a, t][:2])
            tb, bb = (int(np.median(x)) for x in rows[b, t][:2])
            print(f"{t:>8,} | {ta:>8,} {tb:>8,} | {tb/ta:>8.3f} | "
                  f"{ba:>6,} {bb:>6,} | {bb/ba:>11.2f}")

    gaps = [e for e in m["superblocks"] if e["missing_blocks"]]
    print(f"\n{len(gaps)} super-blocks span missing block numbers "
          f"(absent from the export, most likely empty blocks)")


if __name__ == "__main__":
    main()
