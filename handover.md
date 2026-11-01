# Handover: GPU-Accelerated Conflict Detection (IT 301)

## What this project is

We measure how much parallelism exists in real Ethereum blocks, and what it
costs to compute the schedule. Two questions:

- **Question A (answered, Phase 6):** how much parallelism is there, and what
  limits it?
- **Question B (your part, Phases 8–10):** what does computing it cost on CPU
  vs GPU, and when does GPU offloading pay off?

Important framing: this project does NOT try to beat Block-STM. That was the
original proposal and it is not achievable — static conflict detection can't
win on Ethereum, and we have the measurements explaining why. The contribution
is characterisation, not speedup. Please don't re-frame results as "we beat X".

## What's done (Phases 0–7)

| Phase | What |
|---|---|
| 0 | Data: 4 BigQuery exports, 2 days of Ethereum mainnet (2026-09-01, 2026-09-13) |
| 1 | Binary corpus: 3,489,388 transactions, loads in 0.27 s |
| 2 | Three inference policies: P1 contract, P2 slot, P3 commutative |
| 3 | 736 disjoint super-blocks, 10k–100k transactions |
| 4 | Python oracle + independent schedule validator |
| 5 | Verified single-threaded C++ pipeline (~60 ms per 100k super-block) |
| 6 | THE RESEARCH RESULT — full sweep, 2,208 runs |
| 7 | OpenMP implementation [status: finishing sweep] |

Read `findings.md` first. It has a corrections log (C1–C8) recording where we
predicted something, tested it, and were wrong. Keep that practice.

## Key results you need to know

- **Granularity is the big lever:** P1 gives ~5x parallelism, P2 35–49x, P3
  45–64x. Coarse conflict description inflates contention ~10x.
- **Parallelism is FLAT in batch size:** 10k to 100k changes it by at most 12%.
- **~33% of transactions are generic contract calls with no inferable
  footprint.** They fall back to contract granularity under every policy.
- **Two verified limits:** OKX Hot Wallet 5 (nonce chains, structural, 67.7% of
  super-blocks) and a launch-day NFT mint (transient).
- **Levelling does NOT parallelise.** Level-synchronous scheduling gets SLOWER
  with more threads: ~21,000 levels × 2 barriers each, and barrier cost scales
  with graph depth while work per level scales with width. Our graphs are deep
  and thin. This is a finding, not a bug.
- **Pipeline is sort-dominated:** 60–70% of time in the sort, under 2% in
  levelling. (This is with `std::sort`, the seq-std baseline. With the radix
  sort added in Phase 7 the sort drops to roughly 25–45% at 100k, and
  extraction becomes as large as the sort: P1 seq-radix is 3.9 ms extract,
  2.5 ms sort, 2.3 ms CSR out of 11 ms. Check `results/phase7/` before
  deciding what to port first.)

## Your work: Phases 8–10

### Phase 8 — CUDA port

Port stages 1–3 (extraction, conflict detection, graph construction) to CUDA.
Do NOT port levelling to the GPU — Phase 7 established that level-synchronous
parallel scheduling is non-viable on these graphs, and the GPU would hit the
same barrier-per-level wall harder. Use the sequential sweep for levelling and
say so.

Port stage by stage, verifying against the CPU reference after each. Partial
ports are valid intermediate states and make failures easy to localise.

The measurement that matters is the **crossover**: at what super-block size
does GPU compute beat PCIe transfer cost? Charge transfer time to the GPU.
Given ~60 ms of total work on one core, transfer may dominate at every testable
size. If so, that IS the result — report the transfer-cost arithmetic.

### Phase 9 — Skew handling

The OKX hot wallet's nonce key has ~1,500 accesses in a 100k super-block while
most keys have one or two. One GPU thread per key group would serialise that
onto a single thread. Measure the unbalanced version FIRST to get a
before-number, then rebalance, then report the recovery. Without the
before-number you have a design decision and no result.

### Phase 10 — Full sweep and analysis

Factorial: implementation × super-block size × policy. Warm-ups discarded,
multiple runs, median reported. Profiler pass (occupancy, memory throughput,
warp divergence) on the largest super-block.

## Non-negotiables

1. **NO TIMING WITHOUT CORRECTNESS.** Every implementation must produce output
   bit-identical to the verified CPU reference before any number is reported.
   The Python schedule validator must accept the output. A fast wrong answer
   is worse than no answer, and you will not catch it later.
2. **DETERMINISM.** Identical output across runs and thread/block counts. If a
   stage is only deterministic after a final sort, do that sort and account
   for its cost.
