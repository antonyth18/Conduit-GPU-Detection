# Conduit: what does it cost to compute a parallel schedule for Ethereum transactions, on CPU and GPU?

**IT 301 Parallel Computing project, NITK Surathkal.**
*GPU-accelerated conflict detection and dependency-graph construction for parallel
smart-contract execution.*

A blockchain executes each block one transaction after another. Many transactions do
not touch the same state and could run at the same time, but a scheduler must first
find every conflict and turn the conflicts into a safe execution order
[Dickerson et al. 2017]. This project builds that preprocessing pipeline three ways
(single-threaded C++, OpenMP, CUDA) and **characterises** it on real data:

- **Question A** (Phases 0–6): how much parallelism is in real Ethereum blocks, and
  what limits it?
- **Question B** (Phases 7–10): what does computing the schedule cost on CPU and GPU,
  and from what size does GPU offloading pay off once transfers are charged?

The data is two full days of Ethereum mainnet (2026-09-01 and 2026-09-13, 3,489,388
transactions) from the public BigQuery dataset. The contribution is the measurement,
not a new execution engine: we do not claim to beat speculative engines such as
Block-STM [Gelashvili et al. 2023]. Every algorithm used is published work, listed in
[`REFERENCES.md`](REFERENCES.md).

| Document | What's in it |
|---|---|
| [`findings.md`](findings.md) | The research log: every result, with a corrections log |
| [`results/`](results/) | Every run: `phase8/Nikhil-PC-real-large/summary.md` is the main one |
| [`handover.md`](handover.md) | The review of Phases 8–10 and its open items |
| [`REVIEW_RESPONSE.md`](REVIEW_RESPONSE.md) | How each review item was addressed |
| [`GPU_README.md`](GPU_README.md) | CUDA design, algorithms and correctness arguments in detail |
| [`REFERENCES.md`](REFERENCES.md) | Every source, and where it is used in the code |
| [`visualizer/`](visualizer/index.html) | Step-by-step interactive walk-through on a small block |

---

## 1. How the pipeline works

```
 CSV exports ──ingest──► binary corpus ──► super-blocks (10k … 2M transactions)
                                                 │
   stage 1  extract   one thread per transaction: (state key, read/write/commutative)
   stage 2  sort      radix sort by state key: conflicts can only be neighbours
            detect    key groups and runs → dependency edges (reduced rule)
   stage 3  CSR       edges → sorted, de-duplicated compressed sparse row DAG
   stage 4  levels    longest path = batch number (sequential sweep on the CPU)
                      alternatives measured on the GPU: Kahn, Jones–Plassmann
```

- **Read/write sets.** Ethereum does not publish what a transaction touches, so three
  policies infer it: P1 locks whole accounts; P2 locks individual slots (nonce, ETH
  balance, token balance, allowance); P3 additionally treats balance additions as
  commutative [Weihl 1988; Pîrlea et al. 2021]. Calls that cannot be decoded fall back
  to the whole contract.
- **Conflicts.** Two accesses to one key conflict unless both are reads or both are
  commutative updates [Bernstein et al. 1987]. Sorting by key (radix sort,
  [Merrill & Grimshaw 2011]) replaces the O(n²) pairwise comparison.
- **Reduced edges.** Within a key, each access depends on the previous run of
  accesses only. Older conflicts are implied by a path, so levels are identical to the
  full conflict graph with far fewer edges (proof in `include/conduit/pipeline.hpp`).
- **Batches.** A transaction's level is the length of the longest dependency chain
  ending at it. Executing level by level reproduces the serial result exactly.
- **On the GPU.** Every step is a "count, prefix-sum, write" pattern [Blelloch 1990]
  over CUB primitives. Output is bit-identical to the CPU, which is verified stage by
  stage.

## 2. Results

> **Where the numbers come from.** Final run on an RTX 4050 laptop
> (`results/phase8/Nikhil-PC-real-large/` and `Nikhil-PC-real/`): Intel
> i5-13420H (12 hardware threads), NVIDIA GeForce RTX 4050 Laptop GPU (6 GB),
> CUDA 12.6, WSL2, on AC power. **2 warm-up + 7 measured runs, median**; super-blocks
> above 250k transactions use 1 + 3 runs per configuration. All five correctness
> gates passed before any timing (section 6). Windows was on the *Balanced* power
> plan, not *Best performance*; that mainly affects the small sizes (see crossover).
> A second laptop (i7-13620H, same GPU model, 1 + 2 runs) gave the same picture at
> large sizes: `results/phase8/sucheth-katte-ASUS-TUF-Gaming-real-large-quick/`.
> Scope: two days of Ethereum mainnet, one laptop GPU.

