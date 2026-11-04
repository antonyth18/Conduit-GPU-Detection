# Conduit — Phases 8–10: GPU conflict detection, dependency graph and batching

This is the CUDA half of the IT 301 project *GPU-Accelerated Conflict Detection and
Dependency Graph Construction for Parallel Smart Contract Execution*. It extends the
verified CPU pipeline (Phases 0–7, see `handover.md` and `findings.md`) with:

| What | Where |
|---|---|
| CUDA pipeline, stages 1–4 (CUB radix sort / scan / unique) | `src/gpu/cuda_pipeline.cu` |
| Kernel bodies shared by CUDA **and** a CPU emulation | `src/gpu/kernels.hpp` |
| Host emulation of every kernel (runs without a GPU) | `src/gpu/emu_pipeline.cpp` |
| GPU API (`run_pipeline_gpu`, options, timings) | `include/conduit/gpu.hpp` |
| Jones–Plassmann batching on the implicit conflict graph | `kernels.hpp` (`jp_phase_*`) |
| Phase 9 skew: three detection mappings, one thread per key group (`group`), per record (`record`), per output edge (`balanced`) | `kernels.hpp` (`walk_group`, `record_emit`, `record_count` + `edge_emit`) |
| Schedule checks, lower bound, execution simulator | `include/conduit/schedule.hpp`, `src/schedule.cpp` |
| Driver: `info`, `pcie`, `verify`, `stages`, `bench`, `skew`, `schedule`, `execute` | `tools/conduit_gpu.cpp` |
| Execution model: serial, level batches, DAG dataflow | `include/conduit/execute.hpp`, `src/execute.cpp` |
| Visualizer recorder (runs the real stages on a small block) | `tools/conduit_trace.cpp`, `visualizer/` |
| Synthetic corpus (factorial workloads, Ethereum-like mix) | `tools/synth_corpus.py` |
| One-command runs on the GPU machine | `tools/run_phase8.sh` (standard sizes), `tools/run_dataset.sh` (up to the full day), `tools/profile.sh` (Phase 10 profiler) |
| Tables + figures from the CSVs | `report_gpu.py`, `report_profile.py` |
| Unit tests (GPU paths, JP, oracle, simulator) | `tests/test_gpu.inc` |

`--impl cuda` and `--impl gpu-emu` also work in the existing tools (`conduit_run`,
`conduit_bench identical`, `tools/verify_cpp.py`), so the Python validator checks the
CUDA output exactly like it checks the sequential and OpenMP output.

---

## 1. What problem this solves

Before a block of transactions can be executed in parallel, a scheduler must know which
transactions conflict (two accesses to the same state key, at least one a write) and
group them into **batches** that can run concurrently. That preprocessing is:

```
Tx[ ]  ──1 extract──▶ records (key, tx, mode) ──2 sort+detect──▶ conflict edges
       ──3 CSR──▶ dependency DAG ──4 level / colour──▶ batch id per transaction
```

The GPU parallelises all four stages. Stage 4 produces two kinds of schedule:

* **Order-preserving (levels).** Batch = longest-path level in the DAG whose edges point
  from earlier to later block position. Running batches 0,1,2,… and committing gives
  exactly the serial result in block order — what a *validator replaying a fixed block*
  needs. Computed by the CPU sweep (default), GPU Kahn, or **GPU Jones–Plassmann with
  index priorities** (proved below to give the same levels).
* **Reordering (Jones–Plassmann colouring, random priorities).** Every round of JP selects
  an independent set of the *full* conflict graph; the round is the batch (colour). Batches
  are conflict-free but transactions may move relative to block order — valid for a *block
  builder* that decides the order, not for replaying a block. This is the graph-colouring
  method from the proposal, kept honest about what it may and may not be used for.

`conduit_gpu schedule` then **executes** each schedule on W simulated workers (gas as the
cost model) to show what the batches buy in execution time.

## 2. Algorithms (and why they are correct)

### Stage 1 — extraction (`extract_count`, `extract_write`)
One thread per transaction runs `k_tx_access`, a line-for-line port of `policy.cpp`
(P1 contract / P2 slot / P3 commutative). Counts → `cub::DeviceScan::ExclusiveSum` →
each thread writes its ≤7 records at its offset. Records keep the `record.hpp` layout
(`hi = key`, `lo = tx<<3 | mode`). A unit test compares the port with `tx_access` on
every policy.

