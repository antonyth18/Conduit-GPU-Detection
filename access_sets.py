#!/usr/bin/env python3
"""
Phase 2 - Read/write-set inference.

Ethereum does not publish which state a transaction touches, so we infer it.
Three policies, one interface: infer(tx, policy, sels) returns an AccessSets
holding, for every transaction, a list of (state_key, mode) records.

State keys are u64, built from interned ids by a prefix code, so distinct
state can never share a key (no hashing). Every id must be <= MAX_ID; the
all-ones 21-bit id is reserved so no valid key equals SENTINEL.

  bit 63 = 1  ALLOWANCE      [62:42] token  [41:21] owner  [20:0] spender
  bit 63 = 0  [62:60] kind   [59:42] zero   [41:21] a      [20:0] b
     kind 0   ACCOUNT(a)                    whole account, b = 0
     kind 1   NONCE(a)                      b = 0
     kind 2   ETH_BAL(a)                    b = 0
     kind 3   TOKEN_BAL(a = token, b = holder)

Policies:
  P1 CONTRACT     W ACCOUNT(sender), W ACCOUNT(to). No calldata.
  P2 SLOT         decoded calldata -> slot keys, everything else falls back
                  to P1. Each slot access also READs its parent ACCOUNT, so a
                  fallback WRITE of ACCOUNT(token) still conflicts with every
                  slot of that token while decoded calls stay mutually free.
  P3 COMMUTATIVE  P2 keys; balance arithmetic (token balances, ETH transfers,
                  gas) is COMMUTATIVE instead of WRITE.

Output is CSR: offsets[i]:offsets[i+1] indexes keys/modes for transaction i,
with keys ascending within each transaction and each key at most once.
"""
import json
from pathlib import Path

import numpy as np

CORPUS = Path(__file__).parent / "corpus"
ABSENT = 0xFFFFFFFF

DTYPE = np.dtype([
    ("block", "<u4"), ("idx", "<u2"), ("sel", "<u2"),
    ("frm", "<u4"), ("to", "<u4"), ("a1", "<u4"), ("a2", "<u4"),
    ("value_gwei", "<u8"), ("gas", "<u4"),
    ("status", "u1"), ("ttype", "u1"), ("flags", "u1"), ("day", "u1"),
])
assert DTYPE.itemsize == 40

FLAG_DECODED = 1
FLAG_TRUNC = 2
FLAG_MALFORMED = 4
FLAG_CREATE = 8

# Access modes are bit flags so duplicate accesses within one transaction can
# be OR-ed together and then resolved: any WRITE, or READ plus COMMUTATIVE
# (reading a value that is being incremented), becomes WRITE.
READ = 1
COMMUTATIVE = 2
WRITE = 4
MODE_NAMES = {READ: "R", COMMUTATIVE: "C", WRITE: "W"}

ID_BITS = 21
MAX_ID = (1 << ID_BITS) - 2
SENTINEL = np.uint64(0xFFFFFFFFFFFFFFFF)

KIND_ACCOUNT = 0
KIND_NONCE = 1
KIND_ETH_BAL = 2
KIND_TOKEN_BAL = 3
KIND_NAMES = {KIND_ACCOUNT: "ACCOUNT", KIND_NONCE: "NONCE",
              KIND_ETH_BAL: "ETH_BAL", KIND_TOKEN_BAL: "TOKEN_BAL"}

# Transaction categories under P2/P3.
CAT_FALLBACK = 0
CAT_PLAIN = 1
CAT_TRANSFER = 2
CAT_APPROVE = 3
CAT_TRANSFER_FROM = 4

POLICIES = ("P1", "P2", "P3")

KNOWN = {
    "dac17f958d2ee523a2206206994597c13d831ec7": "USDT",
    "a0b86991c6218b36c1d19d4a2e9eb0ce3606eb48": "USDC",
    "c02aaa39b223fe8d0a0e5c4f27ead9083c756cc2": "WETH",
}


class Selectors:
    """Interned ids of the selectors the policies care about; -1 if absent."""

    def __init__(self, names):
        idx = {s: i for i, s in enumerate(names)}
        self.plain = idx.get("0x", -1)
        self.transfer = idx.get("0xa9059cbb", -1)
        self.approve = idx.get("0x095ea7b3", -1)
        self.transfer_from = idx.get("0x23b872dd", -1)


