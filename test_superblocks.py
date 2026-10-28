#!/usr/bin/env python3
"""Phase 3 unit tests: tiling on synthetic blocks, then the invariants on the
real manifest. Run: python3 -m unittest test_superblocks -v"""
import json
import tempfile
import unittest
from pathlib import Path

import numpy as np

from access_sets import CORPUS, DTYPE
from superblocks import (
    BLOCK_DTYPE, MANIFEST, SuperBlocks, assemble, build_manifest, dumps, tile,
)


def synthetic(counts, senders=None):
    """One day with the given tx-per-block counts. Senders default to 0."""
    n = int(sum(counts))
    tx = np.zeros(n, DTYPE)
    blocks = np.zeros(len(counts), BLOCK_DTYPE)
    pos = 0
    for b, c in enumerate(counts):
        blocks[b] = (100 + b, pos, c)
        tx["block"][pos:pos + c] = 100 + b
        tx["idx"][pos:pos + c] = np.arange(c)
        pos += c
    if senders is not None:
        tx["frm"] = senders
    days = [{"date": "d0", "day_id": 0, "blocks": len(counts)}]
    return tx, blocks, days


class Tile(unittest.TestCase):
    def test_greedy_closes_at_first_block_meeting_target(self):
        self.assertEqual(tile(np.array([3, 3, 3, 3, 3]), 5), [(0, 2), (2, 4)])

    def test_exact_hit_closes_immediately(self):
        self.assertEqual(tile(np.array([5, 5, 5]), 5), [(0, 1), (1, 2), (2, 3)])

    def test_tail_below_target_dropped(self):
        self.assertEqual(tile(np.array([4, 4, 1]), 8), [(0, 2)])
        self.assertEqual(tile(np.array([4, 3]), 8), [])

    def test_single_block_over_target(self):
        self.assertEqual(tile(np.array([2, 50, 2]), 10), [(0, 2)])


class AssembleSynthetic(unittest.TestCase):
    def test_entries_cover_whole_blocks_in_order(self):
        rng = np.random.default_rng(1)
        counts = rng.integers(1, 40, size=300)
        tx, blocks, days = synthetic(counts)
        configs, entries = assemble(tx, blocks, days, targets=(50, 200))
        for c in configs:
            es = [e for e in entries if e["target"] == c["target"]]
            used = sum(e["tx_count"] for e in es)
            self.assertEqual(used + c["dropped_tail_tx"], counts.sum())
            self.assertLess(c["dropped_tail_tx"], c["target"])
            for e in es:
                self.assertGreaterEqual(e["tx_count"], e["target"])
                bi = e["block_index_start"]
                self.assertEqual(e["tx_start"], blocks["start"][bi])
                self.assertEqual(e["tx_count"],
                                 blocks["count"][bi:bi + e["block_count"]].sum())

    def test_max_sender(self):
        senders = np.array([7, 7, 7, 1, 2, 7, 1, 1, 3, 3], np.uint32)
        tx, blocks, days = synthetic([5, 5], senders)
        _, entries = assemble(tx, blocks, days, targets=(10,))
        self.assertEqual(entries[0]["max_sender_tx"], 4)

    def test_missing_blocks_counted(self):
        tx, blocks, days = synthetic([5, 5, 5])
        blocks["block"][2] += 3
        _, entries = assemble(tx, blocks, days, targets=(15,))
        self.assertEqual(entries[0]["missing_blocks"], 3)


@unittest.skipUnless((CORPUS / "tx.bin").exists() and MANIFEST.exists(),
                     "needs corpus/ and superblocks.json")
class RealManifest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.sb = SuperBlocks()
        cls.entries = cls.sb.manifest["superblocks"]
        cls.blocks = np.fromfile(CORPUS / "blocks.bin", dtype=BLOCK_DTYPE)

    def test_whole_blocks_never_split(self):
        tx = self.sb.tx
        for e in self.entries:
            s, n = e["tx_start"], e["tx_count"]
            if s > 0:
                self.assertNotEqual(tx["block"][s - 1], tx["block"][s], e["id"])
            if s + n < len(tx):
                self.assertNotEqual(tx["block"][s + n - 1], tx["block"][s + n], e["id"])
            bi = e["block_index_start"]
            seg = self.blocks[bi:bi + e["block_count"]]
            self.assertEqual(int(seg["count"].sum()), n, e["id"])
            self.assertEqual(int(seg["block"][0]), e["block_first"])
            self.assertEqual(int(seg["block"][-1]), e["block_last"])

    def test_samples_disjoint_within_config(self):
        groups = {}
        for e in self.entries:
            groups.setdefault((e["date"], e["target"]), []).append(e)
        for key, es in groups.items():
            es = sorted(es, key=lambda e: e["tx_start"])
            for a, b in zip(es, es[1:]):
                self.assertLessEqual(a["tx_start"] + a["tx_count"], b["tx_start"], key)

    def test_ordering_preserved_and_single_day(self):
        for e in self.entries:
            seg = self.sb.load(e)
            key = seg["block"].astype(np.int64) * 100000 + seg["idx"]
            self.assertTrue(bool(np.all(np.diff(key) > 0)), e["id"])
            self.assertTrue(bool(np.all(seg["day"] == e["day_id"])), e["id"])

    def test_rebuild_is_byte_identical(self):
        self.assertEqual(dumps(build_manifest()), MANIFEST.read_text())

    def test_rejects_manifest_from_other_corpus(self):
        m = json.loads(MANIFEST.read_text())
        m["corpus"]["tx_bin_sha256"] = "0" * 64
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "m.json"
            p.write_text(json.dumps(m))
            with self.assertRaises(ValueError):
                SuperBlocks(p)


if __name__ == "__main__":
    unittest.main()
