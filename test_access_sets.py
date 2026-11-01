#!/usr/bin/env python3
"""Phase 2 unit tests: hand-built transactions with obvious access sets.
Run: python3 -m unittest test_access_sets -v"""
import unittest

import numpy as np

from access_sets import (
    ABSENT, COMMUTATIVE, DTYPE, FLAG_CREATE, FLAG_DECODED, FLAG_TRUNC, READ,
    WRITE, MAX_ID, Selectors, account_key, allowance_key, decode_key,
    eth_bal_key, infer, nonce_key, token_bal_key,
)

SELS = Selectors(["0x", "0xa9059cbb", "0x095ea7b3", "0x23b872dd", "0xdeadbeef"])
PLAIN, TRANSFER, APPROVE, XFROM, OTHER = range(5)

ALICE, BOB, CAROL, USDT, ROUTER = 10, 11, 12, 20, 30


def make(*rows):
    """rows: dicts with sel, frm, to and optional a1, a2, flags."""
    tx = np.zeros(len(rows), DTYPE)
    for i, r in enumerate(rows):
        tx[i]["block"] = 1
        tx[i]["idx"] = i
        tx[i]["sel"] = r["sel"]
        tx[i]["frm"] = r["frm"]
        tx[i]["to"] = r.get("to", ABSENT)
        tx[i]["a1"] = r.get("a1", ABSENT)
        tx[i]["a2"] = r.get("a2", ABSENT)
        tx[i]["flags"] = r.get("flags", 0)
        tx[i]["status"] = 1
    return tx


def access_set(tx, policy, i=0):
    return set(infer(tx, policy, SELS)[i])


def p1(frm, to):
    return {(int(account_key(frm)), WRITE), (int(account_key(to)), WRITE)}


class PlainTransfer(unittest.TestCase):
    tx = make(dict(sel=PLAIN, frm=ALICE, to=BOB))

    def test_p1(self):
        self.assertEqual(access_set(self.tx, "P1"), p1(ALICE, BOB))

    def test_p2(self):
        self.assertEqual(access_set(self.tx, "P2"), {
            (int(nonce_key(ALICE)), WRITE),
            (int(eth_bal_key(ALICE)), WRITE),
            (int(account_key(ALICE)), READ),
            (int(eth_bal_key(BOB)), WRITE),
            (int(account_key(BOB)), READ),
        })

    def test_p3(self):
        self.assertEqual(access_set(self.tx, "P3"), {
            (int(nonce_key(ALICE)), WRITE),
            (int(eth_bal_key(ALICE)), COMMUTATIVE),
            (int(account_key(ALICE)), READ),
            (int(eth_bal_key(BOB)), COMMUTATIVE),
            (int(account_key(BOB)), READ),
        })


class UsdtTransfer(unittest.TestCase):
    tx = make(dict(sel=TRANSFER, frm=ALICE, to=USDT, a1=BOB, flags=FLAG_DECODED))

    def test_p1_is_whole_contract(self):
        self.assertEqual(access_set(self.tx, "P1"), p1(ALICE, USDT))

    def test_p2_writes_two_balance_slots(self):
        self.assertEqual(access_set(self.tx, "P2"), {
            (int(nonce_key(ALICE)), WRITE),
            (int(eth_bal_key(ALICE)), WRITE),
            (int(account_key(ALICE)), READ),
            (int(account_key(USDT)), READ),
            (int(token_bal_key(USDT, ALICE)), WRITE),
            (int(token_bal_key(USDT, BOB)), WRITE),
        })

    def test_p3_balances_commutative(self):
        got = dict(access_set(self.tx, "P3"))
        self.assertEqual(got[int(token_bal_key(USDT, ALICE))], COMMUTATIVE)
        self.assertEqual(got[int(token_bal_key(USDT, BOB))], COMMUTATIVE)
        self.assertEqual(got[int(nonce_key(ALICE))], WRITE)

    def test_truncated_amount_still_decoded(self):
        tx = make(dict(sel=TRANSFER, frm=ALICE, to=USDT, a1=BOB,
                       flags=FLAG_DECODED | FLAG_TRUNC))
        sets = infer(tx, "P2", SELS)
        self.assertFalse(sets.fallback[0])
        self.assertEqual(set(sets[0]), access_set(self.tx, "P2"))


class Approve(unittest.TestCase):
    def test_p3_allowance_stays_write(self):
        tx = make(dict(sel=APPROVE, frm=ALICE, to=USDT, a1=ROUTER, flags=FLAG_DECODED))
        got = access_set(tx, "P3")
        self.assertIn((int(allowance_key(USDT, ALICE, ROUTER)), WRITE), got)
        self.assertIn((int(account_key(USDT)), READ), got)
        self.assertEqual(len(got), 5)


class TransferFrom(unittest.TestCase):
    # ROUTER moves ALICE's tokens to BOB using ALICE's allowance for ROUTER
    tx = make(dict(sel=XFROM, frm=ROUTER, to=USDT, a1=ALICE, a2=BOB, flags=FLAG_DECODED))

    def test_p2(self):
        self.assertEqual(access_set(self.tx, "P2"), {
            (int(nonce_key(ROUTER)), WRITE),
            (int(eth_bal_key(ROUTER)), WRITE),
            (int(account_key(ROUTER)), READ),
            (int(account_key(USDT)), READ),
            (int(token_bal_key(USDT, ALICE)), WRITE),
            (int(token_bal_key(USDT, BOB)), WRITE),
            (int(allowance_key(USDT, ALICE, ROUTER)), WRITE),
        })

    def test_p3(self):
        got = dict(access_set(self.tx, "P3"))
        self.assertEqual(got[int(token_bal_key(USDT, ALICE))], COMMUTATIVE)
        self.assertEqual(got[int(token_bal_key(USDT, BOB))], COMMUTATIVE)
        self.assertEqual(got[int(allowance_key(USDT, ALICE, ROUTER))], WRITE)

    def test_missing_destination_falls_back(self):
        tx = make(dict(sel=XFROM, frm=ROUTER, to=USDT, a1=ALICE, flags=FLAG_DECODED))
        sets = infer(tx, "P2", SELS)
        self.assertTrue(sets.fallback[0])
        self.assertEqual(set(sets[0]), p1(ROUTER, USDT))