class AccessSets:
    def __init__(self, offsets, keys, modes, category):
        self.offsets = offsets      # int64, len n + 1
        self.keys = keys            # uint64
        self.modes = modes          # uint8, one of READ / COMMUTATIVE / WRITE
        self.category = category    # uint8 CAT_*, len n
        self.fallback = category == CAT_FALLBACK

    def __len__(self):
        return len(self.offsets) - 1

    def __getitem__(self, i):
        s, e = self.offsets[i], self.offsets[i + 1]
        return [(int(k), int(m)) for k, m in zip(self.keys[s:e], self.modes[s:e])]

    def sizes(self):
        return np.diff(self.offsets)


# ---- key derivation --------------------------------------------------------

def _u64(a):
    return np.asarray(a, dtype=np.uint64)


def kind_key(kind, a, b=0):
    return (np.uint64(kind) << np.uint64(60)) | (_u64(a) << np.uint64(21)) | _u64(b)


def account_key(a):
    return kind_key(KIND_ACCOUNT, a)


def nonce_key(a):
    return kind_key(KIND_NONCE, a)


def eth_bal_key(a):
    return kind_key(KIND_ETH_BAL, a)


def token_bal_key(token, holder):
    return kind_key(KIND_TOKEN_BAL, token, holder)


def allowance_key(token, owner, spender):
    return (np.uint64(1 << 63) | (_u64(token) << np.uint64(42))
            | (_u64(owner) << np.uint64(21)) | _u64(spender))


def decode_key(k):
    """key -> (kind name, tuple of ids)."""
    k = int(k)
    m = (1 << ID_BITS) - 1
    if k >> 63:
        return "ALLOWANCE", ((k >> 42) & m, (k >> 21) & m, k & m)
    kind = (k >> 60) & 7
    a, b = (k >> 21) & m, k & m
    if kind == KIND_TOKEN_BAL:
        return KIND_NAMES[kind], (a, b)
    return KIND_NAMES[kind], (a,)


def label_key(k, addrs):
    kind, ids = decode_key(k)
    parts = []
    for i in ids:
        a = addrs[i]
        parts.append(KNOWN.get(a, "0x" + a))
    return f"{kind}({', '.join(parts)})"


# ---- policies --------------------------------------------------------------

def check_ids(tx):
    for f in ("frm", "to", "a1", "a2"):
        col = tx[f]
        col = col[col != ABSENT]
        if len(col) and int(col.max()) > MAX_ID:
            raise ValueError(f"{f} id {int(col.max())} exceeds {MAX_ID}; "
                             "the key layout needs a wider id field")


def classify(tx, sels):
    """Category per transaction. Truncation only cuts off the amount (arg3),
    which no key uses, so truncated-but-decoded calls are not fallbacks."""
    s = tx["sel"].astype(np.int64)
    flags = tx["flags"]
    create = (flags & FLAG_CREATE) != 0
    ends = (tx["frm"] != ABSENT) & (tx["to"] != ABSENT)
    ok = ((flags & FLAG_DECODED) != 0) & ~create & ends
    has1 = tx["a1"] != ABSENT
    has2 = tx["a2"] != ABSENT

    cat = np.full(len(tx), CAT_FALLBACK, np.uint8)
    cat[(s == sels.plain) & ~create & ends] = CAT_PLAIN
    cat[ok & (s == sels.transfer) & has1] = CAT_TRANSFER
    cat[ok & (s == sels.approve) & has1] = CAT_APPROVE
    cat[ok & (s == sels.transfer_from) & has1 & has2] = CAT_TRANSFER_FROM
    return cat