### Stage 2a — sort
Records are produced in transaction order, so a **stable** sort by key alone gives the
full 128-bit `(key, tx, mode)` order: `cub::DeviceRadixSort::SortPairs` (stable) on the
64-bit key with the low word as value. Output equals `std::sort` element for element.

### Stage 2b — conflict detection (the reduced edge rule of `detail::walk_group`)
Group heads `hi[i] != hi[i-1]` and run heads (new key, a WRITE, or a mode change) are
flagged and inclusive-scanned. Two kernels then emit the edges between adjacent runs:

* `group` (naive, Phase 9 *before*): one thread walks a whole key group. A hot key
  (an exchange wallet's nonce, USDT under P1) serialises onto one thread.
* `balanced` (Phase 9 *after*, default): each record counts its in-edges (= size of the
  previous run), an exclusive scan gives per-record offsets, and **one thread per output
  edge** finds its record by binary search (load-balanced search). Work per thread is
  O(log R) no matter how skewed the keys are.

Both emit the same multiset of edges as `walk_group`; after stage 3's sort+unique the
CSR is identical.

### Stage 3 — CSR
Edge `u→v` becomes `u·n + v` (order-preserving, needs only ⌈log₂ n²⌉ ≈ 34 bits at 100k,
so the radix sort does fewer passes than on `u<<32|v`), `SortKeys` → `DeviceSelect::Unique`
→ `cols = key mod n`, `row_offsets[u] = lower_bound(u·n)` by binary search per row.

### Stage 4 — levels and batches
* **sweep** (default, CPU): ascending sweep over the copied-back CSR — optimal for deep,
  thin graphs (handover, Phase 7).
* **kahn** (GPU): in-degrees by `atomicAdd`; a vertex joins the next frontier when its
  last predecessor is processed (`atomicSub == 1`), so its round is its longest path.
* **jp-index / jp-random** (GPU): Jones–Plassmann on the **implicit** conflict graph —
  no edges are materialised (the full graph has O(m²) edges on hot keys). Per round, per
  key group `g`, `atomicMax` collects the highest packed priority among active records
  overall (`maxA`) and per mode (`maxW`, `maxR`, `maxC`). A record is a local maximum iff
  it beats every active record of `g` it conflicts with (WRITE: `pk == maxA`; READ:
  `pk > maxW, maxC`; COMM: `pk > maxW, maxR`). A transaction that is a local maximum on all
  its keys gets colour = round. Priorities are packed as `(round+1)<<44 | prio`, so maxima
  from earlier rounds lose every comparison and nothing is ever reset.
  * *Independence:* two selected conflicting transactions would have to beat each other.
  * *Progress:* the highest-priority uncoloured transaction is always selected.
  * *jp-index = levels:* with priority "earlier block position wins", `v` is selected in
    the round after all its earlier conflicting neighbours, i.e. round(v) = 1 + max over
    them = longest path in the full graph = longest path in the reduced graph (pipeline.hpp
    proves the two have the same longest paths). Tested bit-for-bit against the sweep.

**The barrier wall: device-driven rounds and the persistent tail.** Real and
Ethereum-like graphs are deep and thin (thousands of levels of a few transactions each),
so the number of rounds, not the work, dominates. Two measures:

* *Device-driven rounds.* Frontier sizes and the round number live in device memory
  (`RoundState`); kernels read them themselves, so the host enqueues `chunk` (32) rounds
  back-to-back and reads the state once per chunk instead of once per round. Rounds after
  the last one are no-ops.
* *Persistent tail.* Once the frontier / active set is at most `tail_threshold` (8192),
  **one persistent block of 1024 threads runs all remaining rounds** separated by
  `__syncthreads()` — no launches, no host round-trips.

`launches`, `host_syncs`, `rounds` and `tail_rounds` are reported for every run; tune with
`conduit_gpu bench --tail T --chunk K`.

**Known cost of Jones–Plassmann on deep graphs.** Every JP round re-examines *every*
still-uncoloured transaction, so its work is O(rounds × active) — on the Ethereum-like
60k block that is ~7,000 queued launches and ~150 tail rounds, against Kahn's 84 launches
with 1,911 of 1,943 rounds in the persistent block (Kahn only touches the frontier,
O(n + m) work). Expect JP to be the slowest stage-4 method on the GPU; it is kept because
it is the graph-colouring method of the proposal, because jp-index is an independent
cross-check of the levels, and because jp-random is the only *reordering* scheduler.

### Determinism
Every atomic is either commutative (`add`, `max`) or only claims a slot in an output list
whose order cannot affect the result (frontiers, active lists). All outputs are identical
across runs, thresholds and backends — the tests check exactly that.

## 3. How correctness is established (non-negotiable 1)

1. **Unit tests** (`build/conduit_tests`, 36 tests): the GPU paths (emulation, and the
   real GPU when one is visible) must equal `seq-std` for every policy, detect mode,
   levelling method and three tail thresholds, on random blocks with hot senders; JP-random
   batches must be conflict-free and backend-independent; the brute-force O(n²) oracle
   must equal the pipeline; mutation-tested (breaking `jp_phase_b` or `edge_emit` makes
   tests fail).
2. **`conduit_gpu verify`**: the same checks on real/synthetic super-blocks, plus the
   brute-force oracle on the first 2000 transactions, `check_order` (every edge goes
   forward in levels), `check_batches` (each batch conflict-free from the *access sets*,
   no graph involved), the per-key lower bound, and CUDA == emulation for JP-random.
3. **`conduit_bench identical`** now includes the GPU (sweep and Kahn) for every
   super-block, and **`tools/verify_cpp.py --impl cuda`** feeds the CUDA output to the
   Python oracle/validators.
4. **`conduit_gpu bench`** re-checks every configuration against `seq-std` before timing
   it and refuses to time a mismatch.

> **Status note.** The build environment used to write this code had no CUDA compiler,
> so everything above was run through the emulation backend, which executes the *same*
> kernel bodies (`kernels.hpp`) launch by launch. The CUDA wrappers in
> `cuda_pipeline.cu` (grid-stride loops, CUB calls, the two persistent kernels, memory
> management) are first compiled on your machine. `tools/run_phase8.sh` runs the tests
> and `verify` on the real GPU before any timing, so a wrapper bug cannot produce
> numbers. If the first compile reports an error, it will be in those wrappers.

## 4. Setup on your laptop (Windows + RTX 4050)

The corpus loader uses POSIX `mmap`, so build and run inside **WSL2** (CUDA works in WSL2
through the normal Windows driver).

1. **Windows side:** install/update the regular NVIDIA Game Ready or Studio driver
   (R535 or newer). Do **not** install a Linux driver inside WSL.
2. **WSL2 + Ubuntu 22.04** (PowerShell as administrator):
   ```powershell
   wsl --install -d Ubuntu-22.04
   ```
   Reboot, open "Ubuntu", create the user. Check the GPU is visible: `nvidia-smi`.
3. **CUDA toolkit inside Ubuntu** (WSL-Ubuntu repository; toolkit only, no driver):
   ```sh
   wget https://developer.download.nvidia.com/compute/cuda/repos/wsl-ubuntu/x86_64/cuda-keyring_1.1-1_all.deb
   sudo dpkg -i cuda-keyring_1.1-1_all.deb
   sudo apt update
   sudo apt install -y cuda-toolkit-12-6 build-essential cmake git python3-numpy python3-matplotlib
   echo 'export PATH=/usr/local/cuda/bin:$PATH' >> ~/.bashrc && source ~/.bashrc
   nvcc --version
   ```
   (Any CUDA ≥ 11.8 works; 12.x recommended. RTX 4050 = compute capability 8.9, which is
   the default `CMAKE_CUDA_ARCHITECTURES=89`.)
4. **Get the project into WSL** (building on `/mnt/d` works but is slow; copy it):
   ```sh
   cp -r "/mnt/d/projects/parallel computing/Conduit-GPU-Detection" ~/conduit
   cd ~/conduit
   ```
   or `git clone https://github.com/antonyth18/Conduit-GPU-Detection ~/conduit` and copy
   the new files over.
5. **Plug the laptop in and set Windows power mode to "Best performance"**; laptop GPUs
   clock down on battery and the numbers would be meaningless (record this in the report).

## 5. Running it

### One command (everything, in order, with correctness gates)
```sh
bash tools/run_phase8.sh                 # real corpus/ (10k-100k), 2 warm-up + 7 runs
bash tools/run_dataset.sh data           # every size up to the full day + execution model
bash tools/run_phase8.sh --synthetic     # synthetic corpus (generated on first run)
bash tools/profile.sh                    # Phase 10: Nsight Systems + Nsight Compute
# --quick = 1 warm-up + 2 runs, smoke test only; --profile adds the profiler pass
```
Every script runs the gates in this order and **stops at the first failure**:
unit tests → `conduit_gpu stages` → `conduit_gpu verify` → `conduit_bench identical`
→ Python validator (`report_oracle.py` + `tools/verify_cpp.py --impl cuda`). Only
then: `pcie`, `bench`, `skew`, `schedule` (and `execute` in `run_dataset.sh`).
Output: `results/phase8/<host>-real[-large]/` with `machine.txt` (CPU, GPU clocks, AC
power and Windows power plan), the gate logs, `pcie.csv`, `bench.csv`, `skew.csv`,
`schedule.csv`, `execute.csv`, `summary.md` and figures.

### Phase-by-phase commands
```sh
build/conduit_gpu stages --target 100000   # Phase 8: each GPU stage vs the CPU stage, all mappings
build/conduit_gpu pcie                     # pinned/pageable H2D, D2H, launch and sync latency
build/conduit_gpu skew --full              # Phase 9: hot key, imbalance, group/record/balanced
build/conduit_gpu bench --csv bench.csv --configs full   # Phase 8/10 factorial sweep
build/conduit_gpu schedule --csv schedule.csv            # batches vs bound, JP-random
build/conduit_gpu execute --full           # execution model over the whole day
bash tools/profile.sh --id 2026-09-01/full # Phase 10 profiler pass
```

### Step by step
```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release     # look for "conduit: CUDA 12.x enabled"
cmake --build build -j
build/conduit_tests                                 # "39 tests, 0 failed checks"
build/conduit_gpu info                              # "GPU NVIDIA GeForce RTX 4050 ... backend cuda"

python3 tools/synth_corpus.py --out corpus_synth    # 1.1M synthetic tx, 5 scenarios, 215 super-blocks
S="--corpus corpus_synth --manifest corpus_synth/superblocks.json"

build/conduit_gpu verify $S                         # must end "ALL GPU CHECKS PASSED"
build/conduit_run $S --median --impl cuda           # GPU pipeline, per-stage table
build/conduit_run $S --median --impl cuda --level kahn
build/conduit_gpu bench $S --csv bench.csv --configs full
build/conduit_gpu schedule $S --csv schedule.csv
mkdir -p r && mv bench.csv schedule.csv r/ && python3 report_gpu.py r
```
With the real corpus, drop `$S` and additionally run
`build/conduit_bench identical` and `python3 tools/verify_cpp.py --impl cuda`.

### Useful options
| Option | Meaning |
|---|---|
| `--backend cuda\|emu` | force the GPU or the emulation (`emu` = correctness only) |
| `--configs full` | includes all three detection mappings (group, record, balanced) |
| `--runs N --warmup W` | measured runs and warm-ups (default 7 and 2) |
| `--jp-max N` | skip Jones–Plassmann above N transactions (default 250000) |
| `--tail T`, `--chunk C` | persistent-block threshold and launch chunk for Kahn/JP |
| `CONDUIT_CORPUS`, `CONDUIT_MANIFEST` | environment overrides; the manifest next to the corpus is used by default |
| `--configs quick\|full` | `full` = every detect × level combination |
| `--limits 1000,2000,5000` | prefixes of the 10k super-block, to locate the crossover |
| `--threads 1,2,4,8,16` | OpenMP thread counts for the CPU comparison |
| `--workers 8,16,32,64,0` | simulated executor widths (0 = unbounded) |
| `--policy P1\|P2\|P3\|all`, `--id ID`, `--all`, `--date D`, `--target T` | case selection |

### Synthetic scenarios (`tools/synth_corpus.py`)
| scenario | meaning |
|---|---|
| `eth-like` | calibrated to the real corpus' shape: dominant token (P1 ≈ 7× parallel at 100k), hot exchange wallet nonce chain (1.5% of txs), ~⅓ generic calls; P3 ≈ 37× |
| `uniform-low` / `uniform-high` | uniform access, large / tiny state population (low / high conflict density) |
| `zipf-low` / `zipf-high` | Zipf-skewed access, large / small population |

These are models; label anything measured on them *synthetic* (non-negotiable 5).

## 6. Reading the results

* **RQ1/RQ2 — speedup of stages 1–3.** `summary.md` table "end to end, wall time": GPU
  wall (host staging + H2D + kernels + D2H + CPU sweep, `t_wall_ms`) against seq-std, seq-radix and the best OpenMP run,
  each labelled. `k-only` shows the kernels alone. The crossover table gives the smallest
  tested size where the GPU wins *with transfers charged*. If it says "never", that is
  the result (handover: "if transfer dominates at every testable size, that IS the
  result") — report the transfer arithmetic from the stage-breakdown table.
* **Phase 9 — skew.** `skew.csv`: the hottest key (named), work imbalance (max ÷ mean per
  thread) and detect time for the three mappings; recovery = group ÷ balanced.
* **RQ3 — scheduling.** Stage-4 table: rounds, how many ran in the persistent block,
  launches, time of sweep vs Kahn vs JP. Schedule table: batches vs the lower bound and
  simulated execution speedups.
* **Profiler pass.** `tools/profile.sh` writes `profile_summary.md` (per-kernel achieved
  occupancy, memory and DRAM throughput, warp execution efficiency, branch efficiency, from
  `ncu --csv`) plus `nsys_p3.nsys-rep` (timeline: launch gaps,
  memcpy) and `ncu_p3.ncu-rep` (occupancy, memory throughput, warp divergence per
  kernel). Open them with Nsight Systems / Nsight Compute on Windows. On WSL2, `ncu`
  needs GPU performance counters enabled for all users (NVIDIA Control Panel → Desktop →
  Enable Developer Settings → Manage GPU Performance Counters → "Allow access to all
  users").

What the emulation already shows (synthetic, counters only — not GPU times): on
Ethereum-like data both schedules sit almost exactly on the per-key lower bound, so
re-ordering (JP-random) buys almost nothing over order-preserving levels: the batch count
is set by hot-key chains, not by the scheduling algorithm. Gas-weighted simulated
speedups are far below transactions-per-batch because each batch waits for its most
expensive transaction.

## 7. Troubleshooting

| Symptom | Fix |
|---|---|
| CMake: "no CUDA compiler found" | `export PATH=/usr/local/cuda/bin:$PATH`, or `-DCMAKE_CUDA_COMPILER=/usr/local/cuda/bin/nvcc` |
| `nvcc fatal: Unsupported gpu architecture 'compute_89'` | CUDA < 11.8: upgrade, or `-DCMAKE_CUDA_ARCHITECTURES=86` |
| `conduit_gpu info` says "no CUDA device visible" | run `nvidia-smi` in WSL; update the Windows driver; `wsl --update` |
| `CUDA error: out of memory` | close other GPU apps; 100k blocks need well under 1 GB |
| `GPU output differs ... refusing to time it` | a real bug in the CUDA wrappers: run `build/conduit_tests` and `conduit_gpu verify --backend cuda` and compare with `--backend emu` |
| very noisy timings | AC power, "Best performance" mode, close the browser; use `--runs 15` |

---

## 8. Running on your own dataset (the 2-million-transaction BigQuery export)

### What changed for real data
* **`ingest.py --tx <csv or folder>`** (new generic mode): reads any BigQuery-style
  transaction export with at least `block_number, transaction_index, hash, from_address,
  to_address` (uses `selector, value, receipt_*, transaction_type, block_timestamp` when
  present), de-duplicates, sorts into execution order, splits days by `block_timestamp`,
  and keeps **every transaction**. It writes `corpus_eth/` plus
  `corpus_eth/superblocks.json` with sizes 10k…100k, 250k, 500k, 1M **and one full-day
  super-block per day (`2026-09-01/full`, all 2,013,158 transactions)**.
* **Parallel execution engine** (`include/conduit/execute.hpp`, `conduit_gpu execute`):
  the schedule is not just computed, it is *used* — every transaction of the day is
  executed by
  * `serial` — block order, one thread (baseline),
  * `level batches + barriers` — the classic batch-by-batch scheme,
  * `DAG dataflow` — a transaction starts the moment its last predecessor in the
    dependency DAG finishes (atomic counters + lock-free ready list, no barriers).

  Each parallel run must reproduce the serial run's final state and every transaction's
  output exactly (a toy state machine over the real read/write sets; reversing the
  block order is detected by the tests). The run aborts if it ever diverges.
* The end-to-end comparison is **serial execution** vs **GPU preprocessing + parallel
  execution**, so the speedup credits the scheduling, not just the GPU.
* Jones–Plassmann is skipped above 250k transactions (`--jp-max`): its O(rounds × active)
  work explodes on a 400k-round day.

### First results on this file (dev container, 2 vCPU, GPU emulated, no calldata)

Kept for the record; see `findings.md` C11 and C12. The laptop results with calldata are
in `README.md` and `results/phase8/sucheth-katte-ASUS-TUF-Gaming-F15-FX507VU-FX507VU-real/`.

| policy | batches (full day) | per-key lower bound | model speedup, level batches (W=∞) | model speedup, DAG dataflow (W=∞) | measured DAG, 2 threads |
|---|---:|---:|---:|---:|---:|
| P1 | 401,223 | 388,725 | 1.99× | 10.93× | 1.63× |
| P3 | 392,438 | 388,715 | 2.01× | 10.94× | 1.74× |

**Barrier-per-batch scheduling caps execution at ~2× no matter how many cores**: each of
the 400k batches waits for its slowest transaction. **DAG dataflow raises the ceiling to
~11×** on the same dependency information. That is the algorithmic improvement; your
laptop's cores and GPU determine how much of it is realised.

### Why P2/P3 looked like P1 on the first export (fixed by the calldata files)
Your export has `selector` but **no calldata**, so for the 725,509 ERC-20
`transfer/approve/transferFrom` calls the recipient is unknown. To stay correct the
policies must then assume the call may touch any holder of that token, i.e. lock the whole
contract. USDT alone (`0xdac17f…1ec7`, 388,725 transactions, 19% of the day) becomes one
serial chain: that is the 388,725 lower bound above.

With the calldata, USDT transfers become per-holder keys and the bound drops to the busiest
sender's nonce chain (OKX hot wallet, 25,001 transactions): about **15× more room**.
Your teammate's `calldata_20260901.csv` is exactly this file; or export it from BigQuery:

```sql
SELECT block_number, `hash`,
       SUBSTR(input, 1, 10)   AS selector,
       SUBSTR(input, 11, 64)  AS arg1,
       SUBSTR(input, 75, 64)  AS arg2,
       SUBSTR(input, 139, 64) AS arg3,
       LENGTH(input)          AS input_len
FROM `bigquery-public-data.crypto_ethereum.transactions`
WHERE DATE(block_timestamp) = '2026-09-01'
  AND SUBSTR(input, 1, 10) IN ('0xa9059cbb', '0x095ea7b3', '0x23b872dd')
```
Save the result as CSV into the same `data/` folder, delete `corpus_eth/`, and rerun.
`ingest.py` recognises transaction exports by their header and matches them to days by
`block_timestamp`; calldata files must contain the date (`YYYYMMDD`) in their name. With
the four files now in `data/` (two days, transactions + calldata), full-day batches are
401,223 / 89,380 / 42,309 (Sep 1) and 269,610 / 30,150 / 21,378 (Sep 13).

### Commands
```sh
cd ~/conduit
bash tools/run_dataset.sh data --quick      # first run, ~10-20 min
bash tools/run_dataset.sh data              # final numbers for the report
# results: results/phase8/<host>-real-large/summary.md and figures

# individual steps
python3 ingest.py --tx data --out corpus_eth
export CONDUIT_CORPUS=corpus_eth                 # tools use corpus_eth/superblocks.json
S=""
build/conduit_gpu verify  $S                      # GPU == CPU on 10k ... full day
build/conduit_run         $S --id 2026-09-01/full --impl cuda    # whole day on the GPU
build/conduit_gpu execute $S --full               # execute all 2,013,158 transactions
build/conduit_gpu bench   $S --csv bench.csv      # CPU vs OpenMP vs GPU, 10k ... 2M
```

## 9. The visualizer

`visualizer/index.html` (open it directly in a browser, no server) steps through every
stage on a small block, with live animations (Pause, Restart, slider, speed) for the
parallel steps: read/write sets, records, the radix sort, group and run heads, the
reduced edge rule, CSR, DAG, levels, the conflict graph, Jones–Plassmann rounds (index or
random priority) and the four execution strategies as Gantt charts. The data come from
`build/conduit_trace`, which runs the CPU stages and the GPU kernel bodies on the block,
checks they agree, and writes `visualizer/trace_data.js`:

```sh
build/conduit_trace --example                                   # 12-transaction teaching block
build/conduit_trace --id 2026-09-01/10k/000 --auto --limit 14 --append
build/conduit_trace --id 2026-09-13/10k/000 --auto --limit 16 --append
```
`--auto` picks the window of that size with the most conflicts; `--offset` picks one by hand.
