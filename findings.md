# Findings

Running log of results for the Ethereum parallelism study. Each phase appends
its numbers and the caveats that bound them. Figures are reproducible with the
script named in each section.

## Phase 1 — Corpus

Source: BigQuery exports, two days of Ethereum mainnet. Built by `ingest.py`, checked by
`verify_corpus.py`.

| Day | Weekday | Transactions | Blocks | Tx / block | Calldata decoded | Failed (status 0) |
|---|---|---|---|---|---|---|
| 2026-09-01 | Tuesday | 2,013,158 | 7,172 | 281 | 36.0% | 1.2% |
| 2026-09-13 | Sunday | 1,476,230 | 7,178 | 206 | 35.8% | 1.6% |

- 3,489,388 transactions, 1,216,599 distinct addresses, 58,624 distinct selectors.
- Calldata was exported only for `transfer`, `approve` and `transferFrom`; it
  joined to its transaction for 100% of rows.
- Truncated calldata (amount argument cut off): 14,552 rows on Sep 1 and
  21,127 on Sep 13. The address operands are intact in these rows.
- Malformed operands: 30 rows on Sep 1 and 54 on Sep 13.
- Plain ETH transfers are 31.4% of Sep 1 and 28.6% of Sep 13.
- USDT is the `to` address of 19.3% of Sep 1 transactions and 18.1% of Sep 13.

## Phase 2 — Read/write-set inference

Built by `access_sets.py`; numbers from `report_access.py`; tests in
`test_access_sets.py`.

### Policies

- **P1 CONTRACT:** each transaction WRITEs the sender account and the `to` account.
- **P2 SLOT:** decoded calls touch individual slots: nonce, ETH balance, token
  balance, allowance. Each slot access also READs its parent account, so a
  fallback WRITE of a whole contract still conflicts with every slot in it.
  Anything not decoded falls back to P1.
- **P3 COMMUTATIVE:** the same keys as P2, but balance changes (token balances,
  ETH transfers and gas payment) are COMMUTATIVE. Nonces, allowances and
  fallback accesses stay WRITE.

Keys are 64-bit integers built from interned ids by a prefix code, so they are
collision-free without hashing. Each id must fit in 21 bits; 58% of that range
is used now, so a third day of data will likely need a wider allowance key.

### Access-set size and key spread

"W+C" counts only conflict-bearing accesses (WRITE and COMMUTATIVE). For P1,
every access is a WRITE, so its "all" and "W+C" figures are the same.

| Day | Policy | Mean set size | Distinct keys (all) | Distinct keys (W+C) | Top-1 share (W+C) | Top-10 | Top-100 |
|---|---|---|---|---|---|---|---|
| Sep 1 | P1 | 1.99 | 704,167 | 704,167 | 9.69% | 19.42% | 32.97% |
| Sep 1 | P2 / P3 | 4.36 | 2,380,285 | 1,821,438 | 0.66% | 4.63% | 15.56% |
| Sep 13 | P1 | 1.99 | 455,050 | 455,050 | 9.08% | 16.89% | 31.55% |
| Sep 13 | P2 / P3 | 4.26 | 1,521,475 | 1,171,449 | 0.61% | 3.73% | 14.73% |

Accesses per key:

| Day | Policy | Max | p99 | p95 | Median |
|---|---|---|---|---|---|
| Sep 1 | P1 | 388,725 | 27 | 7 | 1 |
| Sep 1 | P2 / P3 (W+C) | 40,031 | 17 | 5 | 1 |
| Sep 13 | P1 | 266,865 | 38 | 8 | 2 |
| Sep 13 | P2 / P3 (W+C) | 26,609 | 22 | 6 | 1 |

P3 has the same keys as P2 and differs only in access modes:

| Day | WRITE | COMMUTATIVE | READ |
|---|---|---|---|
| Sep 1 | 2,734,131 | 3,326,735 | 2,713,169 |
| Sep 13 | 2,050,205 | 2,334,707 | 1,899,082 |

### USDT fragmentation

| Day | P1: ACCOUNT(USDT) | P2: USDT keys | P2: hottest USDT key | Reduction |
|---|---|---|---|---|
| Sep 1 | 19.3% of tx | 254,292 | 1.9% of tx | 10.3x |
| Sep 13 | 18.1% of tx | 170,443 | 1.8% of tx | 10.0x |