def infer(tx, policy, sels):
    """Access sets for a slice of tx records. Pure: depends only on its
    arguments, and the output order is fixed (keys ascending per tx)."""
    if policy not in POLICIES:
        raise ValueError(f"unknown policy {policy!r}")
    check_ids(tx)
    n = len(tx)
    frm, to, a1, a2 = (tx[f].astype(np.uint64) for f in ("frm", "to", "a1", "a2"))
    has_frm = tx["frm"] != ABSENT
    has_to = tx["to"] != ABSENT

    if policy == "P1":
        cat = np.full(n, CAT_FALLBACK, np.uint8)
        keys = np.stack([account_key(frm), account_key(to)], axis=1)
        modes = np.full((n, 2), WRITE, np.uint8)
        valid = np.stack([has_frm, has_to], axis=1)
        offsets, k, m = _compact(keys, modes, valid)
        return AccessSets(offsets, k, m, cat)

    cat = classify(tx, sels)
    bal = COMMUTATIVE if policy == "P3" else WRITE
    fb = cat == CAT_FALLBACK
    dec = ~fb
    plain = cat == CAT_PLAIN
    xfer = cat == CAT_TRANSFER
    appr = cat == CAT_APPROVE
    xfrom = cat == CAT_TRANSFER_FROM

    S = 7
    keys = np.zeros((n, S), np.uint64)
    modes = np.zeros((n, S), np.uint8)
    valid = np.zeros((n, S), bool)

    def put(col, mask, key, mode):
        keys[mask, col] = key[mask]
        modes[mask, col] = mode
        valid[mask, col] = True

    # sender: nonce + gas payment, or the whole account on fallback
    put(0, dec & has_frm, nonce_key(frm), WRITE)
    put(1, dec & has_frm, eth_bal_key(frm), bal)
    put(2, dec & has_frm, account_key(frm), READ)
    put(2, fb & has_frm, account_key(frm), WRITE)
    # to: parent of the recipient's ETH balance or the token's slots
    put(3, dec & has_to, account_key(to), READ)
    put(3, fb & has_to, account_key(to), WRITE)
    # slots; to is the token contract for the three ERC-20 calls
    put(4, plain, eth_bal_key(to), bal)
    put(4, xfer, token_bal_key(to, frm), bal)
    put(5, xfer, token_bal_key(to, a1), bal)
    put(4, appr, allowance_key(to, frm, a1), WRITE)
    put(4, xfrom, token_bal_key(to, a1), bal)
    put(5, xfrom, token_bal_key(to, a2), bal)
    put(6, xfrom, allowance_key(to, a1, frm), WRITE)

    offsets, k, m = _compact(keys, modes, valid)
    return AccessSets(offsets, k, m, cat)


def _compact(keys, modes, valid):
    """(n, S) candidate slots -> CSR with keys sorted and deduplicated per row,
    duplicate modes merged."""
    keys = np.where(valid, keys, SENTINEL)
    modes = np.where(valid, modes, 0).astype(np.uint8)
    order = np.argsort(keys, axis=1, kind="stable")
    keys = np.take_along_axis(keys, order, axis=1)
    modes = np.take_along_axis(modes, order, axis=1)
    del order

    first = keys != SENTINEL
    first[:, 1:] &= keys[:, 1:] != keys[:, :-1]
    counts = first.sum(axis=1)
    starts = np.flatnonzero(first.ravel())
    flat_k = keys.ravel()[starts]
    if len(starts):
        # Slots between two starts are duplicates of the first key or invalid
        # padding with mode 0, so OR-reducing over them is exact.
        merged = np.bitwise_or.reduceat(modes.ravel(), starts)
    else:
        merged = np.zeros(0, np.uint8)

    write = ((merged & WRITE) != 0) | (((merged & READ) != 0)
                                       & ((merged & COMMUTATIVE) != 0))
    out = np.where(write, WRITE,
                   np.where((merged & COMMUTATIVE) != 0, COMMUTATIVE, READ))
    offsets = np.zeros(len(keys) + 1, np.int64)
    np.cumsum(counts, out=offsets[1:])
    return offsets, flat_k, out.astype(np.uint8)


# ---- corpus ----------------------------------------------------------------

def load_corpus(path=CORPUS):
    tx = np.fromfile(path / "tx.bin", dtype=DTYPE)
    meta = json.load(open(path / "meta.json"))
    addrs = open(path / "addresses.txt").read().split("\n")[:-1]
    sels = open(path / "selectors.txt").read().split("\n")[:-1]
    return tx, meta, addrs, sels
