# RTX 4050 laptop, both days with calldata, all sizes (quick run)

- Machine: see `machine.txt` (Intel i7-13620H, 16 threads; NVIDIA GeForce RTX 4050
  Laptop GPU, 6 GB; driver/runtime 13.2). `gpu_info.txt` and `bench.log` show
  `backend cuda`: the GPU rows are real GPU measurements.
- Corpus: 3,489,388 transactions of 2026-09-01 and 2026-09-13 with calldata
  (`ingest.txt`: 84 of 1,254,086 token calls without decoded operands).
- Correctness: 852 GPU checks and 38 unit tests passed (`verify.txt`, `tests.txt`).
  The bit-identity sweep and the Python validator were **not** part of this run
  (handover.md, Fix 2); `tools/run_dataset.sh` now runs both and stops on failure.
- **Repetitions: 1 warm-up + 2 measured runs. Provisional.** The standard is
  2 + 7 (handover.md, Fix 1); re-run with `bash tools/run_dataset.sh data`.
- `summary.md` and the figures were regenerated on 2026-11-04 by the current
  `report_gpu.py` from the unchanged CSVs, so the headline is wall time
  (handover.md, Fix 3). The original run's folder name had a space ("… Test2");
  it was renamed (Fix 4).
