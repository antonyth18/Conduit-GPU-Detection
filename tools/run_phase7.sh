#!/usr/bin/env bash
# Phase 7: one-command OpenMP benchmark run, meant for the benchmark machine.
#
#   tools/run_phase7.sh [--quick]
#
# Records the machine, builds, runs the C++ tests, the bit-identity sweep over
# every super-block, the memory-bandwidth probe, the scaling sweep and the
# Python validator on OpenMP output. Results go to results/phase7/<host>/.
# --quick runs 1 warm-up + 3 measured runs instead of 2 + 7.
#
# Needs: cmake, a C++17 compiler with OpenMP, python3 with numpy, and the corpus
# (python3 ingest.py with the CSVs in data/). Linux or macOS; the corpus loader
# uses POSIX mmap, so native Windows is not supported (WSL2 works).
set -euo pipefail
cd "$(dirname "$0")/.."

RUNS=7; WARMUP=2
[[ "${1:-}" == "--quick" ]] && { RUNS=3; WARMUP=1; }

[[ -f corpus/tx.bin ]] || { echo "corpus/ missing: put the CSVs in data/ and run python3 ingest.py"; exit 1; }

HOST=$(hostname -s 2>/dev/null || hostname)
OUT="results/phase7/$HOST"
mkdir -p "$OUT"

if [[ "$(uname)" == "Linux" ]]; then
  NPROC=$(nproc)
  # Pin threads to cores; documented in machine.txt.
  export OMP_PROC_BIND=close OMP_PLACES=cores
else
  NPROC=$(sysctl -n hw.logicalcpu)
fi
THREADS=1
t=2; while (( t < NPROC )); do THREADS="$THREADS,$t"; t=$((t * 2)); done
THREADS="$THREADS,$NPROC,$((NPROC * 2))"   # last value oversubscribes

{
  echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "host: $HOST"
  echo "uname: $(uname -a)"
  echo "git: $(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
  echo "threads swept: $THREADS"
  echo "OMP_PROC_BIND=${OMP_PROC_BIND:-unset} OMP_PLACES=${OMP_PLACES:-unset}"
  echo "runs: $WARMUP warm-up + $RUNS measured, median reported"
  echo
  if [[ "$(uname)" == "Linux" ]]; then
    cat /etc/os-release 2>/dev/null | head -4
    echo; lscpu
    echo; free -h
    echo; command -v nvidia-smi >/dev/null && nvidia-smi --query-gpu=name,memory.total,driver_version --format=csv
  else
    sw_vers
    sysctl -n machdep.cpu.brand_string hw.physicalcpu hw.logicalcpu hw.perflevel0.physicalcpu \
      hw.perflevel1.physicalcpu hw.memsize 2>/dev/null
    echo "macOS: no thread pinning available (OMP_PROC_BIND has no effect on core placement)"
  fi
  echo; c++ --version | head -1; cmake --version | head -1
} > "$OUT/machine.txt"
echo "machine info -> $OUT/machine.txt"

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release > "$OUT/build.log"
cmake --build build -j "$NPROC" >> "$OUT/build.log"
grep -q "OpenMP" "$OUT/build.log" || echo "warning: check build.log for OpenMP status"

echo "== unit tests";        build/conduit_tests | tee "$OUT/tests.txt" | tail -1
echo "== bit-identity";      build/conduit_bench identical --threads "1,2,4,$NPROC" | tee "$OUT/identical.txt" | tail -2
echo "== memory bandwidth";  build/membw --threads "$THREADS" | tee "$OUT/membw.txt"
echo "== scaling sweep (long)"
build/conduit_bench scale --csv "$OUT/scale.csv" --threads "$THREADS" --bf \
  --runs "$RUNS" --warmup "$WARMUP" | tee "$OUT/scale.log" | tail -3
echo "== validator on OpenMP output"
python3 tools/verify_cpp.py --impl omp --threads "$NPROC" --level kahn | tee "$OUT/validator.txt" | tail -1
echo "done: results in $OUT/  (analyse with: python3 report_openmp.py $OUT)"
