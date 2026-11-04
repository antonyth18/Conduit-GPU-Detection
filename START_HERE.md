# Conduit: final project folder

Everything for the IT 301 project is in this folder.

| Folder or file | What it is |
|---|---|
| `README.md` | Project overview, results, setup and how to run everything |
| `visualizer/index.html` | Live walk-through of the pipeline: double-click to open |
| `results/` | All measured results, one folder per machine and run |
| `findings.md` | The research log, with every result and the corrections log |
| `REFERENCES.md` | Every paper and source used, and where it is used in the code |
| `REVIEW_RESPONSE.md` | How each item of Antony's review was addressed |
| `GPU_README.md` | CUDA design and the GPU tools in detail |
| `data/` | The four input CSVs: transactions and calldata for 2026-09-01 and 2026-09-13 |
| `data_original_export/` | The first BigQuery export of 2026-09-01 (same day as `tx_20260901.csv`, kept for the record; not read by the scripts) |
| `presentation/` | Slide decks and the scripts that generated them |
| `src/`, `include/`, `tools/`, `tests/`, `*.py` | Source code |

## Bring in your latest results from Ubuntu

Your final runs live inside Ubuntu (`~/conduit/results`). Copy them here once, in Ubuntu:

```bash
cp -r ~/conduit/results/. "/mnt/d/projects/parallel computing/Conduit-Final-Project/results/"
```

## Run it again from this folder

In Ubuntu:

```bash
rm -rf ~/conduit && cp -r "/mnt/d/projects/parallel computing/Conduit-Final-Project" ~/conduit
cd ~/conduit && chmod +x tools/*.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc)
python3 ingest.py && python3 verify_corpus.py && python3 report_oracle.py
bash tools/run_phase8.sh
bash tools/run_dataset.sh data
```

Build and run inside Ubuntu's own disk (`~/conduit`), not on `/mnt/d`. Windows-drive access from WSL is much slower and would distort the timings.
