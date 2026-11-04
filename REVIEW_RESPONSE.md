# Response to the Phase 8–10 review (`handover.md`)

Each item from the review, what changed, and where to check it. Nothing in the
conflict rule, the edge-reduction rule or the three policies was changed, and
Phase 6's results are untouched (see Fix 2).

## Fix 1: full repetition count

- `tools/run_phase8.sh` and `tools/run_dataset.sh` now default to **2 warm-up
  + 7 measured runs**, median reported; no line needs editing any more.
  `conduit_gpu` itself defaults to `--runs 7 --warmup 2`.
- `--quick` (1 + 2) remains for smoke tests only, and its output folder ends
  in `-quick` so it cannot be mistaken for final numbers.
- Above 250k transactions the bench uses 1 warm-up + 3 runs per configuration
  so that a full-day factorial sweep finishes in reasonable time; `runs` and
  `warmup` are written into every `bench.csv` row, so the table shows it.
- `machine.txt` now records AC/battery state, the Linux governor, the Windows
  power plan (through `powercfg.exe` under WSL), GPU clocks, power limit,
  PCIe link, free memory and the full thread list
  (`tools/lib_machine.sh`).
- **Done:** `results/phase8/Nikhil-PC-real/` and `Nikhil-PC-real-large/` are
  2 + 7 runs with every gate passing; `README.md` and `findings.md` now use
  them. One shortfall: Windows was on the *Balanced* power plan, not *Best
  performance* (recorded in `machine.txt`). It shows at small sizes (the GPU's
  fixed cost per call is 0.5–1.2 ms here against 0.15–0.19 ms on the first
  laptop); full-day results agree with the first laptop within 1–6%.

## Fix 2: Python validator and full bit-identity sweep

- Both scripts now run the same gates in the same order and **stop at the
  first failure** (`set -euo pipefail` plus a check of each gate's final
  line):
  1. `build/conduit_tests` (39 tests)
  2. `conduit_gpu stages`, new: each GPU stage (extract, sort, detect, CSR,
     levels) against the matching CPU stage, for all three detection
     mappings
  3. `conduit_gpu verify`
  4. `conduit_bench identical`, every super-block
  5. `report_oracle.py` + `tools/verify_cpp.py --impl cuda` (Python
     validator)
- `run_dataset.sh` previously skipped 4 and 5; it now runs both on the
  large corpus.
- **The CSV names no longer matter.** `ingest.py` recognises transaction
  exports by their header and assigns them to days by `block_timestamp`;
  calldata files are matched by the `YYYYMMDD` in their name. The four files
  in `data/` (one of them still named `bq-results-…csv`) are mapped
  automatically and the mapping is printed.
- **Phase 6 is unchanged.** Rebuilding `corpus/` from these exports gives
  the same 736 of 736 super-block boundaries. The `tx.bin` hash differs only
  because address ids follow CSV row order, so `ingest.py` now writes a
  corpus-local `corpus/superblocks.json` and every tool prefers it
  (`CONDUIT_CORPUS`, `CONDUIT_MANIFEST` override). The Python validator
  passes on the rebuilt corpus.

## Fix 3: wall time as the headline

- `report_gpu.py` uses `t_wall_ms` for the speedup table, the crossover
  (measured and modelled) and the figure. `t_e2e_ms` (the sum of stage
  timings) is kept as the breakdown, next to `device` and `kernels`.
- **What the gap is.** Two new columns separate it: `t_host_prep_ms`
  (copying the input into pinned host buffers, before the first event) and
  `t_host_post_ms` (copying results out of pinned buffers, building the
  result struct). Device allocation is done once per call and is inside the
  first event. In the final run, at full day,
  host prep is 8–13 ms and host post 1.4–2.5 ms; together they are the whole
  10–16 ms gap between wall and device time.
- `conduit_gpu pcie` measures pinned and pageable H2D/D2H bandwidth, launch
  latency and sync latency, so the transfer table shows a prediction from
  bytes next to the measured transfer.
