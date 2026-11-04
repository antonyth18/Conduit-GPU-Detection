#!/usr/bin/env python3
"""
Synthetic corpus generator (Phase 8-10 workloads without the BigQuery data).

Writes a corpus directory in exactly the format ingest.py produces
(tx.bin, blocks.bin, meta.json, selectors.txt, addresses.txt) plus a
super-block manifest built by superblocks.build_manifest, so every C++ and
Python tool runs on it unchanged:

    python3 tools/synth_corpus.py --out corpus_synth
    build/conduit_gpu verify --corpus corpus_synth --manifest corpus_synth/superblocks.json

Each SCENARIO becomes one "day" (its name is the date label). Scenarios are
the paper's factorial: conflict density (population size) x access skew
(uniform vs Zipf), plus an Ethereum-like mix calibrated to Phase 1-6
(about a third of transactions are generic calls with no inferable
footprint, a hot exchange wallet with a long nonce chain, Zipf token use).

These are MODELS, not measurements: anything reported from them must be
labelled synthetic. Use them for scaling curves and the skew experiment;
use the real corpus for claims about Ethereum.
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
from access_sets import DTYPE, FLAG_CREATE, FLAG_DECODED, ABSENT  # noqa: E402
from superblocks import BLOCK_DTYPE, TARGETS, build_manifest, dumps  # noqa: E402

GENERIC_SELECTORS = 200
SELECTORS = ["0x", "0xa9059cbb", "0x095ea7b3", "0x23b872dd"] + [
    "0x%08x" % (0x10000000 + 7919 * i) for i in range(GENERIC_SELECTORS)]
SEL_PLAIN, SEL_TRANSFER, SEL_APPROVE, SEL_TRANSFER_FROM = 0, 1, 2, 3

# mix: plain, transfer, approve, transferFrom, generic call, create
SCENARIOS = {
    # calibrated to the real corpus' shape (Phase 6): P1 ~5x parallelism from
    # a dominant token (USDT-like, ~15% of all txs), P3 ~40-60x, limited by a
    # hot exchange wallet's nonce chain (~1.5% of txs) and generic calls
    "eth-like": dict(accounts=400_000, tokens=3_000, contracts=20_000, zipf=1.0,
                     token_zipf=1.5, contract_zipf=0.8, sender_zipf=0.7, hot=0.015,
                     undecoded=0.03, mix=(0.28, 0.30, 0.04, 0.03, 0.345, 0.005)),
    "uniform-low": dict(accounts=1_000_000, tokens=20_000, contracts=50_000, zipf=0.0,
                        sender_zipf=0.0, hot=0.0, undecoded=0.0,
                        mix=(0.30, 0.35, 0.05, 0.05, 0.25, 0.0)),
    "uniform-high": dict(accounts=2_000, tokens=50, contracts=100, zipf=0.0,
                         sender_zipf=0.0, hot=0.0, undecoded=0.0,
                         mix=(0.30, 0.35, 0.05, 0.05, 0.25, 0.0)),
    "zipf-low": dict(accounts=1_000_000, tokens=20_000, contracts=50_000, zipf=1.2,
                     sender_zipf=0.6, hot=0.0, undecoded=0.0,
                     mix=(0.30, 0.35, 0.05, 0.05, 0.25, 0.0)),
    "zipf-high": dict(accounts=20_000, tokens=200, contracts=300, zipf=1.3,
                      sender_zipf=0.8, hot=0.03, undecoded=0.0,
                      mix=(0.30, 0.35, 0.05, 0.05, 0.25, 0.0)),
}


class Zipf:
    """Finite Zipf over ranks 0..N-1 (p_k ~ (k+1)^-s, s = 0 is uniform),
    mapped through a random permutation so popular ids are scattered."""

    def __init__(self, rng, N, s, base):
        self.N, self.base = N, base
        w = np.ones(N) if s == 0 else 1.0 / np.arange(1, N + 1) ** s
        self.cdf = np.cumsum(w / w.sum())
        self.perm = rng.permutation(N).astype(np.uint32)
        self.rng = rng

    def __call__(self, k):
        r = np.searchsorted(self.cdf, self.rng.random(k), side="right")
        r = np.minimum(r, self.N - 1)
        return self.perm[r] + np.uint32(self.base)


def generate(name, cfg, n, day_id, first_block, rng):
    A, T, C = cfg["accounts"], cfg["tokens"], cfg["contracts"]
    eoa = Zipf(rng, A, cfg["zipf"], 0)
    sender = Zipf(rng, A, cfg["sender_zipf"], 0)
    token = Zipf(rng, T, cfg.get("token_zipf", cfg["zipf"]), A)
    contract = Zipf(rng, C, cfg.get("contract_zipf", cfg["zipf"]), A + T)
    gsel = Zipf(rng, GENERIC_SELECTORS, 1.0, 4)
    HOT = np.uint32(A + T + C)          # the exchange hot wallet
    assert A + T + C + 1 < (1 << 21) - 1, "ids exceed MAX_ID"

    tx = np.zeros(n, DTYPE)
    kind = rng.choice(6, size=n, p=np.array(cfg["mix"]) / sum(cfg["mix"]))
    frm = sender(n)
    if cfg["hot"] > 0:
        frm[rng.random(n) < cfg["hot"]] = HOT
    to = np.full(n, ABSENT, np.uint32)
    a1 = np.full(n, ABSENT, np.uint32)
    a2 = np.full(n, ABSENT, np.uint32)
    sel = np.zeros(n, np.uint16)
    flags = np.full(n, FLAG_DECODED, np.uint8)
    gas = np.zeros(n, np.uint32)

    m = kind == 0                                   # plain ETH transfer
    to[m] = eoa(m.sum()); sel[m] = SEL_PLAIN; gas[m] = 21000
    m = kind == 1                                   # ERC-20 transfer(to, amount)
    k = m.sum()
    to[m] = token(k); a1[m] = eoa(k); sel[m] = SEL_TRANSFER
    gas[m] = rng.integers(34_000, 65_000, k)
    m = kind == 2                                   # approve(spender, amount)
    k = m.sum()
    to[m] = token(k); a1[m] = contract(k); sel[m] = SEL_APPROVE
    gas[m] = rng.integers(44_000, 48_000, k)
    m = kind == 3                                   # transferFrom(from, to, amount)
    k = m.sum()
    to[m] = token(k); a1[m] = eoa(k); a2[m] = eoa(k); sel[m] = SEL_TRANSFER_FROM
    gas[m] = rng.integers(50_000, 80_000, k)
    m = kind == 4                                   # generic contract call
    k = m.sum()
    to[m] = contract(k); sel[m] = gsel(k)
    gas[m] = np.clip(rng.lognormal(np.log(120_000), 0.8, k), 21_000, 3_000_000)
    m = kind == 5                                   # contract creation
    k = m.sum()
    sel[m] = gsel(k); flags[m] = FLAG_DECODED | FLAG_CREATE
    gas[m] = rng.integers(300_000, 3_000_000, k)
    if cfg["undecoded"] > 0:                        # calldata missing -> fallback
        m = (kind == 1) & (rng.random(n) < cfg["undecoded"] / 0.27)
        flags[m] = 0

    # blocks of ~200 transactions
    sizes = []
    left = n
    while left > 0:
        s = int(np.clip(rng.normal(200, 60), 50, 500))
        sizes.append(min(s, left))
        left -= sizes[-1]
    blk = np.repeat(np.arange(len(sizes), dtype=np.uint32) + np.uint32(first_block), sizes)
    idx = np.concatenate([np.arange(s, dtype=np.uint16) for s in sizes])

    tx["block"] = blk
    tx["idx"] = idx
    tx["sel"] = sel
    tx["frm"] = frm
    tx["to"] = to
    tx["a1"] = a1
    tx["a2"] = a2
    tx["value_gwei"] = rng.integers(0, 10**9, n)
    tx["gas"] = gas
    tx["status"] = (rng.random(n) > 0.02).astype(np.uint8)
    tx["ttype"] = 2
    tx["flags"] = flags
    tx["day"] = day_id
    return tx, sizes, A + T + C + 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default="corpus_synth")
    ap.add_argument("--scenarios", default=",".join(SCENARIOS))
    ap.add_argument("--tx", type=int, default=220_000, help="transactions per scenario")
    ap.add_argument("--targets", default=",".join(str(t) for t in TARGETS))
    ap.add_argument("--seed", type=int, default=20261101)
    args = ap.parse_args()

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(args.seed)
    targets = tuple(int(t) for t in args.targets.split(","))
    names = args.scenarios.split(",")

    meta = {"days": [], "record_bytes": DTYPE.itemsize, "synthetic": True, "seed": args.seed}
    total = 0
    block_no = 1_000_000
    max_ids = 0
    with open(out / "tx.bin", "wb") as tf, open(out / "blocks.bin", "wb") as bf:
        for day_id, name in enumerate(names):
            cfg = SCENARIOS[name]
            tx, sizes, ids = generate(name, cfg, args.tx, day_id, block_no, rng)
            max_ids = max(max_ids, ids)
            tx.tofile(tf)
            starts = total + np.concatenate([[0], np.cumsum(sizes)[:-1]])
            b = np.zeros(len(sizes), BLOCK_DTYPE)
            b["block"] = np.arange(len(sizes)) + block_no
            b["start"] = starts
            b["count"] = sizes
            b.tofile(bf)
            meta["days"].append({"date": "synth-" + name, "day_id": day_id,
                                 "transactions": len(tx), "blocks": len(sizes),
                                 "first_tx_index": total, "block_min": block_no,
                                 "block_max": block_no + len(sizes) - 1,
                                 "scenario": cfg})
            print(f"  synth-{name:13s} {len(tx):8,} tx  {len(sizes):5,} blocks")
            total += len(tx)
            block_no += len(sizes) + 1000
    meta["total_transactions"] = total
    meta["distinct_addresses"] = max_ids
    meta["distinct_selectors"] = len(SELECTORS)
    (out / "meta.json").write_text(json.dumps(meta, indent=2))
    (out / "selectors.txt").write_text("\n".join(SELECTORS) + "\n")
    with open(out / "addresses.txt", "w") as f:
        for i in range(max_ids):
            f.write("0x%040x\n" % (0xC0FFEE0000 + i))
    manifest = build_manifest(corpus=out, targets=targets)
    (out / "superblocks.json").write_text(dumps(manifest))
    print(f"wrote {out}/ ({total:,} tx) and {out}/superblocks.json "
          f"({len(manifest['superblocks'])} super-blocks)")


if __name__ == "__main__":
    main()