class Undecodable(unittest.TestCase):
    def test_unknown_selector_falls_back_to_p1(self):
        tx = make(dict(sel=OTHER, frm=ALICE, to=ROUTER))
        for policy in ("P2", "P3"):
            sets = infer(tx, policy, SELS)
            self.assertTrue(sets.fallback[0])
            self.assertEqual(set(sets[0]), p1(ALICE, ROUTER))

    def test_transfer_without_calldata_falls_back(self):
        tx = make(dict(sel=TRANSFER, frm=ALICE, to=USDT))
        self.assertEqual(access_set(tx, "P2"), p1(ALICE, USDT))

    def test_contract_creation_falls_back(self):
        tx = make(dict(sel=PLAIN, frm=ALICE, to=ROUTER, flags=FLAG_CREATE))
        self.assertEqual(access_set(tx, "P3"), p1(ALICE, ROUTER))

    def test_fallback_conflicts_with_decoded_slots(self):
        # parent READ on ACCOUNT(USDT) vs fallback WRITE on the same key
        tx = make(dict(sel=TRANSFER, frm=ALICE, to=USDT, a1=BOB, flags=FLAG_DECODED),
                  dict(sel=OTHER, frm=CAROL, to=USDT))
        sets = infer(tx, "P3", SELS)
        k = int(account_key(USDT))
        self.assertEqual(dict(sets[0])[k], READ)
        self.assertEqual(dict(sets[1])[k], WRITE)


class SelfTransfer(unittest.TestCase):
    def test_plain_to_self(self):
        tx = make(dict(sel=PLAIN, frm=ALICE, to=ALICE))
        self.assertEqual(access_set(tx, "P1"), {(int(account_key(ALICE)), WRITE)})
        self.assertEqual(access_set(tx, "P3"), {
            (int(nonce_key(ALICE)), WRITE),
            (int(eth_bal_key(ALICE)), COMMUTATIVE),
            (int(account_key(ALICE)), READ),
        })

    def test_token_to_self(self):
        tx = make(dict(sel=TRANSFER, frm=ALICE, to=USDT, a1=ALICE, flags=FLAG_DECODED))
        for policy, mode in (("P2", WRITE), ("P3", COMMUTATIVE)):
            got = infer(tx, policy, SELS)[0]
            ks = [k for k, _ in got]
            self.assertEqual(len(ks), len(set(ks)))
            self.assertIn((int(token_bal_key(USDT, ALICE)), mode), got)
            self.assertEqual(len(got), 5)


class Keys(unittest.TestCase):
    def test_collision_free(self):
        rng = np.random.default_rng(0)
        ids = rng.integers(0, MAX_ID + 1, size=(20000, 3), dtype=np.uint64)
        ids[:5] = [[0, 0, 0], [MAX_ID, MAX_ID, MAX_ID], [0, 0, MAX_ID],
                   [MAX_ID, 0, 0], [1, 1, 1]]
        a, b, c = ids.T
        families = [account_key(a), nonce_key(a), eth_bal_key(a),
                    token_bal_key(a, b), allowance_key(a, b, c)]
        tuples = [{(int(x),) for x in a}] * 3 + [
            {(int(x), int(y)) for x, y in zip(a, b)},
            {tuple(int(v) for v in r) for r in ids}]
        allkeys = np.concatenate([np.unique(f) for f in families])
        self.assertEqual(len(allkeys), len(np.unique(allkeys)))
        self.assertEqual(len(allkeys), sum(len(t) for t in tuples))

    def test_decode_roundtrip(self):
        self.assertEqual(decode_key(allowance_key(MAX_ID, 5, 7)),
                         ("ALLOWANCE", (MAX_ID, 5, 7)))
        self.assertEqual(decode_key(token_bal_key(3, MAX_ID)),
                         ("TOKEN_BAL", (3, MAX_ID)))
        self.assertEqual(decode_key(nonce_key(MAX_ID)), ("NONCE", (MAX_ID,)))

    def test_oversized_id_rejected(self):
        tx = make(dict(sel=PLAIN, frm=MAX_ID + 1, to=BOB))
        with self.assertRaises(ValueError):
            infer(tx, "P1", SELS)


class Determinism(unittest.TestCase):
    def test_same_input_same_output(self):
        tx = make(dict(sel=PLAIN, frm=ALICE, to=BOB),
                  dict(sel=XFROM, frm=ROUTER, to=USDT, a1=ALICE, a2=BOB, flags=FLAG_DECODED),
                  dict(sel=OTHER, frm=CAROL, to=ROUTER))
        for policy in ("P1", "P2", "P3"):
            x, y = infer(tx, policy, SELS), infer(tx.copy(), policy, SELS)
            np.testing.assert_array_equal(x.offsets, y.offsets)
            np.testing.assert_array_equal(x.keys, y.keys)
            np.testing.assert_array_equal(x.modes, y.modes)
            for i in range(len(x)):   # keys ascending, unique within each tx
                ks = x.keys[x.offsets[i]:x.offsets[i + 1]]
                self.assertTrue(np.all(ks[1:] > ks[:-1]))


if __name__ == "__main__":
    unittest.main()
