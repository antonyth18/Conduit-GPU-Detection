# Findings

Running log of results for the Ethereum parallelism study. Each phase appends
its numbers and the caveats that bound them. Figures are reproducible with the
script named in each section.

## Corrections log

Earlier claims that later phases revised. The original text stays in its
section with a **Correction** note beside it, so the history remains readable.

| # | Claim | Made in | Corrected in | What the larger sample shows |
|---|---|---|---|---|
| C1 | "P3 sits exactly on the nonce floor in 9 of 12 super-blocks … at full scale the critical path under P3 is simply the busiest sender's transaction count." | Phase 5 | Phase 6 | Across all 736 super-blocks, P3 is exactly on the floor in **62% on Sep 13 and 37% on Sep 1**. Sep 1's tail reaches 3.7x the floor at p95 and 25.9x at the maximum. The 12 Phase 5 super-blocks were the middle sample of each day by position, not a random draw, and happened to sit on the floor. |
| C2 | "P1 stays about 10–14x worse than P3 at every size." | Phase 5 | Phase 6 | The per-super-block median is **9.3x on Sep 1 and 11.1x on Sep 13**, with p5–p95 of 3.6–14.6x and 5.7–14.7x, and a range of 1.0–22x. "10–14x" described the 12 median super-blocks, not the distribution. |
| C3 | "Once balance updates commute, sender nonce chains are what bound the schedule." | Phase 4 | Phase 6 | True for most super-blocks on Sep 13 but not on Sep 1. In about **95% of above-floor super-blocks on both days**, a single contract's chain of fallback WRITEs, most often a DEX router, is longer than the nonce floor and sets the critical path instead. |
| C4 | Above-floor bursts are "mint storms". | Phase 6 (first draft) | Phase 6 | Mint-like contracts explain only the most extreme dips. The common cause is **DEX routers**: `0x68b3…fc45` alone sets the excess in 159 super-blocks. |
| C5 | "A policy can't do better than [the ~65x bound]" (Phase 3 key finding). | Phase 3 | Phase 6 (refinement, not a reversal) | The bound held in all 2,208 runs. But it is not always *reached*: P3's median parallelism is 45–48x on Sep 1 and 58–64x on Sep 13, against a bound of ~61–69x. |

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

> **Refinement (Phase 6, see C5).** The bound held in all 2,208 runs, but it is
> a ceiling, not a prediction. P3 reaches a median of 45–48x on Sep 1 and
> 58–64x on Sep 13. On Sep 1 especially, fallback chains on DEX-router
> contracts hold the critical path above the nonce floor.

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
  > **Correction (Phase 6, see C3).** This generalised from 6 small slices.
  > Across all super-blocks, the nonce floor sets the P3 critical path in most
  > Sep 13 super-blocks but in fewer than half of Sep 1's. Elsewhere a single
  > contract's fallback WRITE chain, usually a DEX router, is longer.
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

## Phase 5 — Sequential C++ pipeline

A C++17 / CMake project with no dependencies, built with -O3 and run
single-threaded:

- `include/` and `src/`: the library;
- `tools/conduit_run`: runs the pipeline and prints timings and statistics;
- `tools/dump_access`: dumps access records for the cross-language check;
- `tests/`: the C++ unit tests.

The Python verification tools are `tools/xcheck_access.py` and
`tools/verify_cpp.py`. Timings were measured on an Apple M2 (one
performance core) and are reported as the median of 7 runs after 2
discarded warm-up runs.

### Design

- **Access record:** 128 bits, held as two 64-bit words.
  - The high word is the Phase 2 state key.
  - The low word holds the transaction index (local to the super-block) in
    bits 34:3 and the mode in bits 2:0.
  - Sorting records as integers groups them by key, then orders them by block
    position. No two records are equal, so the sort order is deterministic.
- **Reduced edge rule.** Within each key, accesses in block order split into
  runs: consecutive READs, consecutive COMMUTATIVEs, or a single WRITE. Every
  access depends on every access in the previous run, and on nothing earlier.
  - Every edge emitted is a real conflict.
  - Every real conflict is still connected by a path through the runs in
    between.
  - So reachability and longest paths, and therefore levels, are identical to
    the full conflict graph.
