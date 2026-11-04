#!/usr/bin/env bash
# Phases 8-10 on your own BigQuery exports, at every size up to the full day.
#
#   bash tools/run_dataset.sh [DATA] [--quick] [--profile] [--out corpus_eth] [--threads LIST]
#
#   DATA       a CSV file or folder of CSVs (default data/). Transaction exports
#              and calldata exports are told apart by their headers.
#   --quick    1 warm-up + 2 measured runs (smoke test only; NOT for the report)
#   --profile  also run the Nsight profiler pass (tools/profile.sh)
#
# Standard: 2 warm-up + 7 measured runs, median reported (handover Fix 1).
# Every timing step runs only after every correctness gate passed:
#   unit tests -> GPU stage-by-stage -> GPU == CPU on all sizes ->
#   bit-identity sweep over every super-block (seq/omp/gpu) ->
#   Python oracle + validator.
# Output: results/phase8/<host>-real-large/   (handover Fix 4)
set -euo pipefail
cd "$(dirname "$0")/.."
source tools/lib_machine.sh

DATA=data; OUTC=corpus_eth; RUNS=7; WARMUP=2; EXECRUNS=3; THREADS=""; PROFILE=0; QUICK=0
while (( $# )); do
  case "$1" in
    --quick) RUNS=2; WARMUP=1; EXECRUNS=1; QUICK=1 ;;
    --profile) PROFILE=1 ;;
    --out) OUTC=$2; shift ;;
    --threads) THREADS=$2; shift ;;
    -*) echo "unknown option $1"; exit 2 ;;
    *) DATA=$1 ;;
  esac
  shift
done

HOST=$(host_tag)
OUT="results/phase8/$HOST-real-large"; (( QUICK )) && OUT="$OUT-quick"
mkdir -p "$OUT"
export OMP_PROC_BIND=close OMP_PLACES=cores
[[ -z "$THREADS" ]] && THREADS=$(thread_list)
NPROC=$(nproc)

echo "== 1. build"
NVCC=$(command -v nvcc || ls /usr/local/cuda*/bin/nvcc 2>/dev/null | head -1 || true)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release ${NVCC:+-DCMAKE_CUDA_COMPILER=$NVCC} > "$OUT/build.log"
cmake --build build -j "$NPROC" >> "$OUT/build.log" 2>&1 || { tail -30 "$OUT/build.log"; exit 1; }
grep -E "conduit: (CUDA|OpenMP|no CUDA)" "$OUT/build.log" || true
build/conduit_gpu info | tee "$OUT/gpu_info.txt"
BACKEND=cuda; JPMAX=250000
grep -q "backend cuda" "$OUT/gpu_info.txt" || { BACKEND=emu; JPMAX=20000; echo "WARNING: no CUDA device/build; GPU rows will be host EMULATION (correctness only)"; }

echo "== 2. machine"
machine_info "$OUT" "run_dataset.sh data=$DATA runs=$WARMUP+$RUNS threads=$THREADS backend=$BACKEND"

echo "== 3. ingest $DATA -> $OUTC/"
if [[ ! -f "$OUTC/superblocks.json" || -n "$(find "$DATA" -name '*.csv' -newer "$OUTC/superblocks.json" 2>/dev/null)" ]]; then
  python3 ingest.py --tx "$DATA" --out "$OUTC" | tee "$OUT/ingest.txt"
else
  echo "   $OUTC/ is up to date (delete it to re-ingest)" | tee "$OUT/ingest.txt"
fi
export CONDUIT_CORPUS=$OUTC CONDUIT_MANIFEST=$OUTC/superblocks.json
C=(--corpus "$OUTC" --manifest "$OUTC/superblocks.json")

echo "== 4. unit tests"
build/conduit_tests > "$OUT/tests.txt" 2>&1 || true
tail -1 "$OUT/tests.txt"
need "$OUT/tests.txt" " 0 failed checks" "unit tests failed"

echo "== 5. Phase 8: GPU stages one by one against the CPU stages"
build/conduit_gpu stages "${C[@]}" --backend "$BACKEND" > "$OUT/stages.txt"
tail -1 "$OUT/stages.txt"
need "$OUT/stages.txt" "ALL STAGES IDENTICAL" "a GPU stage differs from the CPU stage"

echo "== 6. GPU == CPU on every size, 10k ... full day (JP up to $JPMAX)"
build/conduit_gpu verify "${C[@]}" --backend "$BACKEND" --jp-max "$JPMAX" > "$OUT/verify.txt"
tail -1 "$OUT/verify.txt"
need "$OUT/verify.txt" "ALL GPU CHECKS PASSED" "GPU verification failed"

echo "== 7. bit-identity sweep, every super-block, seq / OpenMP / GPU (long)"
build/conduit_bench identical "${C[@]}" --threads "1,4,$NPROC" > "$OUT/identical.txt" 2>&1
tail -1 "$OUT/identical.txt"
need "$OUT/identical.txt" "ALL IDENTICAL" "bit-identity sweep found a mismatch"

echo "== 8. Python oracle + schedule validator on the GPU output"
python3 report_oracle.py > "$OUT/oracle.txt"
python3 tools/verify_cpp.py --impl "$([[ $BACKEND == cuda ]] && echo cuda || echo gpu-emu)" > "$OUT/validator.txt" || true
tail -1 "$OUT/validator.txt"
need "$OUT/validator.txt" "ALL CHECKS PASSED" "the Python validator rejected the output"

echo "== 9. PCIe / launch microbenchmark (transfer arithmetic)"
if [[ $BACKEND == cuda ]]; then
  build/conduit_gpu pcie --csv "$OUT/pcie.csv" > "$OUT/pcie.txt"; tail -2 "$OUT/pcie.txt"
fi

echo "== 10. factorial timing sweep: implementation x size x policy ($WARMUP+$RUNS runs, long)"
build/conduit_gpu bench "${C[@]}" --backend "$BACKEND" --jp-max "$JPMAX" --csv "$OUT/bench.csv" \
  --threads "$THREADS" --configs full --runs "$RUNS" --warmup "$WARMUP" > "$OUT/bench.log"
tail -1 "$OUT/bench.log"

echo "== 11. Phase 9 skew: before (group) -> record -> balanced"
build/conduit_gpu skew "${C[@]}" --backend "$BACKEND" --csv "$OUT/skew.csv" \
  --runs "$RUNS" --warmup "$WARMUP" > "$OUT/skew.txt"
tail -2 "$OUT/skew.txt" | head -1

echo "== 12. schedule quality (levels vs Jones-Plassmann)"
build/conduit_gpu schedule "${C[@]}" --backend "$BACKEND" --jp-max "$JPMAX" --csv "$OUT/schedule.csv" > "$OUT/schedule.txt"

echo "== 13. execution model: every transaction of each full day"
build/conduit_gpu execute "${C[@]}" --full --backend "$BACKEND" --threads "$THREADS" \
  --exec-runs "$EXECRUNS" --csv "$OUT/execute.csv" > "$OUT/execute.txt"
grep -E "END TO END" "$OUT/execute.txt" | cut -c1-120

if (( PROFILE )) && [[ $BACKEND == cuda ]]; then
  echo "== 14. Phase 10 profiler pass"
  bash tools/profile.sh --corpus "$OUTC" --out "$OUT/profile" || echo "profiler pass failed (see above); continuing"
fi

echo "== report"
python3 report_gpu.py "$OUT"
echo
echo "done. Open $OUT/summary.md"
(( QUICK )) && echo "NOTE: --quick results are a smoke test; re-run without --quick for the report."
exit 0