- **Use the share of transactions.** As a share of accesses the drop looks
  bigger, 15–16x (9.7% to 0.62% on Sep 1), but P2 makes about twice as many
  accesses per transaction, which inflates that ratio.
- **The brief's "18.8% of accesses" is a share of transactions.** Under P1
  every transaction has two accesses, so USDT holds about 9–10% of accesses.
- **No USDT call falls back.** All 655,590 USDT calls are `transfer`,
  `approve` or `transferFrom`, apart from 13 plain ETH sends.

### Fallback rate

The fallback rate is the share of transactions handled at P1 granularity,
which bounds how much of the result is measured rather than assumed.

| Day | Fallback | Plain ETH | Decoded ERC-20 | Decoded with truncated amount (not fallback) |
|---|---|---|---|---|
| Sep 1 | 32.53% | 31.4% | 36.0% | 14,527 |
| Sep 13 | 35.60% | 28.6% | 35.8% | 21,088 |

Fallbacks are almost entirely generic contract calls such as DEX swaps; real
decode failures are negligible.

### Observations

- **After fragmentation, the hotspots are individual busy senders.** Examples
  are `0x28c6c06298d514db089934071355e5743bf21d60` and
  `0x559432e18b281731c054cd703d4b49872be4ed53`, whose USDT balances, ETH
  balances and nonces top the W+C lists.
- **Nonces cap what P3 can gain.** P3 removes conflicts between balance
  updates, but each sender's nonce stays a WRITE. Under P3 the hottest
  WRITE-only key is the router contract
  `0x68b3465833fb72a70ecdf485e0e4c7bd8665fc45` on Sep 1 (0.42% of accesses)
  and the nonce of `0x5594…ed53` on Sep 13 (0.34%). Expect P3's gain over P2
  to be limited by these same-sender chains.
- **P2 and P3 have identical key sets and differ only in modes**, so any
  difference between them will only appear in Phase 6 conflict detection.

### Caveats

- **Internal calls are invisible.** The data has no execution traces, so a
  swap that moves USDT inside a router shows up only as ACCOUNT(router). All
  policies undercount conflicts, so measured parallelism is an upper bound.
- **Failed transactions keep their full access sets.** These are 1.2–1.6% of
  transactions; a scheduler can't know in advance that they'll fail.
- **Plain ETH sends are assumed to touch only balances.** Sends to contracts
  may run code we can't see.
- **The block's fee recipient is not modelled.** The corpus doesn't include
  it. Under WRITE semantics it would serialise every block.

## Phase 3 — Super-blocks

Built by `superblocks.py`, which writes `superblocks.json`; numbers from
`report_superblocks.py`; tests in `test_superblocks.py`.

**Modelling decision.** Real blocks average 281 transactions (Sep 1) and 206
(Sep 13). The pipeline only becomes interesting at 10k–100k transactions, the
batch size of rollups and L2 sequencers. A super-block is a run of consecutive
real blocks concatenated in their original (block, transaction index) order.
It is a model of a large batch, not an observed block.

**How samples are chosen.** Each day is tiled with whole blocks from its first
block: add blocks until the target transaction count is met, close the
super-block, continue from the next block. The leftover tail that can't reach
the target is dropped. There is no randomness and no seed. Super-blocks are
stored as (start, count) ranges into `corpus/tx.bin`, and the manifest is pinned
to the corpus by `tx.bin`'s SHA-256. The result is 736 super-blocks.

Statistics per day and target size (min / median / max across samples):

