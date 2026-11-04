#!/usr/bin/env python3
"""
Phase 6 - measurement sweep for Question A: how much parallelism is in real
Ethereum blocks?

Runs the verified sequential C++ pipeline (build/conduit_run) over every
super-block in the manifest under P1, P2 and P3: 736 x 3 = 2,208 runs. Each
run's schedule is checked by validate_fast() against access sets from the
Python policies, and against the single-sender nonce floor, so every number
in the results rests on a verified schedule.

Outputs, in results/:
  phase6_runs.csv        one row per (super-block, policy), from conduit_run
  phase6_validation.csv  one row per run: validator verdict, floor check

Timings come from 1 warm-up and 3 timed runs (median). They are recorded
for completeness; this phase's result is the schedule shape, not speed.

Usage: python3 measure_parallelism.py   (after building build/conduit_run)
"""
import csv
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "tools"))
from access_sets import Selectors, infer, load_corpus  # noqa: E402
from oracle import validate_fast  # noqa: E402
from superblocks import SuperBlocks  # noqa: E402
from verify_cpp import read_pipeline  # noqa: E402

RUN = ROOT / "build" / "conduit_run"
OUT = ROOT / "results"
RUNS_CSV = OUT / "phase6_runs.csv"
VALID_CSV = OUT / "phase6_validation.csv"


def main():
    if not RUN.exists():
        sys.exit("build build/conduit_run first (cmake --build build)")
    OUT.mkdir(exist_ok=True)
    RUNS_CSV.unlink(missing_ok=True)
    sb = SuperBlocks()
    sels = Selectors(load_corpus()[3])
    by_start = {(e["tx_start"], e["tx_count"]): e for e in sb.manifest["superblocks"]}
    groups = list(dict.fromkeys((c["date"], c["target"]) for c in sb.manifest["configs"]))

    t0 = time.time()
    rows, failures = [], 0
    for date, target in groups:
        with tempfile.TemporaryDirectory() as tmp:
            subprocess.run([str(RUN), "--all", "--date", date, "--target", str(target),
                            "--policy", "all", "--runs", "3", "--warmup", "1",
                            "--dump", tmp, "--csv", str(RUNS_CSV), "--quiet",
                            "--corpus", str(ROOT / "corpus"),
                            "--manifest", str(ROOT / "superblocks.json")],
                           check=True)
            dumps = sorted(Path(tmp).glob("*.bin"))
            for f in dumps:
                c = read_pipeline(f)
                entry = by_start[(c["tx_start"], c["n"])]
                sets = infer(np.array(sb.load(entry)), c["policy"], sels)
                ok, bad = validate_fast(sets, c["levels"])
                cp = int(c["levels"].max()) + 1
                above = cp >= entry["max_sender_tx"]
                failures += (not ok) + (not above)
                rows.append({"id": entry["id"], "policy": c["policy"],
                             "valid": int(ok), "levels": cp,
                             "floor": entry["max_sender_tx"], "at_or_above_floor": int(above),
                             "first_violation": "" if ok else str(bad[0])})
        print(f"  {date} {target:>7,}: {len(dumps):>4} runs validated  "
              f"({time.time() - t0:.0f} s)", flush=True)

    with open(VALID_CSV, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)
    print(f"\n{len(rows):,} runs, {failures} failed checks; "
          f"wrote {RUNS_CSV.relative_to(ROOT)} and {VALID_CSV.relative_to(ROOT)}")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
