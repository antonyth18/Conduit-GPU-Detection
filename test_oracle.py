#!/usr/bin/env python3
"""Phase 4 unit tests: hand-built access sets whose graphs are obvious, broken
schedules the validator must reject, and the comparison harness verdicts.
Run: python3 -m unittest test_oracle -v"""
import unittest

import numpy as np

from access_sets import COMMUTATIVE as C, FLAG_DECODED, READ as R, WRITE as W, infer
from compare import compare
from oracle import (
    access_lists, batch_sizes, conflicts, critical_path, edges, run, validate,
)
from test_access_sets import ALICE, BOB, CAROL, OTHER, PLAIN, SELS, TRANSFER, USDT, make


def graph(access):
    out = run(access)
    return out["edges"], out["levels"]


class ConflictRule(unittest.TestCase):
    def test_full_table(self):
        table = {(R, R): False, (R, C): True, (R, W): True,
                 (C, R): True, (C, C): False, (C, W): True,
                 (W, R): True, (W, C): True, (W, W): True}
        for (a, b), want in table.items():
            self.assertEqual(conflicts(a, b), want, (a, b))

    def test_unknown_mode_rejected(self):
        with self.assertRaises(ValueError):
            conflicts(R, 3)


class HandBuiltGraphs(unittest.TestCase):
    def test_two_independent(self):
        self.assertEqual(graph([[(1, W)], [(2, W)]]), ([], [0, 0]))

    def test_read_read(self):
        self.assertEqual(graph([[(1, R)], [(1, R)]]), ([], [0, 0]))

    def test_read_write(self):
        self.assertEqual(graph([[(1, R)], [(1, W)]]), ([(0, 1)], [0, 1]))

    def test_write_read(self):
        self.assertEqual(graph([[(1, W)], [(1, R)]]), ([(0, 1)], [0, 1]))

    def test_two_commutative(self):
        self.assertEqual(graph([[(1, C)], [(1, C)]]), ([], [0, 0]))

    def test_commutative_and_read(self):
        self.assertEqual(graph([[(1, C)], [(1, R)]]), ([(0, 1)], [0, 1]))

    def test_commutative_and_write(self):
        self.assertEqual(graph([[(1, W)], [(1, C)]]), ([(0, 1)], [0, 1]))

    def test_conflict_needs_shared_key(self):
        # same modes on different keys never conflict
        self.assertEqual(graph([[(1, W), (2, R)], [(3, W), (2, R)]]), ([], [0, 0]))

    def test_three_transaction_chain(self):
        acc = [[(1, W)], [(1, W), (2, W)], [(2, W)]]
        self.assertEqual(graph(acc), ([(0, 1), (1, 2)], [0, 1, 2]))
        self.assertEqual(critical_path([0, 1, 2]), 3)

    def test_conflict_far_earlier(self):
        acc = [[(1, W)]] + [[(100 + i, W)] for i in range(8)] + [[(1, R)]]
        e, lv = graph(acc)
        self.assertEqual(e, [(0, 9)])
        self.assertEqual(lv, [0] * 9 + [1])
        self.assertEqual(batch_sizes(lv), [9, 1])

    def test_longest_path_not_shortest(self):
        # 0->2 directly and 0->1->2: level of 2 is 2, not 1
        acc = [[(1, W), (2, W)], [(1, W), (3, W)], [(2, W), (3, W)]]
        self.assertEqual(graph(acc), ([(0, 1), (0, 2), (1, 2)], [0, 1, 2]))

    def test_duplicate_key_rejected(self):
        with self.assertRaises(ValueError):
            edges([[(1, W), (1, R)]])


