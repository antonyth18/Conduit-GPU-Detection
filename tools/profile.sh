#!/usr/bin/env bash
# Phase 10 profiler pass: Nsight Systems timeline + Nsight Compute metrics
# (occupancy, memory throughput, warp divergence) on the largest super-block.
#
#   bash tools/profile.sh [--corpus DIR] [--id SUPERBLOCK] [--policies P1,P2,P3] [--out DIR]
#                         [--full-report]
#
# Default super-block: the largest one in the manifest (the full day when the
# corpus was built with `ingest.py --tx`, else the largest 100k one).
# Output: <out>/ncu_<policy>.csv, nsys_<policy>_*.csv, profile_summary.md
#
# Needs ncu / nsys (CUDA toolkit: `sudo apt install cuda-nsight-compute-12-6
# cuda-nsight-systems-12-6`, or the standalone installers). On Linux, reading
# GPU performance counters may need root: if ncu reports ERR_NVGPUCTRPERM,
# re-run with  sudo -E env "PATH=$PATH" bash tools/profile.sh ...
# (on WSL2 enable "Allow access to the GPU performance counters to all users"
# in the NVIDIA Control Panel on Windows instead).
set -euo pipefail
cd "$(dirname "$0")/.."

CORPUS=${CONDUIT_CORPUS:-corpus_eth}; ID=""; POLS="P1,P2,P3"; OUT=""; FULL=0
while (( $# )); do
  case "$1" in
    --corpus) CORPUS=$2; shift ;;
    --id) ID=$2; shift ;;
    --policies) POLS=$2; shift ;;
    --out) OUT=$2; shift ;;
    --full-report) FULL=1 ;;
    *) echo "unknown option $1"; exit 2 ;;
  esac
  shift
done
[[ -d $CORPUS ]] || CORPUS=corpus
MAN="$CORPUS/superblocks.json"; [[ -f $MAN ]] || MAN=superblocks.json
HOST=$(hostname -s 2>/dev/null || hostname); HOST=${HOST// /-}
OUT=${OUT:-results/phase10/$HOST-profile}
mkdir -p "$OUT"
[[ -x build/conduit_run ]] || { echo "build first: cmake --build build"; exit 1; }
build/conduit_gpu info | grep -q "backend cuda" || { echo "no CUDA device/build: nothing to profile"; exit 1; }

if [[ -z $ID ]]; then
  ID=$(python3 - "$MAN" <<'EOF'
import json, sys
m = json.load(open(sys.argv[1]))
print(max(m["superblocks"], key=lambda s: s["tx_count"])["id"])
EOF
)
fi
echo "profiling $ID ($CORPUS), policies $POLS -> $OUT"
RUN=(build/conduit_run --corpus "$CORPUS" --manifest "$MAN" --impl cuda --id "$ID" --quiet)

# metrics: time, achieved/theoretical occupancy, SM and memory throughput,
# DRAM bandwidth, warp execution efficiency (threads per warp instruction),
# branch uniformity, launch shape (Nsight Compute Kernel Profiling Guide)
METRICS=gpu__time_duration.sum,\
sm__warps_active.avg.pct_of_peak_sustained_active,\
sm__maximum_warps_per_active_cycle_pct,\
sm__throughput.avg.pct_of_peak_sustained_elapsed,\
gpu__compute_memory_throughput.avg.pct_of_peak_sustained_elapsed,\
dram__throughput.avg.pct_of_peak_sustained_elapsed,\
dram__bytes.sum.per_second,\
smsp__thread_inst_executed_per_inst_executed.ratio,\
smsp__sass_average_branch_targets_threads_uniform.pct,\
launch__grid_size,launch__block_size,launch__registers_per_thread

{
  echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)  host: $HOST  super-block: $ID"
  nvidia-smi --query-gpu=name,memory.total,driver_version,clocks.max.sm --format=csv 2>/dev/null || true
  command -v ncu >/dev/null && ncu --version | tail -1
  command -v nsys >/dev/null && nsys --version
} > "$OUT/profile_machine.txt"

for P in ${POLS//,/ }; do
  if command -v nsys >/dev/null; then
    echo "== nsys $P"
    nsys profile --force-overwrite true -o "$OUT/nsys_$P" "${RUN[@]}" --policy "$P" --runs 3 --warmup 1 >/dev/null
    for rep in cuda_gpu_kern_sum cuda_gpu_mem_time_sum cuda_api_sum; do
      nsys stats --force-export=true --report "$rep" --format csv --output "$OUT/nsys_${P}" \
        "$OUT/nsys_$P.nsys-rep" >/dev/null 2>&1 || true
    done
  else
    echo "nsys not found: skipping the timeline"
  fi
  if command -v ncu >/dev/null; then
    echo "== ncu $P"
    # warm-up 0, one measured run: every kernel of one pipeline pass, once
    if ! ncu --metrics "$METRICS" --csv --page raw "${RUN[@]}" --policy "$P" --runs 1 --warmup 0 \
         > "$OUT/ncu_$P.csv" 2> "$OUT/ncu_$P.log"; then
      if grep -q ERR_NVGPUCTRPERM "$OUT/ncu_$P.log" "$OUT/ncu_$P.csv" 2>/dev/null; then
        echo "ncu: no permission for GPU performance counters."
        echo "     re-run as: sudo -E env \"PATH=\$PATH\" bash tools/profile.sh $*"
        exit 1
      fi
      echo "ncu failed: see $OUT/ncu_$P.log"; exit 1
    fi
    if (( FULL )); then
      ncu --set full -f -o "$OUT/ncu_full_$P" "${RUN[@]}" --policy "$P" --runs 1 --warmup 0 >/dev/null
    fi
  else
    echo "ncu not found: skipping kernel metrics"
  fi
done
python3 report_profile.py "$OUT"
echo "done: $OUT/profile_summary.md"
