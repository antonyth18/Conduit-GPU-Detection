#!/usr/bin/env bash
# Run the WHOLE project on your own transaction export (BigQuery CSV).
#
#   bash tools/run_dataset.sh [DATA] [--quick] [--out corpus_eth] [--threads 1,2,4,8]
#
#   DATA      a CSV file or a folder of CSVs (default: data/). A calldata export
#             in the same folder is picked up automatically.
#   --quick   fewer repetitions (first try)
#
# Steps (each must pass before the next runs):
#   1 machine record   2 ingest (CSV -> binary corpus, every transaction kept)
#   3 build            4 unit tests          5 GPU correctness on real blocks
#   6 timing sweep: seq / OpenMP / GPU from 10k transactions up to the full day
#   7 schedule quality (Jones-Plassmann vs levels, up to 250k)
#   8 EXECUTION of every transaction of the full day: serial vs level-batched
#     vs DAG-dataflow on all CPU threads, final state checked against serial
#   9 report: results/dataset/<host>/summary.md + figures
set -euo pipefail
cd "$(dirname "$0")/.."

DATA=data; OUTC=corpus_eth; RUNS=5; WARMUP=1; EXECRUNS=3; THREADS=""
while (( $# )); do
  case "$1" in
    --quick) RUNS=2; WARMUP=1; EXECRUNS=1 ;;
    --out) OUTC=$2; shift ;;
    --threads) THREADS=$2; shift ;;
    -*) echo "unknown option $1"; exit 2 ;;
    *) DATA=$1 ;;
  esac
  shift
done

HOST=$(hostname -s 2>/dev/null || hostname)
OUT="results/dataset/$HOST"
mkdir -p "$OUT"
NPROC=$(nproc)
export OMP_PROC_BIND=close OMP_PLACES=cores
if [[ -z "$THREADS" ]]; then
  THREADS=1; t=2; while (( t < NPROC )); do THREADS="$THREADS,$t"; t=$((t * 2)); done
  (( NPROC > 1 )) && THREADS="$THREADS,$NPROC"
fi

echo "== 1. machine"
{
  echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)   host: $HOST   data: $DATA"
  echo "threads swept: $THREADS   runs: $WARMUP warm-up + $RUNS measured"
  uname -a; grep -qi microsoft /proc/version && echo "WSL2: yes"
  lscpu | grep -E "Model name|^CPU\(s\)|Thread|Core" || true
  free -h | head -2
  nvidia-smi --query-gpu=name,memory.total,driver_version --format=csv 2>/dev/null || echo "nvidia-smi: not found"
  nvcc --version 2>/dev/null | tail -1 || echo "nvcc: not found"
} | tee "$OUT/machine.txt"

echo "== 2. ingest $DATA -> $OUTC/"
if [[ ! -f "$OUTC/superblocks.json" || -n "$(find "$DATA" -name '*.csv' -newer "$OUTC/superblocks.json" 2>/dev/null)" ]]; then
  python3 ingest.py --tx "$DATA" --out "$OUTC" | tee "$OUT/ingest.txt"
else
  echo "   $OUTC/ is up to date (delete it to re-ingest)"
fi
C=(--corpus "$OUTC" --manifest "$OUTC/superblocks.json")

echo "== 3. build"
NVCC=$(command -v nvcc || ls /usr/local/cuda*/bin/nvcc 2>/dev/null | head -1 || true)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release ${NVCC:+-DCMAKE_CUDA_COMPILER=$NVCC} > "$OUT/build.log"
cmake --build build -j "$NPROC" >> "$OUT/build.log" 2>&1 || { tail -30 "$OUT/build.log"; exit 1; }
grep -E "conduit: (CUDA|OpenMP|no CUDA)" "$OUT/build.log" || true
build/conduit_gpu info | tee "$OUT/gpu_info.txt"
BACKEND=cuda
JPMAX=250000
grep -q "backend cuda" "$OUT/gpu_info.txt" || { BACKEND=emu; JPMAX=20000; echo "WARNING: no CUDA device/build; GPU times will be host EMULATION"; }

echo "== 4. unit tests"
build/conduit_tests > "$OUT/tests.txt" 2>&1 || true
tail -1 "$OUT/tests.txt"
grep -q " 0 failed checks" "$OUT/tests.txt" || { echo "unit tests failed: see $OUT/tests.txt"; exit 1; }

echo "== 5. GPU correctness on your blocks (10k ... full day)"
build/conduit_gpu verify "${C[@]}" --backend "$BACKEND" --jp-max "$JPMAX" | tee "$OUT/verify.txt" | tail -2
grep -q "ALL GPU CHECKS PASSED" "$OUT/verify.txt" || { echo "verification failed: NOT timing"; exit 1; }

echo "== 6. timing sweep (seq / OpenMP / GPU), 10k ... full day"
build/conduit_gpu bench "${C[@]}" --backend "$BACKEND" --jp-max "$JPMAX" --csv "$OUT/bench.csv" --threads "$THREADS" \
  --runs "$RUNS" --warmup "$WARMUP" | tee "$OUT/bench.log" | tail -1

echo "== 7. schedule quality (levels vs Jones-Plassmann)"
build/conduit_gpu schedule "${C[@]}" --backend "$BACKEND" --jp-max "$JPMAX" --csv "$OUT/schedule.csv" > "$OUT/schedule.txt"
tail -1 "$OUT/schedule.txt"

echo "== 8. parallel EXECUTION of every transaction of the full day"
build/conduit_gpu execute "${C[@]}" --full --backend "$BACKEND" --threads "$THREADS" \
  --exec-runs "$EXECRUNS" --csv "$OUT/execute.csv" | tee "$OUT/execute.txt" | grep -E "===|END TO END"

echo "== 9. report"
python3 report_gpu.py "$OUT"
echo
echo "done. Open: $OUT/summary.md, $OUT/fig_e2e_vs_size.png, $OUT/fig_execution_speedup.png"