- **READ next to COMMUTATIVE is the one exception.** Where a run of several
  READs meets a run of several COMMUTATIVEs, every pair across them must get
  an edge. Phase 2 never puts READ and COMMUTATIVE on the same key, and the
  pipeline counted **0 such edges** on every super-block.
- **Levelling** is one ascending sweep over the successor CSR, which is exact
  and optimal sequentially. The OpenMP and CUDA versions will need
  frontier-based relaxation instead, and that difference must be reported.

### Verification

- **The C++ policy port matches Python byte for byte.** This holds under P1, P2
  and P3 on the whole corpus (3,489,388 transactions; 6.95M records under P1
  and 15.06M under P2/P3). It also holds on each day's median super-block at
  every target size, loaded through the C++ super-block path.
- **The C++ levels match the oracle exactly on all 18 Phase 4 slices.** The
  C++ graph has far fewer edges (on Sep 1 at n = 2,000 under P1, 1,721 instead
  of 99,664), and every dropped edge is still implied by a path. There are no
  spurious edges, and both validators accept.
- **The fast validator accepts all 36 full-size runs.** That covers each day's
  median super-block at every target from 10k to 100k, under all three
  policies.
  - The fast validator is new in `oracle.py`. Per key it tracks the highest
    level seen so far for each mode, so it never enumerates pairs.
  - Its tests require it to agree with the brute-force validator on
    hand-built, random and real cases, including broken schedules.
- **The critical path never drops below the single-sender floor.**
- **Output is identical across repeated runs,** and the whole timed run peaks
  at 224 MB of memory.

### Critical path at full scale

Median super-block per target; "÷ floor" is the critical path divided by the
longest single-sender chain.

| Day | Target | Floor | P1 levels | P2 levels | P3 levels | P1 ÷ floor | P2 ÷ floor | P3 ÷ floor |
|---|---|---|---|---|---|---|---|---|
| Sep 1 | 10k | 222 | 2,058 | 326 | 222 | 9.3 | 1.47 | 1.00 |
| Sep 1 | 20k | 274 | 3,891 | 432 | 310 | 14.2 | 1.58 | 1.13 |
| Sep 1 | 40k | 671 | 8,268 | 871 | 671 | 12.3 | 1.30 | 1.00 |
| Sep 1 | 60k | 939 | 12,311 | 1,420 | 1,105 | 13.1 | 1.51 | 1.18 |
| Sep 1 | 80k | 1,239 | 16,538 | 1,835 | 1,362 | 13.3 | 1.48 | 1.10 |
| Sep 1 | 100k | 1,733 | 21,013 | 2,181 | 1,733 | 12.1 | 1.26 | 1.00 |
| Sep 13 | 10k | 181 | 1,981 | 266 | 181 | 10.9 | 1.47 | 1.00 |
| Sep 13 | 20k | 373 | 3,949 | 529 | 373 | 10.6 | 1.42 | 1.00 |
| Sep 13 | 40k | 704 | 7,609 | 1,300 | 704 | 10.8 | 1.85 | 1.00 |
| Sep 13 | 60k | 1,037 | 11,328 | 1,874 | 1,037 | 10.9 | 1.81 | 1.00 |
| Sep 13 | 80k | 1,374 | 15,001 | 2,305 | 1,374 | 10.9 | 1.68 | 1.00 |
| Sep 13 | 100k | 1,741 | 18,678 | 2,766 | 1,741 | 10.7 | 1.59 | 1.00 |

- **P3 sits exactly on the nonce floor in 9 of 12 super-blocks,** and within
  1.18x in the other three. At full scale the critical path under P3 is
  simply the busiest sender's transaction count.
  > **Correction (Phase 6, see C1).** True of these 12 super-blocks, not of
  > the population. They were the middle sample of each day and size by
  > position, not chosen by outcome, and they happened to fall on the floor.
  > Across all 736 super-blocks, P3 is exactly on the floor in 62% on Sep 13
  > and 37% on Sep 1. The ratio's p95 is 1.86x on Sep 13 and 3.69x on Sep 1,
  > and the maximum is 25.9x.