### Phase 8: cost of computing the schedule, wall time, transfers charged

| block | policy | seq-std | seq-radix | OpenMP best (threads) | GPU wall | vs seq-std | vs seq-radix | vs OpenMP |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| 2026-09-01, 100k | P1 | 22.2 ms | 8.8 ms | 5.5 ms (8) | 2.51 ms | 8.8× | 3.5× | 2.2× |
| 2026-09-01, 100k | P2 | 49.5 ms | 21.6 ms | 13.2 ms (8) | 3.58 ms | 13.9× | 6.0× | 3.7× |
| 2026-09-01, 100k | P3 | 39.3 ms | 15.4 ms | 8.6 ms (8) | 3.02 ms | 13.0× | 5.1× | 2.8× |
| 2026-09-01, full day (2.01M) | P1 | 584 ms | 242 ms | 279 ms (4) | 53.5 ms | 10.9× | 4.5× | 5.2× |
| 2026-09-01, full day (2.01M) | P2 | 1,173 ms | 548 ms | 491 ms (8) | 73.2 ms | 16.0× | 7.5× | 6.7× |
| 2026-09-01, full day (2.01M) | P3 | 1,038 ms | 475 ms | 439 ms (8) | 63.5 ms | 16.3× | 7.5× | 6.9× |
| 2026-09-13, full day (1.48M) | P2 | 814 ms | 343 ms | 334 ms (8) | 52.8 ms | 15.4× | 6.5× | 6.3× |

- **Read the speedups against seq-radix and OpenMP.** Part of the gain over seq-std
  comes from switching to radix sort, not from the hardware (finding C9).
- **Crossover.** With transfers charged, the GPU beats seq-radix and the best OpenMP
  run from about **10k–20k transactions** on this machine (40k for Sep 13 under P1).
  Below that, the GPU's fixed cost per call (launches, synchronisations, small
  copies: 0.5–1.2 ms here) dominates. On the second laptop that cost was
  0.15–0.19 ms and the crossover 2k–5k, so **the crossover depends on the machine**;
  the large-size speedups agree between the two.
- **Transfers.** At the full day, moving the input (80.5 MB, 6.4 ms at 10.1 GB/s
  pinned) and the graph back (27–32 MB, 2.7–3.6 ms) costs **14–19% of the GPU wall
  time**. Staging the input into pinned host memory adds another 8–13 ms and copying
  results out 1.4–2.5 ms: that host-side work is the gap between wall and device time
  [Gregg & Hazelwood 2011].
- **Where the device time goes** (full day, P2): radix sort 13.5 ms, H2D 6.4,
  detection 6.7, CSR 4.7, D2H 3.6, extraction 3.1. The CPU levelling sweep adds
  19 ms and is now the largest single part.

![wall time vs size](docs/real_wall_vs_size_rtx4050.png)

### Phase 9: skew

The hottest state key is the **USDT contract account**: 20,805 records in a 100k
super-block and 388,725 at full day (Sep 1). With one GPU thread per key group, that
one thread does most of the detection work.

| detection stage | one thread per key group (before) | one thread per record | one thread per output edge (after) | recovery |
|---|---:|---:|---:|---:|
| 2026-09-01, 100k, P1 | 5.22 ms | 0.218 ms | 0.233 ms | 22× |
| 2026-09-01, full day, P1 | 138.8 ms | 3.47 ms | 3.53 ms | 39× |
| 2026-09-01, full day, P2 | 136.2 ms | 6.80 ms | 6.62 ms | 21× |
| 2026-09-01, full day, P3 | 136.0 ms | 6.10 ms | 5.72 ms | 24× |
| 2026-09-13, full day, P1 / P2 / P3 | 93.5 / 91.6 / 91.5 ms | | 2.49 / 4.77 / 4.20 ms | 38× / 19× / 22× |