| Day | Target | Samples | Transactions | Blocks | Max tx from one sender | Tail dropped |
|---|---|---|---|---|---|---|
| Sep 1 | 10k | 198 | 10,002 / 10,141 / 10,847 | 17 / 36 / 47 | 69 / 154 / 684 | 2,304 |
| Sep 1 | 20k | 99 | 20,000 / 20,131 / 20,598 | 39 / 71 / 92 | 129 / 307 / 1,169 | 17,126 |
| Sep 1 | 40k | 50 | 40,006 / 40,159 / 40,481 | 103 / 142 / 181 | 265 / 588 / 2,183 | 4,573 |
| Sep 1 | 60k | 33 | 60,004 / 60,105 / 60,478 | 149 / 212 / 272 | 394 / 893 / 3,174 | 28,461 |
| Sep 1 | 80k | 25 | 80,008 / 80,117 / 80,433 | 215 / 284 / 360 | 532 / 1,183 / 3,621 | 9,693 |
| Sep 1 | 100k | 20 | 100,014 / 100,208 / 100,360 | 280 / 350 / 443 | 679 / 1,449 / 4,865 | 9,333 |
| Sep 13 | 10k | 146 | 10,001 / 10,093 / 10,396 | 38 / 48 / 63 | 84 / 160 / 600 | 630 |
| Sep 13 | 20k | 73 | 20,000 / 20,085 / 20,278 | 78 / 97 / 117 | 154 / 329 / 552 | 9,195 |
| Sep 13 | 40k | 36 | 40,003 / 40,110 / 40,323 | 160 / 192 / 233 | 351 / 647 / 845 | 32,218 |
| Sep 13 | 60k | 24 | 60,014 / 60,119 / 60,383 | 242 / 289 / 341 | 492 / 980 / 1,183 | 32,951 |
| Sep 13 | 80k | 18 | 80,019 / 80,094 / 80,224 | 325 / 385 / 458 | 712 / 1,262 / 1,560 | 34,308 |
| Sep 13 | 100k | 14 | 100,001 / 100,087 / 100,278 | 412 / 480 / 551 | 824 / 1,543 / 1,925 | 74,860 |

**The two days are comparable at equal size.** At each target, the median
transaction counts of the two days are within 0.5% of each other, while Sep 13
needs 1.33–1.37x as many blocks. Sizing by transaction count is what makes the
two days comparable.

**Some super-blocks span missing blocks.** 29 super-blocks span block numbers
that are absent from the export, most likely empty blocks. Order is unaffected.

### Key finding: one sender caps parallelism at about 65x

Every transaction writes its sender's nonce, and nonces don't commute. So a
sender with k transactions in a super-block forces a dependency chain of length
k under every policy, P3 included. That gives:

- the critical path is at least the largest number of transactions any one
  sender has in the super-block;
- average parallelism is at most the super-block's transaction count divided by
  that number.

| Target | Sep 1: median floor | Sep 1: median parallelism bound | Sep 13: median floor | Sep 13: median parallelism bound |
|---|---|---|---|---|
| 10k | 154 | 66 | 160 | 63 |
| 20k | 307 | 65 | 329 | 62 |
| 40k | 588 | 68 | 647 | 62 |
| 60k | 893 | 67 | 980 | 61 |
| 80k | 1,183 | 68 | 1,262 | 63 |
| 100k | 1,449 | 69 | 1,543 | 65 |

- **The bound doesn't grow with size.** It sits at about 61–69x at every target,
  because the floor grows linearly with the super-block. Making super-blocks
  bigger adds work but no extra parallelism beyond about 65x.
- **One address is responsible in every representative sample.**
  `0x559432e18b281731c054cd703d4b49872be4ed53` sends about 1.5–1.7% of all
  transactions. It is the same address that topped Phase 2's hot keys on Sep 13.
- **Bursts are worse on Sep 1.** The heaviest Sep 1 super-blocks have floors of
  up to 4,865 at 100k, a bound of about 21x. Sep 13's floors are much more uniform
  (at most 1,925).
- **What this means for Phase 6.** Any measured critical path should be compared
  against this floor; a policy can't do better than it. If the floor dominates,
  the gap between P2 and P3 will be small, and the hottest remaining constraint
  is a protocol rule (nonce order), not state contention.

## Phase 4 — Correctness oracle

Built by `oracle.py` and `compare.py`; numbers from `report_oracle.py`; tests in
`test_oracle.py`. The oracle is a deliberately slow O(n²) reference and is
never timed.

### Definitions

These are the contract that every fast implementation must match.

- **Conflict:** two transactions share a key, and the pair of modes is neither
  READ/READ nor COMMUTATIVE/COMMUTATIVE.
- **Edge:** i → j for every conflicting pair with i < j in block order.
- **Level:** the longest path, counted in edges, ending at the transaction. A
  transaction with no incoming edge is at level 0.
