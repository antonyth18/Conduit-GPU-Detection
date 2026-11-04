#!/usr/bin/env bash
# Phases 8-10 on the standard 10k-100k super-blocks (the Phase 6 manifest).
#
#   bash tools/run_phase8.sh [--synthetic] [--quick] [--profile]
#
#   default      real corpus in corpus/ (python3 ingest.py), results in
#                results/phase8/<host>-real/
#   --synthetic  synthetic workloads (tools/synth_corpus.py), results in
#                results/phase8/<host>-synthetic/
#   --quick      1 warm-up + 3 measured runs (smoke test, not for the report)
#   --profile    Nsight profiler pass on the largest super-block (tools/profile.sh)
#
# Standard: 2 warm-up + 7 measured runs, median reported. Nothing is timed
# unless every correctness gate passed; the script STOPS at the first failure
# (bit-identity sweep, Python validator, GPU stages, GPU verify).
set -euo pipefail
cd "$(dirname "$0")/.."
source tools/lib_machine.sh

RUNS=7; WARMUP=2; SYNTH=0; PROFILE=0; QUICK=0
for a in "$@"; do
  case "$a" in
    --quick) RUNS=3; WARMUP=1; QUICK=1 ;;
    --synthetic) SYNTH=1 ;;
    --profile) PROFILE=1 ;;
    *) echo "unknown option $a"; exit 2 ;;
  esac
done
if (( ! SYNTH )) && [[ ! -f corpus/tx.bin ]]; then
  if ls data/*.csv >/dev/null 2>&1; then
    echo "== building corpus/ from data/ (python3 ingest.py)"
    python3 ingest.py
  else
    echo "no corpus/ and no data/*.csv: using the synthetic workloads"; SYNTH=1
  fi
fi

if (( SYNTH )); then
  CORPUS=corpus_synth; TAG=synthetic
  [[ -f corpus_synth/superblocks.json ]] || python3 tools/synth_corpus.py --out corpus_synth
  MANIFEST=corpus_synth/superblocks.json
else
  CORPUS=corpus; TAG=real
  MANIFEST=corpus/superblocks.json; [[ -f $MANIFEST ]] || MANIFEST=superblocks.json
fi
export CONDUIT_CORPUS=$CORPUS CONDUIT_MANIFEST=$MANIFEST
C=(--corpus "$CORPUS" --manifest "$MANIFEST")

HOST=$(host_tag)
OUT="results/phase8/$HOST-$TAG"; (( QUICK )) && OUT="$OUT-quick"
mkdir -p "$OUT"
NPROC=$(nproc)
export OMP_PROC_BIND=close OMP_PLACES=cores
THREADS=$(thread_list)

echo "== build"
NVCC=$(command -v nvcc || ls /usr/local/cuda*/bin/nvcc 2>/dev/null | head -1 || true)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release ${NVCC:+-DCMAKE_CUDA_COMPILER=$NVCC} ${CUDA_ARCH:+-DCMAKE_CUDA_ARCHITECTURES=$CUDA_ARCH} > "$OUT/build.log"
cmake --build build -j "$NPROC" >> "$OUT/build.log" 2>&1 || { tail -30 "$OUT/build.log"; exit 1; }
grep -E "conduit: (CUDA|OpenMP|no CUDA)" "$OUT/build.log" || true
build/conduit_gpu info | tee "$OUT/gpu_info.txt"
BACKEND=cuda; IMPL=cuda
grep -q "backend cuda" "$OUT/gpu_info.txt" || { BACKEND=emu; IMPL=gpu-emu; echo "WARNING: no CUDA device/build: GPU numbers will be EMULATION"; }
machine_info "$OUT" "run_phase8.sh corpus=$CORPUS runs=$WARMUP+$RUNS threads=$THREADS backend=$BACKEND"

echo "== unit tests"
build/conduit_tests > "$OUT/tests.txt" 2>&1 || true
tail -1 "$OUT/tests.txt"
need "$OUT/tests.txt" " 0 failed checks" "unit tests failed"

echo "== GPU stages one by one against the CPU stages"
build/conduit_gpu stages "${C[@]}" --backend "$BACKEND" > "$OUT/stages.txt"
tail -1 "$OUT/stages.txt"
need "$OUT/stages.txt" "ALL STAGES IDENTICAL" "a GPU stage differs from the CPU stage"

echo "== GPU correctness (bit-identity vs seq-std, JP batch checks, brute oracle)"
build/conduit_gpu verify "${C[@]}" --backend "$BACKEND" > "$OUT/verify.txt"
tail -1 "$OUT/verify.txt"
need "$OUT/verify.txt" "ALL GPU CHECKS PASSED" "GPU verification failed"

if (( ! SYNTH )); then
  echo "== bit-identity sweep over every super-block (seq/omp/gpu)"
  build/conduit_bench identical "${C[@]}" --threads "1,4,$NPROC" > "$OUT/identical.txt" 2>&1
  tail -1 "$OUT/identical.txt"
  need "$OUT/identical.txt" "ALL IDENTICAL" "bit-identity sweep found a mismatch"
  echo "== Python oracle + validator on the GPU output"
  [[ -d oracle_ref ]] || python3 report_oracle.py > "$OUT/oracle.txt"
  python3 tools/verify_cpp.py --impl "$IMPL" > "$OUT/validator.txt" || true
  tail -1 "$OUT/validator.txt"
  need "$OUT/validator.txt" "ALL CHECKS PASSED" "the Python validator rejected the output"
fi

if [[ $BACKEND == cuda ]]; then
  echo "== PCIe / launch microbenchmark"
  build/conduit_gpu pcie --csv "$OUT/pcie.csv" > "$OUT/pcie.txt"; tail -1 "$OUT/pcie.txt"
fi

echo "== factorial timing sweep (long)"
build/conduit_gpu bench "${C[@]}" --backend "$BACKEND" --csv "$OUT/bench.csv" --threads "$THREADS" \
  --configs full --runs "$RUNS" --warmup "$WARMUP" > "$OUT/bench.log"
tail -1 "$OUT/bench.log"

echo "== Phase 9 skew"
build/conduit_gpu skew "${C[@]}" --backend "$BACKEND" --csv "$OUT/skew.csv" --runs "$RUNS" --warmup "$WARMUP" > "$OUT/skew.txt"

echo "== schedule quality"
build/conduit_gpu schedule "${C[@]}" --backend "$BACKEND" --csv "$OUT/schedule.csv" > "$OUT/schedule.txt"

if (( PROFILE )) && [[ $BACKEND == cuda ]]; then
  echo "== Phase 10 profiler pass"
  bash tools/profile.sh --corpus "$CORPUS" --out "$OUT/profile" || echo "profiler pass failed; continuing"
fi

python3 report_gpu.py "$OUT"
echo "done: $OUT/summary.md"
