#!/usr/bin/env python3
"""Phase 2 exit criteria. Runs every policy over every day and prints
access-set statistics, hot keys, fallback rates and the USDT fragmentation."""
import resource
import sys
import time

import numpy as np

from access_sets import (
    CAT_PLAIN, COMMUTATIVE, FLAG_TRUNC, KIND_TOKEN_BAL, POLICIES,
    WRITE, Selectors, account_key, infer, label_key, load_corpus,
)

TOP_N = (1, 10, 100)


def key_stats(keys):
    """Per-key access counts, hottest first (ties broken by key ascending)."""
    uniq, counts = np.unique(keys, return_counts=True)
    order = np.lexsort((uniq, -counts.astype(np.int64)))
    return uniq[order], counts[order]


def print_dist(title, uniq, counts, n_tx, addrs):
    total = int(counts.sum())
    p50, p95, p99 = np.percentile(counts, [50, 95, 99])
    shares = "  ".join(f"top{k} {counts[:k].sum()/total*100:5.2f}%" for k in TOP_N)
    print(f"    {title}: {total:,} accesses over {len(uniq):,} keys")
    print(f"      accesses/key   max {counts[0]:,}  p99 {p99:.0f}  "
          f"p95 {p95:.0f}  median {p50:.0f}")
    print(f"      share          {shares}")
    print("      hottest keys   (share of accesses / share of tx touching it)")
    for k, c in zip(uniq[:5], counts[:5]):
        print(f"        {c/total*100:5.2f}% {c/n_tx*100:5.1f}%  {label_key(k, addrs)}")


def usdt_view(sets, usdt, conflict):
    """Accesses to USDT state: ACCOUNT(USDT) plus every slot inside it."""
    k = sets.keys[conflict]
    m = np.uint64((1 << 21) - 1)
    allowance = (k >> np.uint64(63)) == 1
    balance = (~allowance & ((k >> np.uint64(60)) & np.uint64(7) == KIND_TOKEN_BAL)
               & ((k >> np.uint64(21)) & m == usdt))
    slot = balance | (allowance & ((k >> np.uint64(42)) & m == usdt))
    whole = k == account_key(usdt)
    _, counts = key_stats(k[slot])
    hottest = int(counts[0]) if len(counts) else 0
    return int(whole.sum()), int(slot.sum()), len(counts), hottest, len(k)


def main():
    t0 = time.time()
    tx_all, meta, addrs, sel_names = load_corpus()
    sels = Selectors(sel_names)
    usdt = addrs.index("dac17f958d2ee523a2206206994597c13d831ec7")
    print(f"loaded {len(tx_all):,} transactions in {time.time()-t0:.1f} s\n")

    headline = []
    for d in meta["days"]:
        s = d["first_tx_index"]
        tx = tx_all[s:s + d["transactions"]]
        n = len(tx)
        print("=" * 78)
        print(f"{d['date']}  {n:,} transactions")
        print("=" * 78)
        for policy in POLICIES:
            t1 = time.time()
            sets = infer(tx, policy, sels)
            sizes = sets.sizes()
            print(f"\n  {policy}  ({time.time()-t1:.1f} s)")
            print(f"    access-set size   mean {sizes.mean():.2f}  "
                  f"median {np.median(sizes):.0f}  max {sizes.max()}")
            if policy != "P1":
                fb = sets.fallback.mean()
                trunc = ((tx["flags"] & FLAG_TRUNC) != 0) & ~sets.fallback
                plain = (sets.category == CAT_PLAIN).mean()
                print(f"    fallback rate     {fb*100:.2f}%  (plain ETH {plain*100:.1f}%, "
                      f"decoded ERC-20 {(1-fb-plain)*100:.1f}%; "
                      f"{int(trunc.sum()):,} decoded with truncated amount)")
            else:
                print("    fallback rate     n/a (P1 uses no calldata)")
            modes = np.bincount(sets.modes, minlength=5)
            print(f"    modes             W {modes[WRITE]:,}  C {modes[COMMUTATIVE]:,}  "
                  f"R {modes[1]:,}")

            uniq, counts = key_stats(sets.keys)
            print_dist("all accesses", uniq, counts, n, addrs)
            conflict = sets.modes != 1
            if policy != "P1":
                u2, c2 = key_stats(sets.keys[conflict])
                print_dist("W + C accesses", u2, c2, n, addrs)
            if policy == "P3":
                u3, c3 = key_stats(sets.keys[sets.modes == WRITE])
                print(f"    W-only hottest key {c3[0]/len(sets.keys)*100:.2f}% "
                      f"of accesses: {label_key(u3[0], addrs)}")
            headline.append((d["date"], policy, n, usdt_view(sets, usdt, conflict)))
            del sets, uniq, counts

    print("\n" + "=" * 78)
    print("USDT fragmentation (conflict-bearing accesses: WRITE + COMMUTATIVE)")
    print("=" * 78)
    print("  access share depends on the policy's access count; tx share does not")
    p1 = {}
    for date, policy, n, (whole, slot, nslots, hottest, total) in headline:
        if policy == "P1":
            p1[date] = (whole / total, whole / n)
            print(f"  {date} P1  ACCOUNT(USDT): {whole/total*100:.2f}% of accesses, "
                  f"{whole/n*100:.1f}% of tx")
        elif policy == "P2":
            top = max(hottest, whole)
            acc, txs = top / total, top / n
            print(f"  {date} P2  {whole:,} fallback WRITEs of ACCOUNT(USDT); "
                  f"{slot:,} accesses over {nslots:,} USDT slots")
            print(f"  {' ' * len(date)}      hottest USDT key: {acc*100:.2f}% of accesses "
                  f"({p1[date][0]/acc:.1f}x less), {txs*100:.1f}% of tx "
                  f"({p1[date][1]/txs:.1f}x less)")

    rss = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    rss /= 1e9 if sys.platform == "darwin" else 1e6
    print(f"\npeak RSS {rss:.2f} GB, total {time.time()-t0:.1f} s")


if __name__ == "__main__":
    main()
