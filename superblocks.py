#!/usr/bin/env python3
"""
Phase 3 - Super-block assembly.

Real blocks hold ~200-300 transactions; the preprocessing pipeline only gets
interesting at 10k-100k, the scale of rollup batches and L2 sequencers. As a
documented modelling decision we study SUPER-BLOCKS: runs of consecutive real
blocks concatenated in their original (block_number, transaction_index) order.

Rule: for each (day, target), start at the day's first block, add whole blocks
until the transaction count meets or exceeds the target, close the super-block,
continue from the next block. A tail that cannot reach the target is dropped.
Greedy earliest-close tiling maximises the number of disjoint samples and has
no free choices, so no seed is needed.

A super-block is the range [tx_start, tx_start + tx_count) of corpus/tx.bin.
Nothing is copied; the manifest (superblocks.json) is the deliverable and is
pinned to the corpus by the SHA-256 of tx.bin.

Usage: python3 superblocks.py      writes superblocks.json
"""
import hashlib
import json
from pathlib import Path

import numpy as np

from access_sets import CORPUS, DTYPE

MANIFEST = Path(__file__).parent / "superblocks.json"
TARGETS = (10_000, 20_000, 40_000, 60_000, 80_000, 100_000)
RULE = ("greedy whole-block tiling from the first block of each day; "
        "tail below target dropped")
BLOCK_DTYPE = np.dtype([("block", "<u4"), ("start", "<u4"), ("count", "<u4")])


def tile(counts, target):
    """Greedy whole-block tiling of one day. counts = tx per block.
    Returns [(first, end)] block positions, end exclusive."""
    cum = np.zeros(len(counts) + 1, np.int64)
    np.cumsum(counts, out=cum[1:])
    out = []
    i = 0
    while True:
        j = int(np.searchsorted(cum, cum[i] + target, side="left"))
        if j > len(counts):
            return out
        out.append((i, j))
        i = j


def max_sender(frm):
    """(sender id, tx count) of the sender with the most transactions;
    ties go to the lowest id."""
    ids, counts = np.unique(frm, return_counts=True)
    k = int(np.argmax(counts))
    return int(ids[k]), int(counts[k])


def assemble(tx, blocks, days, targets=TARGETS):
    """days: meta["days"]-style dicts with date, day_id, blocks. Blocks of each
    day are contiguous in blocks.bin, in day order. Returns (configs, entries)."""
    configs, entries = [], []
    block_off = 0
    for d in days:
        db = blocks[block_off:block_off + d["blocks"]]
        for target in targets:
            spans = tile(db["count"], target)
            used = 0
            for sample, (i, j) in enumerate(spans):
                start = int(db["start"][i])
                count = int(db["count"][i:j].sum())
                first, last = int(db["block"][i]), int(db["block"][j - 1])
                _, top = max_sender(tx["frm"][start:start + count])
                entries.append({
                    "id": f"{d['date']}/{target // 1000}k/{sample:03d}",
                    "date": d["date"],
                    "day_id": d["day_id"],
                    "target": target,
                    "sample": sample,
                    "tx_start": start,
                    "tx_count": count,
                    "block_first": first,
                    "block_last": last,
                    "block_count": j - i,
                    "block_index_start": block_off + i,
                    "missing_blocks": (last - first + 1) - (j - i),
                    "max_sender_tx": top,
                })
                used += count
            configs.append({
                "date": d["date"],
                "day_id": d["day_id"],
                "target": target,
                "samples": len(spans),
                "dropped_tail_tx": int(db["count"].sum()) - used,
            })
        block_off += d["blocks"]
    return configs, entries


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 24), b""):
            h.update(chunk)
    return h.hexdigest()


def build_manifest(corpus=CORPUS, targets=TARGETS):
    tx = np.memmap(corpus / "tx.bin", dtype=DTYPE, mode="r")
    blocks = np.fromfile(corpus / "blocks.bin", dtype=BLOCK_DTYPE)
    meta = json.load(open(corpus / "meta.json"))
    configs, entries = assemble(tx, blocks, meta["days"], targets)
    return {
        "version": 1,
        "corpus": {"tx_bin_sha256": sha256(corpus / "tx.bin"),
                   "total_transactions": len(tx)},
        "rule": RULE,
        "seed": None,
        "targets": list(targets),
        "configs": configs,
        "superblocks": entries,
    }


def dumps(manifest):
    return json.dumps(manifest, indent=1) + "\n"


class SuperBlocks:
    """Manifest plus a memory-mapped corpus; refuses a manifest built from a
    different tx.bin."""

    def __init__(self, manifest_path=MANIFEST, corpus=CORPUS):
        self.manifest = json.load(open(manifest_path))
        want = self.manifest["corpus"]["tx_bin_sha256"]
        have = sha256(corpus / "tx.bin")
        if have != want:
            raise ValueError(f"manifest built from tx.bin {want[:12]}..., "
                             f"corpus is {have[:12]}...; rebuild the manifest")
        self.tx = np.memmap(corpus / "tx.bin", dtype=DTYPE, mode="r")
        self.by_id = {e["id"]: e for e in self.manifest["superblocks"]}

    def entries(self, date=None, target=None):
        return [e for e in self.manifest["superblocks"]
                if (date is None or e["date"] == date)
                and (target is None or e["target"] == target)]

    def load(self, entry):
        """tx records of a super-block (a view into the memmap, no copy)."""
        if isinstance(entry, str):
            entry = self.by_id[entry]
        return self.tx[entry["tx_start"]:entry["tx_start"] + entry["tx_count"]]


def main():
    m = build_manifest()
    MANIFEST.write_text(dumps(m))
    print(f"wrote {MANIFEST.name}: {len(m['superblocks'])} super-blocks")
    for c in m["configs"]:
        print(f"  {c['date']}  {c['target']:>7,}  {c['samples']:>4} samples  "
              f"(tail dropped: {c['dropped_tail_tx']:,} tx)")


if __name__ == "__main__":
    main()
