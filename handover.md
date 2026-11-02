# Phase 8–10: what to fix before the results go into findings.md

Review of the calldata run (commit `eb27431`, results in
`results/dataset/sucheth-katte-ASUS-TUF-Gaming-F15-FX507VU-FX507VU Test2/`),
plus items still open from the first review of `48d9b28` and `32ea4ba`.

## What's already good

- **The corpus matches Phase 6.** Your corpus has both days with calldata
  (3,489,388 transactions; 84 of 1.25M token calls undecoded). All 36 checks
  that overlap with Phase 6 are identical on size, records, edges and levels:
  the 12 median super-blocks × 3 policies.
- **The GPU was really used** (`backend cuda`; 0.75–1.3 GB of device memory at
  full-day size). All 852 GPU checks and all 38 unit tests pass.
- **The results cover Phases 8 and 9.**
  - **Crossover:** the GPU, with transfers charged, beats seq-radix from 1–5k
    transactions and OpenMP from 1k.
  - **Skew recovery:** 19–43x, with the USDT contract key as the hot group.
  - **GPU levelling:** Kahn on the GPU is 8–24x slower than the CPU sweep,
    which agrees with Phase 7.

None of the conclusions is expected to change. The fixes are about making the
numbers clean enough to publish.

## Fix 1: re-run with the full repetition count

The run used 1 warm-up + 2 measured runs. The standard is **2 warm-up + 7
measured, median reported**. The noise is visible: seq-radix at 80k P1 on
Sep 1 is 22.3 ms, against 6.1 ms at 60k and 11.6 ms at 100k. That one outlier
produces a spurious 15.6x speedup.

Before re-running:
- Plug the laptop in and select the performance power mode. `machine.txt`
  showed the CPU at 66% clock, and the earlier run had 278 MB of RAM free.
- Close other applications, and record the power mode in `machine.txt`.

## Fix 2: run the Python validator and the full bit-identity sweep

Non-negotiable 1: no timing without correctness, and the Python validator must
accept the output. `run_dataset.sh` skips both checks. `run_phase8.sh` runs
them, but only on `corpus/` built by the original ingest:

```sh
# put the four CSVs in data/ under these exact names
data/tx_20260901.csv   data/calldata_20260901.csv
data/tx_20260913.csv   data/calldata_20260913.csv

python3 ingest.py                 # builds corpus/ (no arguments = original path)
python3 verify_corpus.py
python3 report_oracle.py          # builds oracle_ref/, needed by the validator
tools/run_phase8.sh --profile     # 2 + 7 runs; --profile covers the Phase 10 profiler pass
```

Then check, in `results/phase8/<host>-real/`:
- `identical.txt` ends with `ALL IDENTICAL`;
- `validator.txt` ends with `ALL CHECKS PASSED`.

`run_phase8.sh` doesn't stop if either of those fails, so read both before
trusting any timing.

`run_phase8.sh` only covers the standard 10k–100k super-blocks. For the
250k / 500k / 1M / full-day sizes, re-run `tools/run_dataset.sh` too:
- without `--quick`;
- after setting `RUNS=7; WARMUP=2` at line 21.

## Fix 3: headline wall time, not the sum of stages

`report_gpu.py` uses `t_e2e_ms`, the sum of the GPU stage timings, for the
speedup table, the crossover and the figure. Total wall time (`t_wall_ms`) is
**11–35% higher**.
- The gap is probably device memory allocation, which the CPU timings include.
  Please confirm what it is.
- Use `t_wall_ms` for the headline speedups and the crossover, and keep
  `t_e2e_ms` as the breakdown.
- On the current data this moves the Sep 1 P2/P3 crossover against seq-radix
  from 1k to 2k. Nothing else changes.

## Fix 4: tidy the results folders

- Put the new results in `results/phase8/<host>-real/` (what `run_phase8.sh`
  writes) and `results/phase8/<host>-real-large/` for the `run_dataset.sh` run.
- No spaces in folder names (`… Test2`).
- Sucheth's first run is committed twice, under `results/sucheth-…/` and
  `results/phase8/sucheth-…/`. Keep one copy.
- Drop the copies of the Phase 6 figures and `phase6_summary.md` from the
  results folder; they already live in `results/`.
- The no-calldata runs can stay, labelled "no calldata: P2/P3 collapse to P1"
  in their folder names or a README.

## Still open from the first review

1. **README.md framing needs agreement before it's pushed further.** The
   project's contribution is characterisation, not a speedup over prior work.
   - The title and intro reframe the project as "parallel transaction
     execution" that "actually runs them in parallel".
   - "DAG dataflow (ours)… a 5.5× better schedule" claims a standard technique
     as ours.
   - The headline "8–17× faster" is against seq-std and from synthetic data,
     read off a figure. Use the real-data numbers from Fixes 1–3, against
     both baselines and OpenMP.
   - The execution "measurement" uses a toy state machine where work is
     proportional to gas. Label it as a model, not real execution.
   - "Scales further, up to the ~11× ceiling" is a prediction; mark it as one.
2. **Settle the emulation contradiction.**
   `results/phase8/sucheth-…/README.md` says the GPU wasn't visible and the
   GPU rows are emulation, but `bench.log` says `backend cuda`. Confirm which
   is true and correct the README.
3. **Update findings.md Phase 8.** It still says "no GPU timing is reported
   yet". Replace that once Fixes 1–3 are done, and keep the original sentence
   with a correction note (see the corrections log at the top of findings.md
   for the format).
   - The Phase 8b section uses the no-calldata export. Either re-run it on
     the calldata corpus or say clearly in the section that P2/P3 equal P1
     there.
4. **File modes.** `48d9b28` marked 57 files executable (`100644 → 100755`).
   Run `git config core.fileMode false` on Windows/WSL, and restore the modes:
   `git ls-files -s | awk '$1=="100755"{print $4}'`, then `chmod 644` on
   everything that isn't a script.

## Reminders

- **Scope every claim:** two days of Ethereum mainnet, on one laptop GPU
  (RTX 4050, 6 GB).
- **Report speedups against seq-std, seq-radix and OpenMP, labelled.** Part of
  any gain over seq-std is the change to radix sort, not the hardware.
- **Ask before changing** the conflict rule, the edge-reduction rule or the
  policies, before re-running anything that would change Phase 6's results,
  and before framing a result as beating prior work.
