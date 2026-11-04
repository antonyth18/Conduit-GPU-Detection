# References

Every algorithm, rule and measurement convention in this repository comes from
published work. This file lists each source and **what it supports in the code**.
Where we combine or adapt standard techniques we say so; nothing here is claimed
as new.

Bibliographic details were checked against the publisher, DBLP or the authors'
pages on 2026-11-04.

## Problem setting: parallel execution of blockchain transactions

| # | Reference | Used for |
|---|---|---|
| 1 | T. Dickerson, P. Gazzillo, M. Herlihy, E. Koskinen. **Adding concurrency to smart contracts.** PODC 2017, pp. 303–312. doi:10.1145/3087801.3087835. Extended version in *Distributed Computing* 33(3–4):209–225, 2020. | The problem: blocks are executed serially although many transactions do not conflict; a schedule is valid only if it is equivalent to the block's serial order. They express the miner's schedule as a fork-join program that validators re-execute; our dependency DAG plays the same role. |
| 2 | R. Gelashvili, A. Spiegelman, Z. Xiang, G. Danezis, Z. Li, D. Malkhi, Y. Xia, R. Zhou. **Block-STM: scaling blockchain execution by turning ordering curse to a performance blessing.** PPoPP 2023, pp. 232–244. doi:10.1145/3572848.3577524 (arXiv:2203.06871). | Positioning: the state of the art executes speculatively and needs no precomputed dependency graph. It cites the precomputed-DAG approach as prior work. We **characterise** that approach; we do not claim to beat Block-STM (`handover.md`). |
| 3 | G. Wood. **Ethereum: a secure decentralised generalised transaction ledger** (the Yellow Paper). | Account nonces increase by one per transaction, and the state is per account/storage slot. These give the policies' NONCE and balance keys and why same-sender transactions always conflict. |
| 4 | Google Cloud public dataset **`bigquery-public-data.crypto_ethereum`** (tables `transactions`, receipts). | Input data: Ethereum mainnet, 2026-09-01 and 2026-09-13 (3,489,388 transactions). `ingest.py`. |

## Conflicts, serializability and commutativity (stages 1–2)