class Validator(unittest.TestCase):
    acc = [[(1, W)], [(1, R)], [(2, C)], [(2, C)], [(3, R), (1, R)]]

    def test_accepts_oracle_output(self):
        ok, bad = validate(self.acc, run(self.acc)["levels"])
        self.assertTrue(ok, bad)

    def test_accepts_other_valid_schedule(self):
        # not the oracle's levels, but every conflicting pair is ordered
        ok, _ = validate(self.acc, [0, 1, 5, 5, 3])
        self.assertTrue(ok)

    def test_rejects_conflict_in_same_level(self):
        ok, bad = validate(self.acc, [0, 0, 0, 0, 1])
        self.assertFalse(ok)
        self.assertEqual(bad[0][:2], (0, 1))
        self.assertEqual(bad[0][3], "same level")

    def test_rejects_order_inversion(self):
        ok, bad = validate(self.acc, [1, 0, 0, 0, 2])
        self.assertFalse(ok)
        self.assertEqual(bad[0][3], "order inverted")

    def test_rejects_everything_in_one_level(self):
        ok, bad = validate(self.acc, [0] * 5)
        self.assertFalse(ok)
        self.assertEqual({(i, j) for i, j, _, _ in bad}, {(0, 1), (0, 4)})

    def test_accepts_numpy_levels(self):
        lv = np.array(run(self.acc)["levels"], dtype=np.int32)
        self.assertTrue(validate(self.acc, lv)[0])

    def test_rejects_missing_transaction(self):
        self.assertFalse(validate(self.acc, [0, 1, 0, 0])[0])

    def test_rejects_non_integer_level(self):
        self.assertFalse(validate(self.acc, [0, 1, 0, 0, None])[0])
        self.assertFalse(validate(self.acc, [0, 1, 0, 0, -1])[0])

    def test_limit_counts_the_rest(self):
        acc = [[(1, W)] for _ in range(5)]
        ok, bad = validate(acc, [0] * 5, limit=2)
        self.assertFalse(ok)
        self.assertEqual(len(bad), 3)
        self.assertIn("8 more", bad[-1][3])


class Harness(unittest.TestCase):
    # three writers of one key: oracle has 0->1, 0->2, 1->2
    acc = [[(1, W)], [(1, W)], [(1, W)]]

    def setUp(self):
        self.ref = run(self.acc)

    def test_identical(self):
        self.assertEqual(compare(self.ref, run(self.acc), 3)["verdict"], "identical")

    def test_reduced_edges_same_levels_pass(self):
        cand = {"edges": [(0, 1), (1, 2)], "levels": [0, 1, 2]}
        r = compare(self.ref, cand, 3)
        self.assertEqual(r["verdict"], "reduced")
        self.assertTrue(r["ok"])
        self.assertEqual((r["missing"], r["missing_unimplied"]), (1, 0))

    def test_different_levels_fail(self):
        cand = {"edges": [(0, 1), (1, 2)], "levels": [0, 1, 1]}
        r = compare(self.ref, cand, 3)
        self.assertEqual(r["verdict"], "FAIL levels")
        self.assertEqual(r["first_level_divergence"], (2, 2, 1))

    def test_unimplied_missing_edge_fails(self):
        # levels happen to match but 1 and 2 are left unordered
        cand = {"edges": [(0, 1), (0, 2)], "levels": [0, 1, 2]}
        r = compare(self.ref, cand, 3)
        self.assertEqual(r["verdict"], "FAIL unimplied")
        self.assertEqual(r["first_unimplied"], [(1, 2)])

    def test_spurious_edge_fails(self):
        acc = [[(1, W)], [(2, W)]]
        cand = {"edges": [(0, 1)], "levels": [0, 0]}
        r = compare(run(acc), cand, 2)
        self.assertEqual(r["verdict"], "FAIL spurious")


class OnPhase2Output(unittest.TestCase):
    """The oracle consumes real policy output and sees the nonce chain."""
    tx = make(dict(sel=TRANSFER, frm=ALICE, to=USDT, a1=BOB, flags=FLAG_DECODED),
              dict(sel=TRANSFER, frm=CAROL, to=USDT, a1=BOB, flags=FLAG_DECODED),
              dict(sel=PLAIN, frm=ALICE, to=CAROL),
              dict(sel=OTHER, frm=ALICE, to=USDT))

    def test_policies(self):
        got = {p: run(access_lists(infer(self.tx, p, SELS))) for p in ("P1", "P2", "P3")}
        # P1: everything touches USDT or ALICE or CAROL -> fully ordered chain
        self.assertEqual(got["P1"]["levels"], [0, 1, 2, 3])
        # P2: the two transfers both write BAL(USDT, BOB) -> ordered
        self.assertIn((0, 1), got["P2"]["edges"])
        # P3: those writes commute; tx 0 and 1 run together
        self.assertNotIn((0, 1), got["P3"]["edges"])
        self.assertEqual(got["P3"]["levels"][:2], [0, 0])
        # ALICE's three tx form a nonce chain under every policy
        for p in got:
            lv = got[p]["levels"]
            self.assertLess(lv[0], lv[2], p)
            self.assertLess(lv[2], lv[3], p)
            self.assertTrue(validate(access_lists(infer(self.tx, p, SELS)), lv)[0])


if __name__ == "__main__":
    unittest.main()
