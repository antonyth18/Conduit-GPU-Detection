#!/usr/bin/env python3
"""Phase 1 exit criteria. Loads the binary corpus and checks it against the
numbers we independently measured from the CSVs."""
import json
import struct
import time
from collections import Counter
from pathlib import Path

import numpy as np

OUT = Path(__file__).parent / "corpus"
ABSENT = 0xFFFFFFFF

DTYPE = np.dtype([
    ("block", "<u4"), ("idx", "<u2"), ("sel", "<u2"),
    ("frm", "<u4"), ("to", "<u4"), ("a1", "<u4"), ("a2", "<u4"),
    ("value_gwei", "<u8"), ("gas", "<u4"),
    ("status", "u1"), ("ttype", "u1"), ("flags", "u1"), ("day", "u1"),
])
assert DTYPE.itemsize == 40

t0 = time.time()
tx = np.fromfile(OUT / "tx.bin", dtype=DTYPE)
blocks = np.fromfile(OUT / "blocks.bin",
                     dtype=np.dtype([("block", "<u4"), ("start", "<u4"),
                                     ("count", "<u4")]))
meta = json.load(open(OUT / "meta.json"))
addrs = open(OUT / "addresses.txt").read().split("\n")[:-1]
sels = open(OUT / "selectors.txt").read().split("\n")[:-1]
load = time.time() - t0

print(f"load time            {load:.2f} s   "
      f"({tx.nbytes/1e6:.0f} MB tx records)")
print(f"transactions         {len(tx):,}")
print(f"blocks               {len(blocks):,}")
print(f"addresses            {len(addrs):,}")
print(f"selectors            {len(sels):,}\n")

ok = True


def check(name, cond, detail=""):
    global ok
    ok &= bool(cond)
    print(f"  {'PASS' if cond else 'FAIL'}  {name}{('  ' + detail) if detail else ''}")


print("counts and ranges")
check("total transactions", len(tx) == meta["total_transactions"])
for d in meta["days"]:
    sub = tx[tx["day"] == d["day_id"]]
    check(f"{d['date']} row count", len(sub) == d["transactions"],
          f"{len(sub):,}")
    check(f"{d['date']} block range",
          sub["block"].min() == d["block_min"]
          and sub["block"].max() == d["block_max"],
          f"{sub['block'].min()}-{sub['block'].max()}")

print("\nordering (execution order is what the dependency rule consumes)")
for d in meta["days"]:
    sub = tx[tx["day"] == d["day_id"]]
    key = sub["block"].astype(np.int64) * 100000 + sub["idx"]
    check(f"{d['date']} sorted by (block, tx_index)",
          bool(np.all(np.diff(key) > 0)))

print("\nblock index integrity")
check("block count matches", len(blocks) == sum(d["blocks"] for d in meta["days"]))
check("offsets contiguous",
      bool(np.all(blocks["start"][1:] == blocks["start"][:-1] + blocks["count"][:-1])))
check("counts sum to total", int(blocks["count"].sum()) == len(tx))
spot = blocks[len(blocks) // 2]
seg = tx[spot["start"]:spot["start"] + spot["count"]]
check("spot-check block segment",
      bool(np.all(seg["block"] == spot["block"])),
      f"block {spot['block']} has {spot['count']} tx")

print("\ncalldata join")
dec = (tx["flags"] & 1).astype(bool)
for d in meta["days"]:
    sub = tx[tx["day"] == d["day_id"]]
    sd = (sub["flags"] & 1).astype(bool)
    n_cd = int(((sub["flags"] & 1) | (sub["flags"] & 4)).astype(bool).sum())
    check(f"{d['date']} calldata rows matched",
          n_cd == d["calldata_matched"], f"{n_cd:,}")
    check(f"{d['date']} decoded rows have an operand",
          bool(np.all(sub["a1"][sd] != ABSENT)))

print("\nknown values from the CSVs")
sel_id = {s: i for i, s in enumerate(sels)}
day0 = tx[tx["day"] == 0]
n_transfer = int((day0["sel"] == sel_id["0xa9059cbb"]).sum())
check("Sep 1 transfer() count == 647,989", n_transfer == 647989, f"{n_transfer:,}")
n_plain = int((day0["sel"] == sel_id["0x"]).sum())
check("Sep 1 plain-transfer count == 632,752", n_plain == 632752, f"{n_plain:,}")

usdt = addrs.index("dac17f958d2ee523a2206206994597c13d831ec7")
n_usdt = int((day0["to"] == usdt).sum())
check("Sep 1 USDT as to_address == 388,725", n_usdt == 388725, f"{n_usdt:,}")

print("\ncorpus statistics")
for d in meta["days"]:
    sub = tx[tx["day"] == d["day_id"]]
    sd = (sub["flags"] & 1).astype(bool)
    print(f"  {d['date']}: {len(sub):,} tx / {d['blocks']:,} blocks "
          f"= {len(sub)/d['blocks']:.0f} tx per block, "
          f"decoded {sd.sum()/len(sub)*100:.1f}%, "
          f"failed {(sub['status']==0).sum()/len(sub)*100:.1f}%")

top = Counter(tx["to"].tolist()).most_common(5)
print("\n  top contracts by call count (both days):")
for aid, n in top:
    label = "(contract creation)" if aid == ABSENT else "0x" + addrs[aid]
    print(f"    {n:>9,}  {n/len(tx)*100:5.1f}%  {label}")

print("\n" + ("ALL CHECKS PASSED" if ok else "SOME CHECKS FAILED"))