| # | Reference | Used for |
|---|---|---|
| 5 | P. A. Bernstein, V. Hadzilacos, N. Goodman. **Concurrency Control and Recovery in Database Systems.** Addison-Wesley, 1987. | The conflict rule: two accesses to one item conflict unless both are reads. A schedule that orders every conflicting pair as the serial order does is conflict-equivalent to it (`pipeline.hpp`, `schedule.cpp: check_order`). |
| 6 | W. E. Weihl. **Commutativity-based concurrency control for abstract data types.** IEEE Trans. Computers 37(12):1488–1505, 1988. doi:10.1109/12.9728. | Policy P3: operations that commute (balance additions) need not be ordered. That is why two COMMUTATIVE accesses to one key do not conflict. |
| 7 | G. Pîrlea, A. Kumar, I. Sergey. **Practical smart contract sharding with ownership and commutativity analysis.** PLDI 2021. doi:10.1145/3453483.3454112. | Evidence that commutativity of token-balance updates is exploitable in real smart contracts (the P3 policy's premise), and that contract-level ownership is the conservative fallback (P1). |

## GPU primitives (stages 1–3)

| # | Reference | Used for |
|---|---|---|
| 8 | G. E. Blelloch. **Prefix sums and their applications.** Tech. Rep. CMU-CS-90-190, Carnegie Mellon University, 1990. | Every "count, scan, write at offset" step: extraction offsets, group/run numbering, edge offsets (`kernels.hpp`; CUB `DeviceScan`). Gives deterministic output without locks. |
| 9 | D. Merrill, A. Grimshaw. **High performance and scalable radix sorting: a case study of implementing dynamic parallelism for GPU computing.** Parallel Processing Letters 21(2):245–272, 2011. | The GPU radix sort of records and edges (CUB `DeviceRadixSort`, by the same author), and the sequential radix sort baseline (seq-radix) that keeps the comparison fair. |
| 10 | D. Merrill, M. Garland. **Merge-based parallel sparse matrix-vector multiplication.** SC16, pp. 678–689. doi:10.1109/SC.2016.57. | Phase 9 "after" mapping: work is split by **output item** using a binary search over the scanned row offsets (the merge-path / load-balanced decomposition), so cost per thread does not depend on how skewed the rows are (`edge_emit`). |
| 11 | D. Merrill, M. Garland, A. Grimshaw. **Scalable GPU graph traversal.** PPoPP 2012. | The problem Phase 9 measures: one thread per irregular unit (vertex, key group) causes load imbalance on GPUs, and prefix sums reorganise uneven work into uniform work. |
| 12 | Y. Saad. **Iterative Methods for Sparse Linear Systems**, 2nd ed. SIAM, 2003. | The compressed sparse row (CSR) layout of the dependency graph (stage 3). |

## Scheduling (stage 4) and execution

| # | Reference | Used for |
|---|---|---|
| 13 | A. B. Kahn. **Topological sorting of large networks.** Commun. ACM 5(11):558–562, 1962. | Frontier levelling (`level_kahn`, GPU `kahn_expand`): a vertex becomes ready when its in-degree reaches zero. |
| 14 | M. T. Jones, P. E. Plassmann. **A parallel graph coloring heuristic.** SIAM J. Sci. Comput. 14(3):654–669, 1993. doi:10.1137/0914041. | The Jones–Plassmann rounds (`jp_phase_a/b/c`): a vertex is coloured when its priority beats all uncoloured neighbours. Ours runs on the implicit conflict graph given by key groups, and colours with the round number (the independent-set form). |
| 15 | M. Luby. **A simple parallel algorithm for the maximal independent set problem.** SIAM J. Comput. 15(4):1036–1053, 1986. | Random priorities giving independent sets in parallel rounds; the basis Jones–Plassmann builds on (`jp-random`). |
| 16 | W. Hasenplaugh, T. Kaler, T. B. Schardl, C. E. Leiserson. **Ordering heuristics for parallel graph coloring.** SPAA 2014, pp. 166–177. | Jones–Plassmann's round count equals the longest path of the priority-ordered DAG, so priorities matter. With priority = block order the rounds are exactly our levels, and a test checks this. |
| 17 | L. G. Valiant. **A bridging model for parallel computation.** Commun. ACM 33(8):103–111, 1990. | Bulk-synchronous cost: a barrier per superstep (per level) is why level-synchronous execution is limited on deep, thin graphs (Phases 7, 8; executor `levels`). |
| 18 | R. L. Graham. **Bounds on multiprocessing timing anomalies.** SIAM J. Appl. Math. 17(2):416–429, 1969. doi:10.1137/0117039. | List scheduling and longest-processing-time-first inside a batch (`simulate`); list scheduling bounds for the dataflow model (`simulate_dag`). |
| 19 | Y.-K. Kwok, I. Ahmad. **Static scheduling algorithms for allocating directed task graphs to multiprocessors.** ACM Comput. Surv. 31(4):406–471, 1999. doi:10.1145/344588.344618. | DAG list scheduling (start a task when its predecessors finish), the critical path as the lower bound for unlimited workers, and t-level ("level") terminology (`exec_dag`, `simulate_dag`). |
| 20 | K. Gupta, J. A. Stuart, J. D. Owens. **A study of persistent threads style GPU programming for GPGPU workloads.** InPar 2012. doi:10.1109/InPar.2012.6339596. | The persistent single-block kernel that runs the remaining rounds of Kahn / Jones–Plassmann without relaunching (`k_kahn_tail`, `k_jp_tail`). It is a known style with known trade-offs, not our invention. |

## Measurement methodology

| # | Reference | Used for |
|---|---|---|
| 21 | C. Gregg, K. Hazelwood. **Where is the data? Why you cannot debate CPU vs. GPU performance without the answer.** ISPASS 2011, pp. 134–144. doi:10.1109/ISPASS.2011.5762730. | Charging PCIe transfers to the GPU, and reporting the transfer arithmetic and the crossover size (Phase 8, `conduit_gpu pcie`, `report_gpu.py`). |
| 22 | NVIDIA. **Nsight Compute Kernel Profiling Guide** (metrics `sm__warps_active`, `gpu__compute_memory_throughput`, `dram__throughput`, `smsp__thread_inst_executed_per_inst_executed`, `smsp__sass_average_branch_targets_threads_uniform`). | The Phase 10 profiler pass: occupancy, memory throughput, warp divergence (`tools/profile.sh`, `report_profile.py`). |
| 23 | NVIDIA. **CUB / CCCL library documentation** (`DeviceRadixSort`, `DeviceScan`, `DeviceSelect::Unique`). | The device-wide primitives used by the CUDA backend. |
| 24 | J. Gray, P. Sundaresan, S. Englert, K. Baclawski, P. J. Weinberger. **Quickly generating billion-record synthetic databases.** SIGMOD 1994, pp. 243–252. | Zipf-distributed access skew in the synthetic workloads (`tools/synth_corpus.py`). |
| 25 | W. D. Hillis and G. L. Steele Jr. **Data parallel algorithms.** Communications of the ACM 29(12):1170–1183, 1986. | The step-by-step parallel prefix sum animated in the visualizer (stage 1). |
| 26 | D. Merrill and M. Garland. **Single-pass parallel prefix scan with decoupled look-back.** NVIDIA Technical Report NVR-2016-002, 2016. | The scan algorithm inside CUB's `DeviceScan`, which the CUDA backend calls. |

## Where each paper is used in the code

| Code | Sources |
|---|---|
| `src/policy.cpp`, `kernels.hpp: k_tx_access` (P1/P2/P3) | 3, 5, 6, 7 |
| `src/detail.hpp: walk_group` (reduced edge rule), `pipeline.hpp` proof | 5 |
| `kernels.hpp: extract_*`, `head_flags`, `scatter_starts` | 8, 26 |
| `cuda_pipeline.cu` sorts, scans, unique | 9, 23 |
| `kernels.hpp: group_*` / `record_*` / `edge_emit` (Phase 9) | 10, 11 |
| `build_csr*`, `csr_rows`, `csr_cols` | 12 |
| `level`, `level_kahn`, `kahn_*`, `k_kahn_tail` | 13, 17, 20 |
| `jp_*`, `k_jp_tail` | 14, 15, 16, 20 |
| `src/execute.cpp`, `src/schedule.cpp` | 1, 5, 17, 18, 19 |
| `conduit_gpu pcie`, crossover analysis | 21 |
| `tools/profile.sh`, `report_profile.py` | 22 |
| `ingest.py` | 4 |
| `tools/synth_corpus.py` | 24 |
| `visualizer/` (live scan, radix passes, DAG build, colouring, execution) | 8, 9, 13, 14, 18, 25 |