- On the first laptop's data the Sep 1 P2/P3 crossover against seq-radix
  moved from 1k to 2k, as predicted. In the final run it is 10k–20k, because
  that machine's fixed GPU cost per call is higher (see Fix 1).

## Fix 4: results folders

| Before | After |
|---|---|
| `results/sucheth-…-FX507VU/` and `results/phase8/sucheth-…/` (same run twice) | one copy: `results/phase8/sucheth-katte-ASUS-TUF-Gaming-real-large-quick-no-calldata/` |
| `results/dataset/sucheth-…-FX507VU Test2/` (space in name) | `results/phase8/sucheth-katte-ASUS-TUF-Gaming-real-large-quick/` |
| `results/dataset/cloud-2vcpu-gpu-emulated/` | `results/phase8/cloud-2vcpu-emulated-no-calldata/` |
| Phase 6 figure copies and `phase6_summary.md` inside results folders | removed (they live in `results/`) |

The final runs are `results/phase8/Nikhil-PC-real/`,
`results/phase8/Nikhil-PC-real-large/` and `results/phase10/Nikhil-PC-profile/`.
Each renamed folder has a README saying what it is: machine, backend,
repetitions, calldata or not, and whether its GPU columns are timings or
emulation. New runs go to `results/phase8/<host>-real/` (`run_phase8.sh`)
and `results/phase8/<host>-real-large/` (`run_dataset.sh`).

## Still open 1: README framing

`README.md` is rewritten:
- Title and intro frame the project as **characterisation**: what computing
  the schedule costs on CPU and GPU, and when offloading pays off. No claim
  of beating Block-STM or other prior work.
- DAG dataflow is presented as standard list scheduling
  [Graham 1969; Kwok & Ahmad 1999], not as ours.
- Headline numbers are real data, wall time, 2 + 7 runs, against **seq-std,
  seq-radix and OpenMP, labelled**, with the note that part of the seq-std gain
  is the radix sort.
- The execution section is titled "Execution model (not a measurement of an
  Ethereum client)".
- The "~11× ceiling" is gone; the bounds are stated as model bounds, and
  `findings.md` has a "Predictions still to test" list.
- All algorithms are cited; `REFERENCES.md` lists every source and where it
  is used in the code.

## Still open 2: the emulation contradiction

Settled: **the laptop run used the GPU.** `gpu_info.txt` and `bench.log` say
`backend cuda`, and `verify` passed 852 and 1,710 checks on the device. The
old README sentence was wrong and is corrected in the folder's README. Only the
cloud 2-vCPU folder is emulated, and its name now says so. `findings.md`
records this as correction C11.

## Still open 3: findings.md Phase 8

- The original "No GPU timing is reported here yet" paragraph stays, with a
  **Correction (C11)** note beside it, following the corrections-log format.
- The three `[TODO]` headings are replaced by Phase 8, Phase 9, Stage 4 and
  Phase 10 results sections from the final 2 + 7 run, including the profiler
  pass.
- Phase 8b keeps its no-calldata numbers with a **Correction (C12)** note:
  P2/P3 ≈ P1 there because of the missing calldata, and the calldata numbers
  are given.

## Still open 4: file modes

All tracked files are back to `100644` except the shell scripts in `tools/`
(`100755`). On Windows/WSL run once:

```sh
git config core.fileMode false
```

## Added beyond the review

- **Phase 9, third mapping:** one thread per record (`--configs full`,
  `conduit_gpu skew`), between per-group and per-edge.
- **`conduit_gpu skew`:** names the hot key (the USDT contract account),
  reports work imbalance per mapping and the recovery.
- **Phase 10 profiler pass:** `tools/profile.sh` + `report_profile.py`
  (Nsight Systems timeline stats; Nsight Compute occupancy, memory
  throughput, warp execution efficiency, branch efficiency per kernel).
- **Visualizer** (`visualizer/index.html`, recorder `tools/conduit_trace`):
  every stage on a small block, recorded from the real code.
- **`REFERENCES.md`**: 26 sources with a code-to-paper map.