- **P1 stays about 10–14x worse than P3 at every size,** with 4.8–5.4
  transactions per level, against 46–65 for P3.
  > **Correction (Phase 6, see C2).** Measured per super-block across all
  > 736, P3's advantage over P1 has a median of 9.3x on Sep 1 and 11.1x on
  > Sep 13, a p5–p95 of 3.6–14.7x, and a full range of 1.0–22x. At the low
  > end, one hot contract serialises both policies alike.

### Batch shape: what keeps an executor busy

| Day | Target | Policy | Tx per level | Levels with < 32 tx | Share of tx in level 0 | Median level size |
|---|---|---|---|---|---|---|
| Sep 1 | 10k | P3 | 45.8 | 80.6% | 38.8% | 4 |
| Sep 1 | 100k | P1 | 4.8 | 99.5% | 6.6% | 3 |
| Sep 1 | 100k | P2 | 45.9 | 61.9% | 10.4% | 23 |
| Sep 1 | 100k | P3 | 57.8 | 64.3% | 25.7% | 22 |
| Sep 13 | 100k | P3 | 57.5 | 68.2% | 23.3% | 18 |

- **Most levels are too small to keep many workers busy.** Under P3, 64–81%
  of levels hold fewer than 32 transactions at every size. The average of
  about 58 transactions per level comes from a few huge levels: level 0 alone
  holds 23–39% of the super-block.
- **A 32-wide executor would run below capacity on most levels.** The
  long, thin tail is the nonce chain, one transaction per level per busy
  sender.

### Sequential timings

All times are in ms.

| Day | Target | Policy | Extract | Sort | Detect | CSR | Level | Total |
|---|---|---|---|---|---|---|---|---|
| Sep 1 | 10k | P3 | 0.65 | 3.49 | 0.30 | 0.50 | 0.08 | 5.02 |
| Sep 1 | 40k | P3 | 2.72 | 15.82 | 1.13 | 2.60 | 0.34 | 22.61 |
| Sep 1 | 100k | P1 | 2.57 | 17.84 | 1.49 | 9.46 | 0.74 | 32.18 |
| Sep 1 | 100k | P2 | 6.98 | 41.73 | 3.18 | 15.11 | 1.01 | 68.21 |
| Sep 1 | 100k | P3 | 6.66 | 41.64 | 2.72 | 7.81 | 0.88 | 59.67 |
| Sep 13 | 100k | P3 | 6.77 | 41.58 | 2.67 | 7.92 | 0.80 | 59.66 |

- **Total time grows linearly with size,** about 0.6 ms per 1,000
  transactions under P3, roughly 1.7M transactions per second on one core.
- **Sorting takes 60–70% of the time** at every size and policy. Building
  the CSR is second (sorting and deduplicating edges); detection and
  levelling together are under 10%.
- **The parallel phases should focus on the sort first.** Levelling is the
  cheapest stage sequentially but is the one that needs a different
  algorithm in parallel.

### Fairness notes for Phases 7–8

- **Account for any change of algorithm.** The sequential sort is
  comparison-based `std::sort`. If the OpenMP or CUDA versions use a radix
  sort, part of the speedup comes from the algorithm, not the hardware. Report
  it against a sequential radix sort too, or state the change of algorithm.
- **Account for frontier-based levelling.** It does more total work than the
  sequential sweep, and that also has to be reported.
- **Take the final timings on the Linux GPU machine.** The M2 mixes
  performance and efficiency cores, so these sequential numbers are for
  development only; the final comparison needs CPU and GPU on one machine.

## Phase 6 — Question A: how much parallelism is in real Ethereum blocks?

The verified sequential C++ pipeline was run on **all 736 super-blocks under
all three policies: 2,208 runs.**

