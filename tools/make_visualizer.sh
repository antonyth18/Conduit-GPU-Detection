#!/usr/bin/env bash
# Regenerate visualizer/trace_data.js from the real pipeline code.
#   bash tools/make_visualizer.sh            # example block + two real windows (needs corpus/ or corpus_eth/)
#   bash tools/make_visualizer.sh --example  # only the hand-made 12-transaction block
# Then open visualizer/index.html in a browser.
set -euo pipefail
cd "$(dirname "$0")/.."
[[ -x build/conduit_trace ]] || { cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null; cmake --build build -j --target conduit_trace; }
build/conduit_trace --example
if [[ "${1:-}" != "--example" ]]; then
  CORPUS=${CONDUIT_CORPUS:-}
  [[ -z "$CORPUS" && -f corpus/superblocks.json ]] && CORPUS=corpus
  [[ -z "$CORPUS" && -f corpus_eth/superblocks.json ]] && CORPUS=corpus_eth
  if [[ -n "$CORPUS" ]]; then
    export CONDUIT_CORPUS=$CORPUS
    build/conduit_trace --id 2026-09-01/10k/000 --auto --limit 14 --append
    build/conduit_trace --id 2026-09-13/10k/000 --auto --limit 16 --append
  else
    echo "no corpus found: only the example block was recorded (run python3 ingest.py first)"
  fi
fi
echo "open visualizer/index.html"
