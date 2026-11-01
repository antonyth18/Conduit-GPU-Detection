#!/usr/bin/env bash
# Phases 8-10: one-command GPU run, meant for the benchmark machine (RTX 4050).
#
#   tools/run_phase8.sh [--synthetic] [--quick] [--profile]
#
#   --synthetic  use the synthetic corpus (generated into corpus_synth/ if
#                missing) instead of the real one in corpus/. Also the default
#                when corpus/ does not exist.
#   --quick      1 warm-up + 3 measured runs instead of 2 + 7
#   --profile    also run Nsight Compute / Nsight Systems on the largest
#                super-block (needs ncu / nsys on PATH)
#
# Order matters (non-negotiable 1): build -> unit tests -> correctness sweeps;
# nothing is timed unless every check passed. Results: results/phase8/<host>/.
set -euo pipefail
cd "$(dirname "$0")/.."

RUNS=7; WARMUP=2; SYNTH=0; PROFILE=0
for a in "$@"; do
  case "$a" in
    --quick) RUNS=3; WARMUP=1 ;;
    --synthetic) SYNTH=1 ;;
    --profile) PROFILE=1 ;;
    *) echo "unknown option $a"; exit 2 ;;
  esac
done
[[ -f corpus/tx.bin ]] || SYNTH=1

if (( SYNTH )); then
  CORPUS=corpus_synth; MANIFEST=corpus_synth/superblocks.json; TAG=synthetic
  [[ -f $MANIFEST ]] || python3 tools/synth_corpus.py --out corpus_synth
else
  CORPUS=corpus; MANIFEST=superblocks.json; TAG=real
fi
C=(--corpus "$CORPUS" --manifest "$MANIFEST")

HOST=$(hostname -s 2>/dev/null || hostname)
OUT="results/phase8/$HOST-$TAG"
mkdir -p "$OUT"
NPROC=$(nproc)
export OMP_PROC_BIND=close OMP_PLACES=cores
THREADS=1; t=2; while (( t < NPROC )); do THREADS="$THREADS,$t"; t=$((t * 2)); done
THREADS="$THREADS,$NPROC"

{
  echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "host: $HOST   corpus: $CORPUS ($TAG)"
  echo "git: $(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
  echo "runs: $WARMUP warm-up + $RUNS measured, median reported; omp threads $THREADS"
  echo "OMP_PROC_BIND=$OMP_PROC_BIND OMP_PLACES=$OMP_PLACES"
  echo; uname -a
  grep -i microsoft /proc/version >/dev/null 2>&1 && echo "WSL2: yes"
  echo; head -4 /etc/os-release 2>/dev/null
  echo; lscpu | grep -E "Model name|^CPU\(s\)|Thread|Core|Socket|MHz|L2|L3" || true
  echo; free -h
  echo; nvidia-smi --query-gpu=name,memory.total,driver_version,pcie.link.gen.max,pcie.link.width.max,clocks.max.sm --format=csv 2>/dev/null || echo "nvidia-smi: not found"
  echo; nvcc --version 2>/dev/null | tail -2 || echo "nvcc: not found"
  echo; c++ --version | head -1; cmake --version | head -1
} > "$OUT/machine.txt"
echo "machine info -> $OUT/machine.txt"

echo "== build"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release ${CUDA_ARCH:+-DCMAKE_CUDA_ARCHITECTURES=$CUDA_ARCH} > "$OUT/build.log"
cmake --build build -j "$NPROC" >> "$OUT/build.log" 2>&1
grep -E "conduit: (CUDA|OpenMP|no CUDA)" "$OUT/build.log" || true
build/conduit_gpu info | tee "$OUT/gpu_info.txt"
BACKEND=cuda
grep -q "backend cuda" "$OUT/gpu_info.txt" || { BACKEND=emu; echo "WARNING: no CUDA device/build: GPU numbers will be EMULATION"; }

echo "== unit tests"
build/conduit_tests | tee "$OUT/tests.txt" | tail -1
grep -q " 0 failed checks" "$OUT/tests.txt" || { echo "unit tests failed"; exit 1; }

echo "== GPU correctness (bit-identity vs seq-std, JP batch checks, brute oracle)"
build/conduit_gpu verify "${C[@]}" --backend "$BACKEND" | tee "$OUT/verify.txt" | tail -2
grep -q "ALL GPU CHECKS PASSED" "$OUT/verify.txt" || { echo "GPU verification failed: NOT timing"; exit 1; }

if (( ! SYNTH )); then
  echo "== bit-identity sweep over every super-block (seq/omp/gpu)"
  build/conduit_bench identical --threads "1,4,$NPROC" | tee "$OUT/identical.txt" | tail -2
  echo "== Python validator on CUDA output"
  python3 tools/verify_cpp.py --impl cuda | tee "$OUT/validator.txt" | tail -1
fi

echo "== timing sweep (long)"
build/conduit_gpu bench "${C[@]}" --backend "$BACKEND" --csv "$OUT/bench.csv" --threads "$THREADS" \
  --configs full --runs "$RUNS" --warmup "$WARMUP" | tee "$OUT/bench.log" | tail -2

echo "== schedule quality + execution model"
build/conduit_gpu schedule "${C[@]}" --backend "$BACKEND" --csv "$OUT/schedule.csv" | tee "$OUT/schedule.txt" | tail -2

if (( PROFILE )) && [[ $BACKEND == cuda ]]; then
  BIG=$(python3 - "$MANIFEST" <<'EOF'
import json, sys
m = json.load(open(sys.argv[1]))
sb = [s for s in m["superblocks"] if s["target"] == max(m["targets"])]
print(sb[len(sb) // 2]["id"])
EOF
)
  echo "== profiling $BIG"
  if command -v nsys >/dev/null; then
    nsys profile -o "$OUT/nsys_p3" --force-overwrite true \
      build/conduit_run "${C[@]}" --impl cuda --id "$BIG" --policy P3 --runs 3 --warmup 1 --quiet
  fi
  if command -v ncu >/dev/null; then
    ncu --set full -k regex:"k_" -o "$OUT/ncu_p3" -f \
      build/conduit_run "${C[@]}" --impl cuda --id "$BIG" --policy P3 --runs 1 --warmup 0 --quiet || \
      echo "ncu failed (on WSL2/laptops enable GPU performance counters: see GPU_README.md)"
  fi
fi

python3 report_gpu.py "$OUT"
echo "done: $OUT/summary.md"