- **Scripts:** `measure_parallelism.py` runs the sweep and validates every run;
  `report_parallelism.py` produces the statistics and figures.
- **Data:** `results/phase6_runs.csv` has one row per run;
  `results/phase6_validation.csv` has each run's validator verdict.
- **Results:** `results/phase6_summary.md` has the full tables; the figures are
  `results/fig_parallelism_vs_size.png`, `results/fig_floor_ratio.png` and
  `results/fig_bursts.png`.

Parallelism means transactions ÷ levels: the average number of transactions
that can run at once. Super-blocks remain a modelling decision: consecutive
real blocks concatenated in their original order.

**Every result rests on a verified schedule.**
- The fast validator accepted all 2,208 schedules.
- No critical path was below the nonce floor.
- Output was identical across repeated runs.
- No edges came from READ↔COMMUTATIVE run transitions, as predicted.

### Headline

**Slot-level inference (P2) finds about 8x more parallelism than
contract-level (P1); treating balance updates as commutative (P3) adds
about 1.25x more.** Medians across all super-blocks of a day:

| Day | P1 contract | P2 slot | P3 commutative | P3 ÷ P1 per super-block (median; p5–p95) |
|---|---|---|---|---|
| Sep 1 (Tue) | 5.0–5.2x | 35–40x | 45–48x | 9.3 (3.6–14.6) |
| Sep 13 (Sun) | 5.4–5.5x | 44–49x | 58–64x | 11.1 (5.7–14.7) |

- **P1 is stable and low.** At 5x with a tight spread, contract-level
  granularity hides almost all of the available parallelism.
- **The extra gain from P3 over P2 is modest for a typical super-block**
  (median 1.23x on Sep 1, 1.26x on Sep 13), but it reaches 4–19x on
  super-blocks where many transfers hit the same balance.
- **The Sunday had more parallelism than the Tuesday** under P2 and P3 (about
  60 vs 47 under P3). With only two days this can't be attributed to the
  weekday.

### Invariance to batch size

**The median parallelism is flat from 10k to 100k transactions.** Bigger
batches add work but not parallelism.

The elasticity in the table is the slope of log(parallelism) against
log(batch size) across all super-blocks of a day. 0 means flat; 1 would mean
parallelism grows in proportion to batch size.

| Day | Policy | Median at 10k | Median at 100k | 100k ÷ 10k | Elasticity |
|---|---|---|---|---|---|
| Sep 1 | P1 | 5.1 | 5.0 | 0.99 | +0.003 |
| Sep 1 | P2 | 35.4 | 39.7 | 1.12 | +0.051 |
| Sep 1 | P3 | 44.6 | 47.3 | 1.06 | +0.030 |
| Sep 13 | P1 | 5.5 | 5.4 | 0.98 | −0.005 |
| Sep 13 | P2 | 43.5 | 47.5 | 1.09 | +0.071 |
| Sep 13 | P3 | 58.3 | 63.6 | 1.09 | +0.057 |

- **A tenfold larger batch gains at most 12%.** That is consistent with the
  Phase 3 prediction: the busiest sender's share of the batch, and so the nonce
  floor, scales with it.
- **The spread narrows as batches grow.** Under P3 the 5th percentile rises
  from 17.5x to 24.2x on Sep 1 and from 33.9x to 53.5x on Sep 13. Larger
  batches don't raise the typical parallelism; they dilute the bursts.

### The nonce floor

Critical path ÷ the longest single-sender chain in the same super-block:

| Day | Policy | Median | p5 | p95 | Max | Exactly on the floor | Within 5% |
|---|---|---|---|---|---|---|---|
| Sep 1 | P1 | 13.35 | 4.53 | 24.71 | 31.82 | 0% | 0% |
| Sep 1 | P2 | 1.81 | 1.08 | 6.82 | 25.88 | 1.2% | 3.3% |
| Sep 1 | P3 | 1.11 | 1.00 | 3.69 | 25.88 | 37.4% | 43.5% |
| Sep 13 | P1 | 11.95 | 7.63 | 18.89 | 24.88 | 0% | 0% |
| Sep 13 | P2 | 1.38 | 1.05 | 2.43 | 5.85 | 3.2% | 4.8% |
| Sep 13 | P3 | 1.00 | 1.00 | 1.86 | 5.85 | 62.4% | 66.2% |