- **Critical path:** the number of levels, max(level) + 1, counted in
  transactions.
- **A schedule is valid** if every conflicting pair i < j has
  level(i) < level(j). The original brief only required that no two
  conflicting transactions share a level. That misses an *order inversion*:
  a later transaction placed in an earlier level than one it conflicts with.
  The validator checks every pair directly from the access sets and shares no
  code with the edge or level code except the conflict rule.
- **A comparison passes** if the levels are identical and every edge the
  candidate drops is still implied by a path in its graph. It fails on
  different levels, on a dropped edge that leaves a conflicting pair
  unordered, or on an edge between transactions that don't conflict.

### Results

The slices are the first 500, 1,000 and 2,000 transactions of each day's median
10k super-block (`2026-09-01/10k/099` and `2026-09-13/10k/073`). The reference
outputs are saved in `oracle_ref/`, which is git-ignored and rebuilt by
`report_oracle.py`.

The validator accepted the oracle's own schedule in all 18 runs and rejected
the deliberately broken one in all 18. Comparing the oracle with itself gave
"identical" every time.

Critical path in levels; the floor is the longest single-sender chain in the
slice:

| Day | n | P1 | P2 | P3 | Floor | P1/P3 | P2/P3 | P3/floor |
|---|---|---|---|---|---|---|---|---|
| Sep 1 | 500 | 83 | 16 | 9 | 8 | 9.2 | 1.8 | 1.12 |
| Sep 1 | 1,000 | 208 | 55 | 17 | 17 | 12.2 | 3.2 | 1.00 |
| Sep 1 | 2,000 | 380 | 68 | 35 | 26 | 10.9 | 1.9 | 1.35 |
| Sep 13 | 500 | 99 | 26 | 10 | 10 | 9.9 | 2.6 | 1.00 |
| Sep 13 | 1,000 | 218 | 29 | 21 | 19 | 10.4 | 1.4 | 1.11 |
| Sep 13 | 2,000 | 445 | 39 | 26 | 26 | 17.1 | 1.5 | 1.00 |

Edges and average parallelism (n ÷ critical path) at n = 2,000:

| Day | P1 edges | P2 edges | P3 edges | P1 parallelism | P2 parallelism | P3 parallelism |
|---|---|---|---|---|---|---|
| Sep 1 | 99,664 | 8,395 | 5,533 | 5.3 | 29.4 | 57.1 |
| Sep 13 | 114,511 | 6,292 | 4,035 | 4.5 | 51.3 | 76.9 |

### Observations

- **The Phase 3 prediction holds.** The critical path is never below the nonce
  floor under any policy. Under P3 it sits at 1.00–1.35x the floor, exactly
  on it in 3 of 6 slices. Once balance updates commute, sender nonce chains
  are what bound the schedule.
- **Policy precision matters a lot.** P1's critical path is 9–17x longer than
  P3's, and P1 extracts only 4.5–6x average parallelism. P2 removes most of
  that gap. P3 then roughly halves P2's critical path again on Sep 1, and on
  Sep 13 it reaches the floor.
- **P3's parallelism matches the Phase 3 bound.** At 2,000 transactions, P3 gives
  57–77x average parallelism, in line with the ~61–69x bound from super-block
  nonce chains.
- **Batches are very uneven.** Level 0 holds most of the slice (993 of 2,000
  transactions under P3 on Sep 1), while the median level has 12–14
  transactions and the tail levels hold single transactions of the nonce
  chain. Average parallelism hides this; executor efficiency will depend on
  the long thin tail.
- **The floor sender varies by slice.** In five of the six slices it is
  `0x28c6c06298d514db089934071355e5743bf21d60`, in one
  `0x559432e18b281731c054cd703d4b49872be4ed53`. Over a whole 10k–100k
  super-block, `0x5594…ed53` dominates (Phase 3).

### Caveat: commutative semantics

Treating two COMMUTATIVE updates as conflict-free assumes:

- the executor applies balance changes atomically, merging deltas rather than
  having each transaction overwrite the balance;
- the insufficient-balance revert is ignored. A debit can fail depending on
  order, so it isn't strictly commutative.

This is the OCToPus assumption that Phase 2 adopted. P3's numbers are
therefore an optimistic bound on what commutativity can recover.