3. **FAIRNESS.** Two baselines exist: seq-std (`std::sort`) and seq-radix.
   Report speedup against both, labelled. A GPU radix sort beating `std::sort`
   is partly an algorithm change, not hardware.
4. **RECORD THE MACHINE.** CPU model, core count, SMT on/off, RAM, GPU model,
   VRAM, CUDA version, driver. Every timing number is meaningless without it.
   Note: the Phase 7 scaling curves were measured on an M2 with efficiency
   cores and are contaminated — they need re-running on your machine
   (`tools/run_phase7.sh` does this in one command).
5. **SCOPE EVERY CLAIM.** Two days of Ethereum mainnet, not Ethereum in
   general. Where a mechanism generalises by argument (nonces are sequential
   by protocol), say so separately from the measurement.

## Setup

**Requirements:** Linux or WSL2 (the corpus loader uses POSIX `mmap`; native
Windows is not supported), CMake ≥ 3.16, a C++17 compiler with OpenMP,
Python 3 with `numpy` and `matplotlib`, and the CUDA toolkit for Phase 8.

**Code:**

```sh
git clone https://github.com/antonyth18/Conduit-GPU-Detection
cd Conduit-GPU-Detection
```

**Data and corpus:** none of the data is in git (`data/`, `corpus/` and
`oracle_ref/` are git-ignored). Get the four CSV exports (~1 GB) from me and
put them in `data/`:

```
data/tx_20260901.csv        data/calldata_20260901.csv
data/tx_20260913.csv        data/calldata_20260913.csv
```

Then build the corpus (~181 MB) and the oracle reference slices:

```sh
python3 ingest.py           # writes corpus/
python3 verify_corpus.py    # Phase 1 checks
python3 report_oracle.py    # writes oracle_ref/, needed by tools/verify_cpp.py
```

`superblocks.json` is in git and pinned by SHA-256 to the corpus; the loaders
refuse a corpus that doesn't match, so a successful load confirms your corpus
is identical to ours.

**Build:**

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The CMake output should report OpenMP as found; `-DCONDUIT_OPENMP=OFF`
builds without it.

**Verify your setup** — all of these must pass before you change anything:

```sh
build/conduit_tests                                    # 29 C++ tests, "0 failed checks"
python3 -m unittest test_access_sets test_superblocks test_oracle
python3 tools/verify_cpp.py                            # C++ vs Python oracle: "ALL CHECKS PASSED"
python3 tools/verify_cpp.py --impl omp --threads 8 --level kahn
build/conduit_bench identical                          # every super-block: "ALL IDENTICAL"
```

**Re-run Phase 7 on your machine** (needed for non-negotiable 4):

```sh
tools/run_phase7.sh                                    # results/phase7/<host>/
python3 report_openmp.py results/phase7/<host>         # summary.md + figures
```

## Where things are

| Path | What |
|---|---|
| `findings.md` | Research log, phase by phase, with the corrections log |
| `ingest.py`, `access_sets.py`, `superblocks.py` | Python: corpus, policies, super-blocks |
| `oracle.py`, `compare.py` | Brute-force oracle, schedule validators, edge-set comparison |
| `include/conduit/`, `src/` | C++ pipeline: `pipeline.cpp` (sequential), `pipeline_omp.cpp`, `radix.cpp` |
| `src/detail.hpp` | The shared reduced edge rule (`walk_group`) — reuse it, don't re-derive it |
| `tools/conduit_run.cpp` | Run the pipeline on one or all super-blocks (`--impl`, `--threads`, `--level`, `--dump`) |
| `tools/conduit_bench.cpp` | `identical` (bit-identity sweep) and `scale` (timing sweep, CSV) |
| `tools/verify_cpp.py` | Feeds C++ dumps to the Python validators; extra flags pass to `conduit_run` |
| `results/` | Phase 6 tables and figures; `results/phase7/<host>/` per machine |

## Boundaries

- **You own:** `cuda/`, Phase 8–10 benchmarking code, `findings.md` sections
  for 8–10.
- **I own:** [TODO: whatever you're keeping — router decoding? the write-up?]
- **Shared:** the C++ pipeline. Coordinate before changing it; if you find a
  bug, fix it in the sequential path and re-verify against the oracle rather
  than patching around it in GPU code.

## Open questions worth your judgement

- Does the stages 1–3 OpenMP crossover (~10–20k on the M2) hold on real cores?
- Does the GPU ever beat a well-parallelised OpenMP run, at any testable size?
- How much does skew rebalancing actually recover?

## Ask me before

- Changing the conflict rule, the edge-reduction rule, or the policies
- Re-running anything that would invalidate Phase 6's 2,208-run results
- Framing a result as beating prior work