### Correction to Phase 5: the floor binds less often than reported

Phase 5 reported that P3 sits exactly on the nonce floor in 9 of 12
super-blocks, and that "at full scale the critical path under P3 is simply the
busiest sender's transaction count". **That does not hold for the full
sample.**

| | Phase 5 (12 median super-blocks) | Phase 6 (all 736 super-blocks) |
|---|---|---|
| P3 exactly on the floor | 9 of 12 (75%) | Sep 13: 62.4%; Sep 1: 37.4% |
| P3 within 5% of the floor | 9 of 12 | Sep 13: 66.2%; Sep 1: 43.5% |
| P3 critical path ÷ floor, worst case | 1.18 | 25.9 |
| P3 ÷ P1 parallelism | "about 10–14x" | median 9.3x on Sep 1 and 11.1x on Sep 13; p5–p95 3.6–14.7x; range 1.0–22x |

**Why Phase 5 was misleading.** It reported one super-block per day and size:
the middle one by position in the day, not a random or representative draw.
Twelve points can't show a distribution whose tail sits 26x above the floor.
That is why this phase reports distributions over every super-block.

**What changes in the conclusion.**
- **Sep 13:** the nonce floor is the dominant limit for P3, as Phase 5 said.
- **Sep 1:** it is the limit in only about two-fifths of super-blocks. In
  most of the rest, a single contract's fallback chain is longer (next
  section).

**One sender sets the floor in most super-blocks.**
`0x559432e18b281731c054cd703d4b49872be4ed53` is the busiest sender in 498 of
the 736 super-blocks (67.7%). No other sender exceeds 6.2%.

### What holds P3 above the floor: fallback chains on one contract, mostly DEX routers

For every P3 super-block, we compared the longest chain of WRITEs on a
single ACCOUNT key with the nonce floor. Under P3, a chain like that can
only come from fallback (undecoded) calls to one contract. The comparison is
a count, not a trace of the critical path.

| Day | Super-blocks more than 5% above the floor | Of those, a single-contract chain longer than the floor |
|---|---|---|
| Sep 1 | 240 of 425 (56.5%) | 228 (95.0%) |
| Sep 13 | 105 of 311 (33.8%) | 100 (95.2%) |

The contracts whose chain sets the excess most often:

| Contract | Above-floor super-blocks |
|---|---|
| `0x68b3465833fb72a70ecdf485e0e4c7bd8665fc45` (Uniswap SwapRouter02) | 159 |
| `0x4cd00e387622c35bddb9b4c962c136462338bc31` | 46 |
| `0x278d858f05b94576c1e6f73285886876ff6ef8d2` | 39 |
| `0x7a250d5630b4cf539739df2c5dacb4c659f2488d` (Uniswap V2 Router02) | 23 |
| `0x51c72848c68a965f66fa7a88855f9f7784502a7f` | 16 |
| `0x8cc5c24a1666d3214340050315b82b2b6b0d9027` | 15 |
| `0x00005ea00ac477b1030ce78506496e8c2de24bf5` (likely SeaDrop; unverified) | 9 |

**The second limit is a granularity artifact, and its direction is known.**

- Under P2 and P3 a router call is undecoded, so it WRITEs the whole router
  account. Every swap through the router then joins one serial chain.
- **Routers keep almost no per-swap state.** Real conflicts between swaps are
  on the liquidity pools they touch, which is internal-call state we can't see.
  Swaps through different pools don't conflict.
- So the router chain overstates conflicts between swaps on different pools.
  But because internal calls are invisible, it can't be replaced by the true
  pool-level chain from this data.

**Treat above-floor P3 numbers as pessimistic about routers.** Decoding router
calldata to recover the pool addresses is the clearest next step for
tightening Question A.

### Extreme bursts: mint storms, also measured at P1 granularity