The "after" mapping splits work by output edge with a binary search over prefix sums
(the merge-based decomposition of [Merrill & Garland 2016]). On this data, one thread
per record already recovers almost all of it: at full day it is within about 10% of
the per-edge mapping, because the imbalance it leaves (a long run of reads on one
hot key) is small next to the per-group imbalance.

![skew](docs/real_skew_rtx4050.png)

### Stage 4: levelling stays on the CPU

Kahn's algorithm on the GPU gives identical levels but needs one synchronised round
per level: 21k–401k rounds at full day. Even with almost all rounds inside one
persistent thread block [Gupta et al. 2012], it takes 76–392 ms against 9–19 ms for
the CPU sweep, **7–29× slower**, as Phase 7 predicted for deep, thin graphs
[Valiant 1990]. Jones–Plassmann colouring [Jones & Plassmann 1993] is implemented
and verified: with block-order priority it reproduces the levels exactly; with
random priority its batches are conflict-free and reach the per-key lower bound, but
only by reordering transactions. It re-examines every waiting transaction each round,
so it is skipped above 250k transactions.

### Phase 10: profiler pass (Nsight Compute, full day 2026-09-01)

- The radix sort (CUB's onesweep kernel) is **56–58% of kernel time**; the kernels
  that take the time are **memory-bound** (SM throughput 18–29% of peak against
  36–85% for memory) with achieved occupancy of 73–96%.
- Warp execution efficiency is 0.92–1.00 everywhere except **extraction under P2/P3
  (0.64–0.67)**: threads in one warp decode different call types (transfers,
  approvals, unknown calls) and diverge. Under P1 every call is decoded the same way
  and extraction runs at 0.99.
- Nsight Systems recorded host-side CUDA calls but no GPU activity under WSL2, so the
  timeline comes from the project's own cudaEvent stage timings instead.
  Full tables: `results/phase10/Nikhil-PC-profile/profile_summary.md`.

### How many batches a whole day needs

| full day | transactions | P1 | P2 | P3 |
|---|---:|---:|---:|---:|
| 2026-09-01 | 2,013,158 | 401,223 | 89,380 | 42,309 |
| 2026-09-13 | 1,476,230 | 269,610 | 30,150 | 21,378 |

Finer granularity (P1 → P3) cuts the number of batches by 9–13×. This is the same
lever Phase 6 found at 10k–100k.

### Execution model (not a measurement of an Ethereum client)

To show what the schedule buys, `conduit_gpu execute` runs every transaction of the
day on a toy state machine. Each transaction does work proportional to its gas over
its real read/write keys, and every parallel run must end in exactly the serial state
(it did, in every run). Two standard executors are compared: batches with a barrier
between them (bulk-synchronous) and dataflow list scheduling on the DAG, where a
transaction starts when its predecessors finish [Graham 1969; Kwok & Ahmad 1999].

| full day | policy | level batches (best threads) | DAG dataflow (best threads) | GPU preprocessing + dataflow | model bound, batches / dataflow (unlimited workers) |
|---|---|---:|---:|---:|---:|
| 2026-09-01 | P1 | 1.06× (4) | 5.43× (8) | 5.00× | 1.99× / 10.9× |
| 2026-09-01 | P2 | 2.03× (8) | 7.14× (12) | 6.10× | 3.58× / 24.8× |
| 2026-09-01 | P3 | 2.29× (8) | 7.18× (12) | 6.27× | 4.33× / 25.6× |
| 2026-09-13 | P2 | 2.28× (8) | 7.01× (12) | 6.26× | 3.67× / 13.4× |

Barrier-per-batch execution is capped by the slowest transaction in each batch;
dataflow on the same DAG gets about 3× more out of the same 12 threads. These are
model results on this machine; a real EVM would differ.

![execution model](docs/execution_model_full_day_rtx4050.png)

## 3. Setup (Windows laptop with an NVIDIA GPU)

The code is C++17 + CUDA + CMake and needs Linux; on Windows use WSL2, which reaches
the GPU through the normal Windows driver.

**Windows PowerShell (administrator), once:**
```powershell
nvidia-smi                       # must list the GPU
wsl --install -d Ubuntu-22.04    # reboot, open Ubuntu, create a user
wsl --update
```

**Ubuntu, once:**
```bash
sudo apt update
sudo apt install -y build-essential cmake git wget python3 python3-numpy python3-matplotlib
wget https://developer.download.nvidia.com/compute/cuda/repos/wsl-ubuntu/x86_64/cuda-keyring_1.1-1_all.deb
sudo dpkg -i cuda-keyring_1.1-1_all.deb && rm cuda-keyring_1.1-1_all.deb
sudo apt update
sudo apt install -y --no-install-recommends cuda-nvcc-12-6 cuda-cudart-dev-12-6 cuda-cccl-12-6
echo 'export PATH=/usr/local/cuda-12.6/bin:$PATH' >> ~/.bashrc
echo 'export LD_LIBRARY_PATH=/usr/local/cuda-12.6/lib64:$LD_LIBRARY_PATH' >> ~/.bashrc
source ~/.bashrc
nvcc --version && nvidia-smi
```
For the profiler pass, also install `cuda-nsight-compute-12-6` and
`cuda-nsight-systems-12-6`.

**Build and test:**
```bash
git clone https://github.com/antonyth18/Conduit-GPU-Detection ~/conduit && cd ~/conduit
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release     # prints "conduit: CUDA 12.x enabled, architectures 89"
cmake --build build -j$(nproc)
build/conduit_tests        # 39 tests, 0 failed checks
build/conduit_gpu info     # ... backend cuda
```

**Data:** put the BigQuery exports in `data/`: one transaction CSV and one calldata
CSV per day, any file names. Transaction exports are recognised by their header and
matched to days by `block_timestamp`; calldata files must contain the date
(`YYYYMMDD`) in their name. The CSVs are ignored by git.

## 4. Running everything

Before timing: charger plugged in, Windows power mode **Best performance**, other
applications closed. The scripts record the power state in `machine.txt`.

| Command | What it does | Output |
|---|---|---|
| `bash tools/run_phase7.sh` | OpenMP scaling sweep (Phase 7, re-run on this machine) | `results/phase7/<host>/` |
| `bash tools/run_phase8.sh` | Phases 8–10 on the standard 10k–100k super-blocks: tests → GPU stages → GPU verify → bit-identity sweep → Python validator → PCIe probe → factorial sweep → skew → schedules | `results/phase8/<host>-real/` |
| `bash tools/run_dataset.sh data` | The same at every size up to the full day, plus the execution model | `results/phase8/<host>-real-large/` |
| `bash tools/profile.sh` | Phase 10 profiler pass (Nsight Systems and Nsight Compute) on the largest super-block | `results/phase10/<host>-profile/` |
| `bash tools/run_phase8.sh --synthetic` | The same on synthetic workloads (`tools/synth_corpus.py`) | `results/phase8/<host>-synthetic/` |

All three run scripts use **2 warm-up + 7 measured runs** and stop at the first failed
correctness check. `--quick` is for smoke tests only. Add `--profile` to
`run_phase8.sh` or `run_dataset.sh` to include the profiler pass. Each folder ends with
`summary.md`.

Single steps, for a demo:
```bash
python3 ingest.py                                  # corpus/ from data/ (standard manifest)
python3 ingest.py --tx data --out corpus_eth       # corpus_eth/ with 250k, 500k, 1M and full-day blocks
export CONDUIT_CORPUS=corpus_eth                   # tools pick the manifest next to the corpus

build/conduit_gpu stages --target 100000           # Phase 8: each GPU stage vs the CPU stage
build/conduit_gpu verify                           # GPU == CPU on every size
build/conduit_bench identical                      # bit-identity sweep, every super-block
python3 report_oracle.py && python3 tools/verify_cpp.py --impl cuda   # Python validator
build/conduit_gpu pcie                             # PCIe bandwidth and latency
build/conduit_run --id 2026-09-01/full --impl cuda # the whole day on the GPU
build/conduit_gpu skew --full                      # Phase 9 before / after
build/conduit_gpu bench --csv bench.csv --configs full
build/conduit_gpu execute --full                   # execution model, every transaction
```

## 5. The visualizer

`visualizer/index.html` walks through the whole pipeline on a small block in 14
stages. The key steps play as **live animations** of about 5–9 seconds each,
with Pause, Restart, a slider to scrub to any moment and 0.5×/1×/2× speed:

- GPU threads extracting the read/write sets, all at once
- the parallel prefix sum that turns counts into write offsets
- threads writing their records into their own slots
- the radix sort, pass by pass
- head flags, one thread per sorted record
- the dependency DAG: edges emitted, then the levelling sweep moving every
  transaction into its batch column
- the conflict graph built key by key
- Jones–Plassmann colouring, round by round (block-order or random priority)
- the four execution schedules running on one clock (Gantt charts)

A caption under the controls says what is happening at each moment; the right panel
explains the stage, names the GPU kernels and cites the sources. P1/P2/P3 and the
block can be switched at any point.

**Open it:** double-click `visualizer/index.html` (no server or internet needed), or
from Ubuntu run `explorer.exe visualizer/index.html`. Next/Back or the arrow keys
move between stages; Space pauses or plays, R restarts.

The pictures are recordings of the real code, not a separate re-implementation.
To record them again (`bash tools/make_visualizer.sh` does all three):
```bash
build/conduit_trace --example                                         # hand-made 12-transaction block
build/conduit_trace --id 2026-09-01/10k/000 --auto --limit 14 --append    # a real window
build/conduit_trace --id 2026-09-13/10k/000 --auto --limit 16 --append
```
`conduit_trace` runs the CPU stages and the GPU kernel bodies (through the emulation)
on the block, checks them against each other, and writes `visualizer/trace_data.js`.

## 6. Correctness (non-negotiable 1)

No number is reported unless all of these pass. The scripts stop otherwise.

1. **39 unit tests:** GPU paths equal the CPU for every policy, mapping and levelling
   method; brute-force O(n²) oracle; Jones–Plassmann checks; executor equivalence.
   Mutation-tested: breaking a kernel makes them fail.
2. **`conduit_gpu stages`:** each GPU stage (extract, sort, detect, CSR, levels) equals
   the corresponding CPU stage, for all three detection mappings.
3. **`conduit_gpu verify`:** bit-identical output on every size from 10k to the full
   day, schedule checks from the read/write sets alone, and a brute-force oracle on the
   first 2,000 transactions.
4. **`conduit_bench identical`:** every super-block, seq-std vs seq-radix vs OpenMP vs
   GPU.
5. **Python validator** (`tools/verify_cpp.py --impl cuda`): the Phase 4 oracle and
   schedule validators accept the GPU output.
6. **Execution model:** every parallel run reproduces the serial final state and all
   outputs.

## 7. Repository map

| Path | Contents |
|---|---|
| `ingest.py`, `superblocks.py`, `access_sets.py`, `oracle.py` | Corpus, super-blocks, Python policies and oracle |
| `src/pipeline.cpp`, `src/pipeline_omp.cpp`, `src/radix.cpp` | Sequential and OpenMP pipelines |
| `src/gpu/kernels.hpp` | GPU kernel bodies, shared by CUDA and the emulation |
| `src/gpu/cuda_pipeline.cu`, `src/gpu/emu_pipeline.cpp` | CUDA backend and host emulation |
| `src/schedule.cpp`, `src/execute.cpp` | Schedule checks, bounds, execution model |
| `tools/conduit_gpu.cpp` | `info, pcie, verify, stages, bench, skew, schedule, execute` |
| `tools/conduit_trace.cpp`, `visualizer/` | Visualizer recorder and page |
| `tools/run_*.sh`, `tools/profile.sh`, `tools/lib_machine.sh` | One-command runs |
| `report_gpu.py`, `report_profile.py`, `report_openmp.py` | Reports and figures |
| `results/` | One folder per machine and run; see the README in each |
| `tests/` | C++ unit tests |

## 8. Limitations

- **Read/write sets are inferred, not traced.** Internal calls are invisible, and
  undecodable calls fall back to whole-contract locks.
- **Two days of mainnet and one laptop GPU.** Claims are scoped to these.
- **Real blocks hold about 200–300 transactions.** The GPU pays off from roughly 2k,
  so the setting is batches of many blocks (super-blocks, rollup batches, replay), not
  a single block.
- **The execution section is a model,** not an Ethereum client.
- **Jones–Plassmann** is impractical above about 250k transactions on these deep
  graphs.