The deepest dips, below about 13x parallelism under P3, come from a different
fallback pattern: hundreds to thousands of distinct senders calling one
contract with one selector.

| Super-block | P3 parallelism | P3 levels | Floor | Cause |
|---|---|---|---|---|
| `2026-09-01/10k/179` | 5.7x | 1,786 | 69 | 1,786 calls to `0x062bd962fad2cf6df9ab91d6a41f55c1c20da298` from 1,775 senders, selector `0x4a21a2df` |
| `2026-09-01/20k/090` | 8.8x | 2,303 | 129 | the same contract, 2,303 calls |
| `2026-09-13/10k/085` | 10.1x | 995 | 170 | 995 calls to `0x00005ea00ac477b1030ce78506496e8c2de24bf5` from 903 senders, selector `0x161ac21f` |
| `2026-09-01/10k/049` | 12.3x | 821 | 119 | 821 calls to the same `0x0000…4bf5` contract, selector `0x4b61cd6f` |

- **These bursts are assumed, not measured.** A fallback call WRITEs the whole
  contract, so these super-blocks are measured at P1 granularity, and every
  call to the contract becomes one serial chain.
- **The pattern looks like NFT mints, but this is unconfirmed.** I believe
  `0x0000…4bf5` is OpenSea's SeaDrop contract; that should be checked on a
  block explorer before it is cited.
- **Real mints do share some state,** such as a supply counter, but the
  serial chain is an upper bound on their conflicts.
- **These bursts are the most extreme cases, not the most common.**
  Correction to this phase's first draft (see C4): router fallback chains
  (previous section) affect far more super-blocks. Mint storms explain only
  the deepest dips.

`fig_bursts.png` shows bursts as short, deep dips through both days, while P1
stays flat near 5x throughout.

### Batch shape

| Day | Target | Policy | Levels with < 32 tx | Share of tx in level 0 | Median level size |
|---|---|---|---|---|---|
| Sep 1 | 10k | P3 | 80.4% | 39.7% | 5 |
| Sep 1 | 100k | P3 | 79.0% | 27.7% | 12 |
| Sep 13 | 10k | P3 | 73.4% | 37.8% | 9 |
| Sep 13 | 100k | P3 | 61.9% | 25.2% | 24 |
| Both | any | P1 | ≥ 98.6% | 6–16% | 2–3 |

These are medians across super-blocks; `phase6_summary.md` has P2 as well.

- **Even under P3, 62–80% of levels hold fewer than 32 transactions.**
- **Level 0 holds a quarter to two-fifths of each super-block.**
- **A wide executor would be busy only briefly:** at the start, then for a long
  tail of narrow levels.

### Answer to Question A

On these two days of mainnet traffic, real Ethereum batches offer about
**5x parallelism at contract granularity, 35–49x at slot granularity, and
45–64x with commutative balances** (medians). The figure is flat across batch
sizes from 10k to 100k. Contract-level inference understates the parallelism
by about 9–11x per super-block (median).

Under P3, two limits set the critical path (corrected from Phase 5's "simply
the busiest sender's nonce chain"; see C1 and C3):

1. **The busiest sender's nonce chain** is a protocol rule that holds under
   every policy. It is the binding limit in 62–66% of Sep 13 super-blocks and
   37–44% of Sep 1's.
2. **Fallback chains on a single contract**, mostly DEX routers (Uniswap
   SwapRouter02 in 159 super-blocks), account for about 95% of the rest.
   These are a granularity artifact: undecoded router calls WRITE the whole
   router account.

The two remaining bounds on the result:
- **Internal calls are invisible,** which makes every figure here an upper
  bound.
- **About a third of transactions fall back to P1.** That makes the router
  chains and mint-storm bursts pessimistic: a lower bound on parallelism in
  the super-blocks they dominate.

These two biases point in opposite directions, so neither can be assumed to
cancel the other. The next step that would most tighten the answer is
decoding router calldata to recover pool addresses. That would also close
the gap between P3's measured median and the ~65x nonce bound.
