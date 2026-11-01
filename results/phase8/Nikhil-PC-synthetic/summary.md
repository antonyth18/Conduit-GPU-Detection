# Phase 8-10 results: GPU preprocessing pipeline

## Machine

```
date: 2026-11-01T09:27:29Z
host: Nikhil-PC   corpus: corpus_synth (synthetic)
git: c72b087
runs: 2 warm-up + 7 measured, median reported; omp threads 1,2,4,8,12
OMP_PROC_BIND=close OMP_PLACES=cores

Linux Nikhil-PC 6.18.54.1-microsoft-standard-WSL2 #1 SMP PREEMPT_DYNAMIC Tue Oct  6 19:13:07 UTC 2026 x86_64 x86_64 x86_64 GNU/Linux
WSL2: yes

PRETTY_NAME="Ubuntu 24.04.5 LTS"
NAME="Ubuntu"
VERSION_ID="24.04"
VERSION="24.04.5 LTS (Noble Numbat)"

CPU(s):                                  12
Model name:                              13th Gen Intel(R) Core(TM) i5-13420H
Thread(s) per core:                      2
Core(s) per socket:                      6
Socket(s):                               1
L2 cache:                                7.5 MiB (6 instances)
L3 cache:                                12 MiB (1 instance)

               total        used        free      shared  buff/cache   available
Mem:           7.6Gi       460Mi       6.2Gi       3.6Mi       1.1Gi       7.1Gi
Swap:          2.0Gi          0B       2.0Gi

name, memory.total [MiB], driver_version, pcie.link.gen.max, pcie.link.width.max, clocks.max.sm [MHz]
NVIDIA GeForce RTX 4050 Laptop GPU, 6141 MiB, 610.91, 4, 8, 3105 MHz

Cuda compilation tools, release 12.6, V12.6.85
Build cuda_12.6.r12.6/compiler.35059454_0

c++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
cmake version 3.28.3
```

## End-to-end time per super-block (ms, median)

GPU = balanced detection + CPU sweep, transfers included. `k-only` = GPU stages 1-3 kernels without transfers.

| scenario | n | pol | seq-std | seq-radix | omp best (t) | GPU e2e | GPU k-only | vs seq-std | vs seq-radix | vs omp |
|---|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|
| synth-eth-like | 1000 | P1 | 0.09 | 0.05 | 0.10 (1) | 0.33 | 0.30 | 0.26x | 0.16x | 0.30x |
| synth-eth-like | 1000 | P2 | 0.19 | 0.10 | 0.18 (2) | 0.50 | 0.46 | 0.38x | 0.20x | 0.36x |
| synth-eth-like | 1000 | P3 | 0.19 | 0.11 | 0.18 (2) | 0.49 | 0.46 | 0.38x | 0.21x | 0.36x |
| synth-eth-like | 2000 | P1 | 0.21 | 0.09 | 0.18 (1) | 0.51 | 0.46 | 0.40x | 0.17x | 0.35x |
| synth-eth-like | 2000 | P2 | 0.45 | 0.20 | 0.28 (2) | 0.59 | 0.54 | 0.76x | 0.33x | 0.48x |
| synth-eth-like | 2000 | P3 | 0.44 | 0.20 | 0.26 (4) | 0.89 | 0.81 | 0.49x | 0.22x | 0.30x |
| synth-eth-like | 5000 | P1 | 0.63 | 0.21 | 0.33 (4) | 1.06 | 0.94 | 0.59x | 0.20x | 0.31x |
| synth-eth-like | 5000 | P2 | 1.32 | 0.54 | 0.58 (8) | 0.59 | 0.51 | 2.23x | 0.91x | 0.98x |
| synth-eth-like | 5000 | P3 | 1.28 | 0.58 | 0.53 (4) | 0.59 | 0.52 | 2.15x | 0.97x | 0.89x |
| synth-eth-like | 10001 | P1 | 1.40 | 0.43 | 0.61 (8) | 0.60 | 0.51 | 2.33x | 0.72x | 1.01x |
| synth-eth-like | 10001 | P2 | 2.89 | 1.13 | 0.93 (8) | 0.90 | 0.74 | 3.21x | 1.25x | 1.03x |
| synth-eth-like | 10001 | P3 | 2.74 | 1.07 | 0.82 (4) | 0.68 | 0.56 | 4.05x | 1.58x | 1.21x |
| synth-eth-like | 20031 | P1 | 3.13 | 0.94 | 0.91 (4) | 0.83 | 0.64 | 3.77x | 1.14x | 1.10x |
| synth-eth-like | 20031 | P2 | 6.34 | 2.37 | 1.54 (8) | 0.81 | 0.57 | 7.84x | 2.93x | 1.91x |
| synth-eth-like | 20031 | P3 | 5.88 | 2.40 | 1.43 (8) | 1.24 | 0.96 | 4.73x | 1.93x | 1.15x |
| synth-eth-like | 40060 | P1 | 6.60 | 2.05 | 1.86 (8) | 0.92 | 0.56 | 7.17x | 2.23x | 2.02x |
| synth-eth-like | 40060 | P2 | 14.81 | 6.85 | 4.13 (8) | 1.16 | 0.68 | 12.78x | 5.91x | 3.57x |
| synth-eth-like | 40060 | P3 | 12.88 | 5.01 | 2.68 (8) | 1.24 | 0.75 | 10.39x | 4.04x | 2.16x |
| synth-eth-like | 60070 | P1 | 10.75 | 3.35 | 2.83 (8) | 1.23 | 0.66 | 8.76x | 2.73x | 2.31x |
| synth-eth-like | 60070 | P2 | 23.08 | 10.38 | 6.48 (8) | 1.56 | 0.80 | 14.77x | 6.64x | 4.15x |
| synth-eth-like | 60070 | P3 | 20.07 | 8.17 | 4.13 (8) | 1.60 | 0.86 | 12.56x | 5.11x | 2.59x |
| synth-eth-like | 80001 | P1 | 15.74 | 4.91 | 3.19 (8) | 1.41 | 0.68 | 11.19x | 3.49x | 2.26x |
| synth-eth-like | 80001 | P2 | 34.09 | 17.01 | 9.06 (8) | 2.72 | 1.60 | 12.55x | 6.26x | 3.33x |
| synth-eth-like | 80001 | P3 | 31.10 | 14.51 | 5.75 (8) | 2.45 | 1.47 | 12.67x | 5.91x | 2.34x |
| synth-eth-like | 100119 | P1 | 18.83 | 6.32 | 4.39 (8) | 2.62 | 1.69 | 7.17x | 2.41x | 1.67x |
| synth-eth-like | 100119 | P2 | 41.68 | 18.69 | 12.65 (8) | 2.24 | 0.96 | 18.60x | 8.34x | 5.65x |
| synth-eth-like | 100119 | P3 | 36.71 | 14.80 | 9.25 (8) | 2.21 | 0.99 | 16.58x | 6.68x | 4.18x |
| synth-uniform-low | 1000 | P1 | 0.06 | 0.05 | 0.08 (1) | 0.38 | 0.34 | 0.15x | 0.15x | 0.22x |
| synth-uniform-low | 1000 | P2 | 0.18 | 0.11 | 0.16 (2) | 0.48 | 0.45 | 0.38x | 0.23x | 0.33x |
| synth-uniform-low | 1000 | P3 | 0.18 | 0.12 | 0.16 (1) | 0.52 | 0.48 | 0.36x | 0.24x | 0.31x |
| synth-uniform-low | 2000 | P1 | 0.15 | 0.08 | 0.11 (1) | 0.56 | 0.48 | 0.26x | 0.14x | 0.20x |
| synth-uniform-low | 2000 | P2 | 0.41 | 0.20 | 0.23 (4) | 0.52 | 0.48 | 0.79x | 0.40x | 0.45x |
| synth-uniform-low | 2000 | P3 | 0.41 | 0.20 | 0.24 (4) | 1.48 | 1.32 | 0.27x | 0.14x | 0.16x |
| synth-uniform-low | 5000 | P1 | 0.43 | 0.18 | 0.25 (2) | 0.51 | 0.45 | 0.86x | 0.35x | 0.50x |
| synth-uniform-low | 5000 | P2 | 1.12 | 0.54 | 0.46 (4) | 0.54 | 0.50 | 2.06x | 0.98x | 0.84x |
| synth-uniform-low | 5000 | P3 | 1.12 | 0.51 | 0.41 (4) | 0.54 | 0.49 | 2.07x | 0.95x | 0.76x |
| synth-uniform-low | 10009 | P1 | 0.97 | 0.39 | 0.38 (4) | 0.53 | 0.46 | 1.81x | 0.74x | 0.71x |
| synth-uniform-low | 10009 | P2 | 2.44 | 1.01 | 0.68 (4) | 0.59 | 0.52 | 4.12x | 1.71x | 1.14x |
| synth-uniform-low | 10009 | P3 | 2.45 | 1.04 | 0.76 (8) | 0.63 | 0.55 | 3.91x | 1.66x | 1.22x |
| synth-uniform-low | 20088 | P1 | 2.19 | 0.78 | 0.71 (8) | 0.77 | 0.65 | 2.82x | 1.01x | 0.92x |
| synth-uniform-low | 20088 | P2 | 5.24 | 2.11 | 1.40 (8) | 0.79 | 0.68 | 6.60x | 2.66x | 1.76x |
| synth-uniform-low | 20088 | P3 | 5.19 | 2.18 | 1.24 (8) | 1.54 | 1.35 | 3.36x | 1.42x | 0.80x |
| synth-uniform-low | 40062 | P1 | 4.79 | 1.65 | 1.29 (8) | 2.53 | 1.77 | 1.90x | 0.65x | 0.51x |
| synth-uniform-low | 40062 | P2 | 10.97 | 4.65 | 2.34 (8) | 1.40 | 1.14 | 7.86x | 3.33x | 1.68x |
| synth-uniform-low | 40062 | P3 | 10.85 | 4.50 | 2.31 (8) | 1.09 | 0.87 | 9.93x | 4.12x | 2.11x |
| synth-uniform-low | 60099 | P1 | 7.98 | 2.70 | 2.11 (8) | 1.21 | 0.81 | 6.57x | 2.23x | 1.74x |
| synth-uniform-low | 60099 | P2 | 17.33 | 7.70 | 4.62 (8) | 1.56 | 1.23 | 11.09x | 4.93x | 2.96x |
| synth-uniform-low | 60099 | P3 | 17.56 | 7.41 | 4.23 (8) | 1.56 | 1.21 | 11.28x | 4.76x | 2.72x |
| synth-uniform-low | 80268 | P1 | 11.64 | 3.81 | 2.77 (8) | 1.88 | 1.14 | 6.18x | 2.02x | 1.47x |
| synth-uniform-low | 80268 | P2 | 24.04 | 10.48 | 7.41 (8) | 1.98 | 1.51 | 12.13x | 5.29x | 3.74x |
| synth-uniform-low | 80268 | P3 | 24.04 | 10.85 | 6.76 (8) | 1.71 | 1.22 | 14.08x | 6.35x | 3.96x |
| synth-uniform-low | 100098 | P1 | 14.94 | 4.94 | 4.39 (8) | 1.67 | 0.91 | 8.94x | 2.95x | 2.63x |
| synth-uniform-low | 100098 | P2 | 34.36 | 19.42 | 15.13 (8) | 2.61 | 1.95 | 13.15x | 7.43x | 5.79x |
| synth-uniform-low | 100098 | P3 | 31.58 | 16.45 | 16.28 (8) | 4.02 | 3.06 | 7.85x | 4.09x | 4.05x |
| synth-uniform-high | 1000 | P1 | 0.12 | 0.05 | 0.11 (1) | 0.52 | 0.48 | 0.23x | 0.10x | 0.21x |
| synth-uniform-high | 1000 | P2 | 0.23 | 0.12 | 0.20 (2) | 0.64 | 0.60 | 0.36x | 0.19x | 0.31x |
| synth-uniform-high | 1000 | P3 | 0.23 | 0.11 | 0.19 (2) | 1.56 | 1.39 | 0.15x | 0.07x | 0.12x |
| synth-uniform-high | 2000 | P1 | 0.29 | 0.10 | 0.19 (2) | 0.99 | 0.94 | 0.29x | 0.10x | 0.19x |
| synth-uniform-high | 2000 | P2 | 0.63 | 0.25 | 0.33 (4) | 0.73 | 0.69 | 0.86x | 0.34x | 0.45x |
| synth-uniform-high | 2000 | P3 | 0.55 | 0.23 | 0.30 (4) | 0.87 | 0.81 | 0.63x | 0.26x | 0.34x |
| synth-uniform-high | 5000 | P1 | 0.93 | 0.25 | 0.38 (4) | 0.79 | 0.69 | 1.18x | 0.32x | 0.48x |
| synth-uniform-high | 5000 | P2 | 1.80 | 0.65 | 0.63 (8) | 0.87 | 0.76 | 2.07x | 0.75x | 0.72x |
| synth-uniform-high | 5000 | P3 | 1.63 | 0.60 | 0.60 (4) | 0.88 | 0.76 | 1.86x | 0.69x | 0.69x |
| synth-uniform-high | 10114 | P1 | 2.05 | 0.57 | 0.57 (4) | 1.97 | 1.67 | 1.04x | 0.29x | 0.29x |
| synth-uniform-high | 10114 | P2 | 4.42 | 1.56 | 0.99 (8) | 2.03 | 1.75 | 2.18x | 0.77x | 0.49x |
| synth-uniform-high | 10114 | P3 | 3.74 | 1.32 | 0.94 (4) | 0.85 | 0.69 | 4.41x | 1.55x | 1.12x |
| synth-uniform-high | 20155 | P1 | 4.33 | 1.07 | 0.98 (4) | 0.85 | 0.67 | 5.09x | 1.26x | 1.15x |
| synth-uniform-high | 20155 | P2 | 10.51 | 3.16 | 2.05 (8) | 1.20 | 0.87 | 8.75x | 2.63x | 1.71x |
| synth-uniform-high | 20155 | P3 | 8.33 | 2.72 | 1.69 (8) | 0.89 | 0.63 | 9.32x | 3.04x | 1.89x |
| synth-uniform-high | 40189 | P1 | 8.95 | 2.89 | 1.70 (8) | 1.97 | 1.57 | 4.54x | 1.47x | 0.86x |
| synth-uniform-high | 40189 | P2 | 20.94 | 6.60 | 3.36 (8) | 2.22 | 1.60 | 9.44x | 2.98x | 1.51x |
| synth-uniform-high | 40189 | P3 | 18.82 | 6.76 | 3.45 (8) | 1.26 | 0.72 | 14.96x | 5.37x | 2.74x |
| synth-uniform-high | 60032 | P1 | 14.27 | 4.08 | 2.47 (8) | 1.18 | 0.69 | 12.13x | 3.47x | 2.10x |
| synth-uniform-high | 60032 | P2 | 32.97 | 12.13 | 5.78 (8) | 1.62 | 0.78 | 20.31x | 7.47x | 3.56x |
| synth-uniform-high | 60032 | P3 | 29.31 | 13.44 | 6.47 (8) | 1.92 | 0.95 | 15.28x | 7.01x | 3.37x |
| synth-uniform-high | 80061 | P1 | 19.25 | 5.29 | 4.72 (8) | 1.81 | 1.00 | 10.62x | 2.92x | 2.61x |
| synth-uniform-high | 80061 | P2 | 47.00 | 16.13 | 12.48 (8) | 2.90 | 1.64 | 16.22x | 5.57x | 4.31x |
| synth-uniform-high | 80061 | P3 | 40.12 | 17.40 | 8.71 (8) | 2.32 | 0.98 | 17.30x | 7.50x | 3.76x |
| synth-uniform-high | 100081 | P1 | 24.38 | 7.54 | 4.78 (8) | 1.85 | 0.85 | 13.17x | 4.07x | 2.58x |
| synth-uniform-high | 100081 | P2 | 59.35 | 22.91 | 20.14 (8) | 2.79 | 1.35 | 21.28x | 8.21x | 7.22x |
| synth-uniform-high | 100081 | P3 | 51.45 | 20.32 | 17.13 (8) | 2.49 | 1.04 | 20.66x | 8.16x | 6.88x |
| synth-zipf-low | 1000 | P1 | 0.08 | 0.05 | 0.10 (1) | 0.36 | 0.33 | 0.23x | 0.14x | 0.27x |
| synth-zipf-low | 1000 | P2 | 0.22 | 0.11 | 0.19 (2) | 0.50 | 0.47 | 0.43x | 0.23x | 0.38x |
| synth-zipf-low | 1000 | P3 | 0.21 | 0.11 | 0.18 (2) | 0.50 | 0.47 | 0.41x | 0.23x | 0.36x |
| synth-zipf-low | 2000 | P1 | 0.21 | 0.09 | 0.18 (2) | 0.49 | 0.46 | 0.43x | 0.18x | 0.36x |
| synth-zipf-low | 2000 | P2 | 0.48 | 0.21 | 0.27 (4) | 1.51 | 1.36 | 0.32x | 0.14x | 0.18x |
| synth-zipf-low | 2000 | P3 | 0.47 | 0.20 | 0.25 (4) | 0.59 | 0.54 | 0.80x | 0.35x | 0.43x |
| synth-zipf-low | 5000 | P1 | 0.64 | 0.21 | 0.32 (4) | 1.70 | 1.54 | 0.37x | 0.12x | 0.19x |
| synth-zipf-low | 5000 | P2 | 1.38 | 0.56 | 0.48 (4) | 0.63 | 0.56 | 2.18x | 0.88x | 0.76x |
| synth-zipf-low | 5000 | P3 | 1.32 | 0.51 | 0.49 (4) | 0.59 | 0.52 | 2.22x | 0.86x | 0.83x |
| synth-zipf-low | 10100 | P1 | 1.40 | 0.48 | 0.61 (8) | 0.63 | 0.54 | 2.22x | 0.77x | 0.96x |
| synth-zipf-low | 10100 | P2 | 2.92 | 1.14 | 0.83 (8) | 0.66 | 0.56 | 4.46x | 1.74x | 1.26x |
| synth-zipf-low | 10100 | P3 | 2.79 | 1.07 | 0.88 (4) | 0.61 | 0.53 | 4.59x | 1.76x | 1.45x |
| synth-zipf-low | 20003 | P1 | 2.99 | 0.93 | 1.03 (8) | 0.70 | 0.54 | 4.27x | 1.32x | 1.48x |
| synth-zipf-low | 20003 | P2 | 6.32 | 2.27 | 1.41 (8) | 0.96 | 0.79 | 6.56x | 2.36x | 1.47x |
| synth-zipf-low | 20003 | P3 | 5.88 | 2.15 | 1.34 (4) | 1.07 | 0.90 | 5.51x | 2.02x | 1.26x |
| synth-zipf-low | 40197 | P1 | 6.46 | 1.92 | 1.53 (8) | 0.89 | 0.58 | 7.24x | 2.15x | 1.72x |
| synth-zipf-low | 40197 | P2 | 13.25 | 5.16 | 2.67 (8) | 1.33 | 0.98 | 9.99x | 3.89x | 2.02x |
| synth-zipf-low | 40197 | P3 | 13.06 | 4.66 | 2.42 (8) | 1.00 | 0.72 | 13.02x | 4.65x | 2.41x |
| synth-zipf-low | 60055 | P1 | 10.09 | 3.03 | 2.30 (8) | 1.08 | 0.60 | 9.32x | 2.80x | 2.12x |
| synth-zipf-low | 60055 | P2 | 21.17 | 9.08 | 4.50 (8) | 1.65 | 1.08 | 12.84x | 5.51x | 2.73x |
| synth-zipf-low | 60055 | P3 | 20.33 | 8.08 | 4.15 (8) | 1.29 | 0.81 | 15.70x | 6.24x | 3.21x |
| synth-zipf-low | 80103 | P1 | 14.98 | 5.40 | 3.01 (8) | 1.41 | 0.74 | 10.61x | 3.82x | 2.13x |
| synth-zipf-low | 80103 | P2 | 31.96 | 11.61 | 7.28 (8) | 1.58 | 0.90 | 20.29x | 7.37x | 4.62x |
| synth-zipf-low | 80103 | P3 | 27.66 | 10.93 | 6.76 (8) | 1.52 | 0.88 | 18.24x | 7.21x | 4.46x |
| synth-zipf-low | 100103 | P1 | 18.85 | 5.51 | 4.14 (8) | 1.86 | 0.86 | 10.14x | 2.96x | 2.23x |
| synth-zipf-low | 100103 | P2 | 36.76 | 15.32 | 15.69 (8) | 2.48 | 1.57 | 14.80x | 6.17x | 6.32x |
| synth-zipf-low | 100103 | P3 | 35.94 | 15.67 | 16.82 (8) | 1.87 | 1.08 | 19.18x | 8.36x | 8.98x |
| synth-zipf-high | 1000 | P1 | 0.11 | 0.06 | 0.11 (1) | 0.39 | 0.35 | 0.29x | 0.15x | 0.29x |
| synth-zipf-high | 1000 | P2 | 0.26 | 0.12 | 0.19 (2) | 0.83 | 0.77 | 0.32x | 0.14x | 0.23x |
| synth-zipf-high | 1000 | P3 | 0.25 | 0.11 | 0.20 (4) | 0.61 | 0.56 | 0.42x | 0.19x | 0.32x |
| synth-zipf-high | 2000 | P1 | 0.28 | 0.10 | 0.20 (1) | 0.55 | 0.49 | 0.51x | 0.19x | 0.37x |
| synth-zipf-high | 2000 | P2 | 0.60 | 0.24 | 0.35 (4) | 0.70 | 0.66 | 0.86x | 0.34x | 0.50x |
| synth-zipf-high | 2000 | P3 | 0.59 | 0.23 | 0.27 (4) | 0.74 | 0.71 | 0.79x | 0.32x | 0.36x |
| synth-zipf-high | 5000 | P1 | 0.82 | 0.25 | 0.38 (4) | 0.60 | 0.54 | 1.37x | 0.42x | 0.64x |
| synth-zipf-high | 5000 | P2 | 1.78 | 0.64 | 0.53 (4) | 1.53 | 1.33 | 1.16x | 0.42x | 0.35x |
| synth-zipf-high | 5000 | P3 | 1.60 | 0.59 | 0.49 (4) | 0.85 | 0.64 | 1.88x | 0.69x | 0.57x |
| synth-zipf-high | 10115 | P1 | 1.83 | 0.58 | 0.62 (8) | 0.65 | 0.54 | 2.83x | 0.89x | 0.95x |
| synth-zipf-high | 10115 | P2 | 3.92 | 1.35 | 0.92 (8) | 0.74 | 0.61 | 5.29x | 1.82x | 1.23x |
| synth-zipf-high | 10115 | P3 | 3.51 | 1.23 | 0.89 (4) | 0.74 | 0.62 | 4.71x | 1.66x | 1.19x |
| synth-zipf-high | 20256 | P1 | 3.98 | 1.13 | 1.06 (8) | 1.01 | 0.76 | 3.94x | 1.12x | 1.05x |
| synth-zipf-high | 20256 | P2 | 8.95 | 2.91 | 1.93 (8) | 1.42 | 1.06 | 6.31x | 2.05x | 1.36x |
| synth-zipf-high | 20256 | P3 | 7.72 | 2.60 | 1.54 (8) | 0.80 | 0.58 | 9.64x | 3.25x | 1.92x |
| synth-zipf-high | 40061 | P1 | 8.50 | 2.37 | 1.84 (8) | 1.98 | 1.55 | 4.30x | 1.20x | 0.93x |
| synth-zipf-high | 40061 | P2 | 18.78 | 6.20 | 3.51 (8) | 1.20 | 0.72 | 15.69x | 5.18x | 2.94x |
| synth-zipf-high | 40061 | P3 | 16.49 | 5.43 | 2.84 (8) | 1.22 | 0.80 | 13.56x | 4.47x | 2.34x |
| synth-zipf-high | 60106 | P1 | 13.08 | 4.34 | 2.63 (8) | 2.15 | 1.57 | 6.08x | 2.02x | 1.22x |
| synth-zipf-high | 60106 | P2 | 29.68 | 10.09 | 5.47 (8) | 2.54 | 1.76 | 11.68x | 3.97x | 2.15x |
| synth-zipf-high | 60106 | P3 | 26.27 | 9.48 | 4.69 (8) | 1.34 | 0.75 | 19.53x | 7.05x | 3.49x |
| synth-zipf-high | 80168 | P1 | 17.80 | 5.41 | 3.58 (8) | 1.82 | 0.82 | 9.78x | 2.97x | 1.97x |
| synth-zipf-high | 80168 | P2 | 41.51 | 14.24 | 9.30 (8) | 2.61 | 1.25 | 15.89x | 5.45x | 3.56x |
| synth-zipf-high | 80168 | P3 | 36.49 | 12.46 | 6.90 (8) | 1.82 | 0.99 | 20.03x | 6.84x | 3.79x |
| synth-zipf-high | 100081 | P1 | 23.29 | 6.71 | 5.40 (8) | 1.71 | 0.85 | 13.60x | 3.92x | 3.15x |
| synth-zipf-high | 100081 | P2 | 60.64 | 23.87 | 19.93 (8) | 2.44 | 1.18 | 24.86x | 9.79x | 8.17x |
| synth-zipf-high | 100081 | P3 | 47.02 | 18.55 | 16.18 (8) | 2.45 | 1.40 | 19.22x | 7.59x | 6.61x |

### Crossover (smallest tested n where the GPU, transfers charged, wins)

| scenario | pol | beats seq-radix at n >= | beats best omp at n >= |
|---|---|---:|---:|
| synth-eth-like | P1 | 20031 | 10001 |
| synth-eth-like | P2 | 10001 | 10001 |
| synth-eth-like | P3 | 10001 | 10001 |
| synth-uniform-low | P1 | 20088 | 60099 |
| synth-uniform-low | P2 | 10009 | 10009 |
| synth-uniform-low | P3 | 10009 | 10009 |
| synth-uniform-high | P1 | 20155 | 20155 |
| synth-uniform-high | P2 | 20155 | 20155 |
| synth-uniform-high | P3 | 10114 | 10114 |
| synth-zipf-low | P1 | 20003 | 20003 |
| synth-zipf-low | P2 | 10100 | 10100 |
| synth-zipf-low | P3 | 10100 | 10100 |
| synth-zipf-high | P1 | 20256 | 20256 |
| synth-zipf-high | P2 | 10115 | 10115 |
| synth-zipf-high | P3 | 10115 | 10115 |

## GPU stage breakdown at the largest super-block (ms)

| scenario | pol | n | H2D | extract | sort | detect | csr | D2H | level (sweep) | e2e | wall | launches | syncs | device MB |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| synth-eth-like | P1 | 100119 | 0.33 | 0.15 | 0.55 | 0.49 | 0.48 | 0.15 | 0.45 | 2.62 | 3.26 | 17 | 6 | 68.5 |
| synth-eth-like | P2 | 100119 | 0.33 | 0.26 | 0.24 | 0.25 | 0.19 | 0.15 | 0.81 | 2.24 | 2.87 | 17 | 6 | 80.5 |
| synth-eth-like | P3 | 100119 | 0.33 | 0.29 | 0.24 | 0.24 | 0.20 | 0.14 | 0.74 | 2.21 | 2.86 | 17 | 6 | 80.5 |
| synth-uniform-low | P1 | 100098 | 0.33 | 0.18 | 0.22 | 0.23 | 0.23 | 0.10 | 0.32 | 1.67 | 2.27 | 17 | 6 | 88.7 |
| synth-uniform-low | P2 | 100098 | 0.34 | 0.47 | 0.57 | 0.38 | 0.52 | 0.11 | 0.21 | 2.61 | 3.49 | 17 | 6 | 88.7 |
| synth-uniform-low | P3 | 100098 | 0.62 | 0.94 | 1.19 | 0.52 | 0.38 | 0.16 | 0.17 | 4.02 | 4.67 | 17 | 6 | 88.7 |
| synth-uniform-high | P1 | 100081 | 0.35 | 0.16 | 0.16 | 0.25 | 0.20 | 0.17 | 0.34 | 1.85 | 2.62 | 17 | 6 | 93.5 |
| synth-uniform-high | P2 | 100081 | 0.33 | 0.34 | 0.33 | 0.36 | 0.27 | 0.22 | 0.87 | 2.79 | 3.61 | 17 | 6 | 96.5 |
| synth-uniform-high | P3 | 100081 | 0.33 | 0.31 | 0.26 | 0.25 | 0.25 | 0.19 | 0.89 | 2.49 | 3.20 | 17 | 6 | 96.5 |
| synth-zipf-low | P1 | 100103 | 0.35 | 0.20 | 0.20 | 0.25 | 0.30 | 0.15 | 0.45 | 1.86 | 3.07 | 17 | 6 | 96.5 |
| synth-zipf-low | P2 | 100103 | 0.33 | 0.28 | 0.33 | 0.45 | 0.50 | 0.14 | 0.40 | 2.48 | 3.09 | 17 | 6 | 96.5 |
| synth-zipf-low | P3 | 100103 | 0.34 | 0.31 | 0.27 | 0.26 | 0.23 | 0.11 | 0.37 | 1.87 | 2.68 | 17 | 6 | 96.5 |
| synth-zipf-high | P1 | 100081 | 0.33 | 0.17 | 0.17 | 0.23 | 0.28 | 0.16 | 0.37 | 1.71 | 2.51 | 17 | 6 | 96.5 |
| synth-zipf-high | P2 | 100081 | 0.33 | 0.30 | 0.26 | 0.32 | 0.29 | 0.21 | 0.71 | 2.44 | 3.14 | 17 | 6 | 96.5 |
| synth-zipf-high | P3 | 100081 | 0.33 | 0.29 | 0.26 | 0.38 | 0.30 | 0.17 | 0.56 | 2.45 | 3.23 | 17 | 6 | 96.5 |

## Phase 9: skew (detection, one thread per key vs one thread per edge)

| scenario | pol | n | largest key group | detect group (ms) | detect balanced (ms) | recovery |
|---|---|---:|---:|---:|---:|---:|
| synth-eth-like | P1 | 100119 | 14444 | 3.467 | 0.490 | 7.07x |
| synth-eth-like | P2 | 100119 | 14444 | 10.927 | 0.251 | 43.55x |
| synth-eth-like | P3 | 100119 | 14444 | 3.416 | 0.244 | 14.02x |
| synth-uniform-low | P1 | 100098 | 10 | 0.216 | 0.229 | 0.94x |
| synth-uniform-low | P2 | 100098 | 10 | 1.113 | 0.380 | 2.93x |
| synth-uniform-low | P3 | 100098 | 10 | 0.739 | 0.519 | 1.42x |
| synth-uniform-high | P1 | 100081 | 976 | 0.564 | 0.250 | 2.26x |
| synth-uniform-high | P2 | 100081 | 976 | 2.156 | 0.361 | 5.97x |
| synth-uniform-high | P3 | 100081 | 976 | 1.783 | 0.252 | 7.08x |
| synth-zipf-low | P1 | 100103 | 9009 | 2.208 | 0.249 | 8.87x |
| synth-zipf-low | P2 | 100103 | 9009 | 5.744 | 0.449 | 12.81x |
| synth-zipf-low | P3 | 100103 | 9009 | 6.287 | 0.255 | 24.66x |
| synth-zipf-high | P1 | 100081 | 13864 | 3.345 | 0.234 | 14.27x |
| synth-zipf-high | P2 | 100081 | 13864 | 13.423 | 0.325 | 41.35x |
| synth-zipf-high | P3 | 100081 | 13864 | 8.195 | 0.380 | 21.57x |

## Stage 4: levelling / batching on the GPU vs the CPU sweep

`rounds` = batches; `tail` = rounds run inside the persistent single block.

| scenario | pol | n | method | batches | rounds | tail | launches | level ms | e2e ms |
|---|---|---:|---|---:|---:|---:|---:|---:|---:|
| synth-eth-like | P1 | 100119 | sweep | 14444 | 0 | 0 | 17 | 0.452 | 2.62 |
| synth-eth-like | P1 | 100119 | kahn | 14444 | 14444 | 14412 | 84 | 11.461 | 12.89 |
| synth-eth-like | P1 | 100119 | jp-index | 14444 | 14444 | 1388 | 52245 | 903.278 | 905.50 |
| synth-eth-like | P1 | 100119 | jp-random | 14444 | 14444 | 1388 | 52245 | 874.630 | 876.09 |
| synth-eth-like | P2 | 100119 | sweep | 3163 | 0 | 0 | 17 | 0.808 | 2.24 |
| synth-eth-like | P2 | 100119 | kahn | 3163 | 3163 | 3131 | 84 | 9.474 | 11.16 |
| synth-eth-like | P2 | 100119 | jp-index | 3163 | 3163 | 123 | 12181 | 305.169 | 306.71 |
| synth-eth-like | P2 | 100119 | jp-random | 3170 | 3170 | 130 | 12181 | 309.895 | 311.42 |
| synth-eth-like | P3 | 100119 | sweep | 2717 | 0 | 0 | 17 | 0.739 | 2.21 |
| synth-eth-like | P3 | 100119 | kahn | 2717 | 2717 | 2685 | 84 | 8.683 | 10.25 |
| synth-eth-like | P3 | 100119 | jp-index | 2717 | 2717 | 125 | 10389 | 242.524 | 244.14 |
| synth-eth-like | P3 | 100119 | jp-random | 2723 | 2723 | 131 | 10389 | 234.016 | 235.83 |
| synth-uniform-low | P1 | 100098 | sweep | 10 | 0 | 0 | 17 | 0.323 | 1.67 |
| synth-uniform-low | P1 | 100098 | kahn | 10 | 10 | 0 | 83 | 0.588 | 1.91 |
| synth-uniform-low | P1 | 100098 | jp-index | 10 | 10 | 0 | 148 | 1.063 | 2.31 |
| synth-uniform-low | P1 | 100098 | jp-random | 10 | 10 | 0 | 148 | 1.023 | 2.34 |
| synth-uniform-low | P2 | 100098 | sweep | 6 | 0 | 0 | 17 | 0.215 | 2.61 |
| synth-uniform-low | P2 | 100098 | kahn | 6 | 6 | 0 | 83 | 0.616 | 2.71 |
| synth-uniform-low | P2 | 100098 | jp-index | 6 | 6 | 0 | 148 | 1.443 | 3.62 |
| synth-uniform-low | P2 | 100098 | jp-random | 6 | 6 | 0 | 148 | 1.511 | 3.59 |
| synth-uniform-low | P3 | 100098 | sweep | 5 | 0 | 0 | 17 | 0.170 | 4.02 |
| synth-uniform-low | P3 | 100098 | kahn | 5 | 5 | 0 | 83 | 1.734 | 5.15 |
| synth-uniform-low | P3 | 100098 | jp-index | 5 | 5 | 0 | 148 | 2.325 | 5.39 |
| synth-uniform-low | P3 | 100098 | jp-random | 6 | 6 | 0 | 148 | 1.862 | 5.00 |
| synth-uniform-high | P1 | 100081 | sweep | 1245 | 0 | 0 | 17 | 0.341 | 1.85 |
| synth-uniform-high | P1 | 100081 | kahn | 1245 | 1245 | 1245 | 20 | 1.487 | 4.07 |
| synth-uniform-high | P1 | 100081 | jp-index | 1245 | 1245 | 61 | 4757 | 97.170 | 98.55 |
| synth-uniform-high | P1 | 100081 | jp-random | 1249 | 1249 | 65 | 4757 | 93.479 | 94.87 |
| synth-uniform-high | P2 | 100081 | sweep | 412 | 0 | 0 | 17 | 0.874 | 2.79 |
| synth-uniform-high | P2 | 100081 | kahn | 412 | 412 | 412 | 20 | 2.247 | 4.12 |
| synth-uniform-high | P2 | 100081 | jp-index | 412 | 412 | 0 | 1684 | 52.742 | 54.59 |
| synth-uniform-high | P2 | 100081 | jp-random | 412 | 412 | 0 | 1684 | 53.294 | 55.28 |
| synth-uniform-high | P3 | 100081 | sweep | 399 | 0 | 0 | 17 | 0.886 | 2.49 |
| synth-uniform-high | P3 | 100081 | kahn | 399 | 399 | 399 | 20 | 2.163 | 3.88 |
| synth-uniform-high | P3 | 100081 | jp-index | 399 | 399 | 15 | 1557 | 49.097 | 51.32 |
| synth-uniform-high | P3 | 100081 | jp-random | 401 | 401 | 17 | 1557 | 50.258 | 52.57 |
| synth-zipf-low | P1 | 100103 | sweep | 9009 | 0 | 0 | 17 | 0.450 | 1.86 |
| synth-zipf-low | P1 | 100103 | kahn | 9009 | 9009 | 8977 | 84 | 7.649 | 9.23 |
| synth-zipf-low | P1 | 100103 | jp-index | 9009 | 9009 | 1201 | 31253 | 530.531 | 531.94 |
| synth-zipf-low | P1 | 100103 | jp-random | 9009 | 9009 | 1073 | 31765 | 545.669 | 548.00 |
| synth-zipf-low | P2 | 100103 | sweep | 5680 | 0 | 0 | 17 | 0.397 | 2.48 |
| synth-zipf-low | P2 | 100103 | kahn | 5680 | 5680 | 5648 | 84 | 5.508 | 7.07 |
| synth-zipf-low | P2 | 100103 | jp-index | 5680 | 5680 | 592 | 20373 | 385.989 | 387.47 |
| synth-zipf-low | P2 | 100103 | jp-random | 5680 | 5680 | 688 | 19989 | 385.254 | 387.61 |
| synth-zipf-low | P3 | 100103 | sweep | 5045 | 0 | 0 | 17 | 0.369 | 1.87 |
| synth-zipf-low | P3 | 100103 | kahn | 5045 | 5045 | 5013 | 84 | 4.399 | 5.87 |
| synth-zipf-low | P3 | 100103 | jp-index | 5045 | 5045 | 1589 | 13845 | 181.514 | 183.09 |
| synth-zipf-low | P3 | 100103 | jp-random | 5045 | 5045 | 1589 | 13845 | 199.277 | 200.83 |
| synth-zipf-high | P1 | 100081 | sweep | 13875 | 0 | 0 | 17 | 0.374 | 1.71 |
| synth-zipf-high | P1 | 100081 | kahn | 13875 | 13875 | 13875 | 20 | 11.448 | 13.19 |
| synth-zipf-high | P1 | 100081 | jp-index | 13875 | 13875 | 659 | 52885 | 1094.340 | 1095.62 |
| synth-zipf-high | P1 | 100081 | jp-random | 13881 | 13881 | 665 | 52885 | 1152.398 | 1154.22 |
| synth-zipf-high | P2 | 100081 | sweep | 8292 | 0 | 0 | 17 | 0.713 | 2.44 |
| synth-zipf-high | P2 | 100081 | kahn | 8292 | 8292 | 8292 | 20 | 14.729 | 16.73 |
| synth-zipf-high | P2 | 100081 | jp-index | 8292 | 8292 | 228 | 32277 | 1091.563 | 1093.22 |
| synth-zipf-high | P2 | 100081 | jp-random | 8280 | 8280 | 248 | 32149 | 1104.247 | 1106.14 |
| synth-zipf-high | P3 | 100081 | sweep | 7543 | 0 | 0 | 17 | 0.561 | 2.45 |
| synth-zipf-high | P3 | 100081 | kahn | 7543 | 7543 | 7511 | 84 | 13.634 | 15.42 |
| synth-zipf-high | P3 | 100081 | jp-index | 7543 | 7543 | 407 | 28565 | 724.317 | 726.36 |
| synth-zipf-high | P3 | 100081 | jp-random | 7543 | 7543 | 407 | 28565 | 754.401 | 756.37 |

![end-to-end](fig_e2e_vs_size.png)

## Schedule quality and simulated execution

Speedup = serial gas / simulated makespan on W workers (gas as cost, LPT inside a batch, barrier between batches). `bound` = per-key lower bound on batches for any schedule. `levels` preserve block order; `jp-random` may reorder.

| scenario | n | pol | method | batches | bound | tx/batch | W=8 | W=16 | W=32 | W=64 | W=inf |
|---|---:|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| synth-eth-like | 10001 | P1 | levels | 1406 | 1406 | 7.1 | 4.12 | 4.97 | 5.50 | 5.75 | 5.90 |
| synth-eth-like | 10001 | P1 | jp-random1 | 1407 | 1406 | 7.1 | 4.07 | 4.91 | 5.43 | 5.68 | 5.83 |
| synth-eth-like | 10001 | P1 | jp-random2 | 1406 | 1406 | 7.1 | 4.13 | 5.00 | 5.56 | 5.83 | 6.00 |
| synth-eth-like | 10001 | P1 | jp-random3 | 1406 | 1406 | 7.1 | 3.99 | 4.79 | 5.27 | 5.51 | 5.65 |
| synth-eth-like | 10001 | P2 | levels | 325 | 195 | 30.8 | 5.97 | 8.32 | 10.09 | 11.00 | 11.63 |
| synth-eth-like | 10001 | P2 | jp-random1 | 327 | 195 | 30.6 | 5.85 | 8.05 | 9.70 | 10.58 | 11.15 |
| synth-eth-like | 10001 | P2 | jp-random2 | 319 | 195 | 31.4 | 5.94 | 8.20 | 9.91 | 10.87 | 11.49 |
| synth-eth-like | 10001 | P2 | jp-random3 | 332 | 195 | 30.1 | 5.66 | 7.67 | 9.10 | 9.85 | 10.35 |
| synth-eth-like | 10001 | P3 | levels | 286 | 137 | 35.0 | 6.05 | 8.54 | 10.47 | 11.51 | 12.26 |
| synth-eth-like | 10001 | P3 | jp-random1 | 283 | 137 | 35.3 | 6.05 | 8.48 | 10.42 | 11.51 | 12.25 |
| synth-eth-like | 10001 | P3 | jp-random2 | 275 | 137 | 36.4 | 6.18 | 8.80 | 10.90 | 12.13 | 12.96 |
| synth-eth-like | 10001 | P3 | jp-random3 | 284 | 137 | 35.2 | 5.84 | 8.09 | 9.79 | 10.71 | 11.35 |
| synth-eth-like | 20031 | P1 | levels | 2891 | 2891 | 6.9 | 3.77 | 4.39 | 4.77 | 4.97 | 5.09 |
| synth-eth-like | 20031 | P1 | jp-random1 | 2891 | 2891 | 6.9 | 3.76 | 4.37 | 4.74 | 4.95 | 5.08 |
| synth-eth-like | 20031 | P1 | jp-random2 | 2891 | 2891 | 6.9 | 3.74 | 4.35 | 4.73 | 4.92 | 5.04 |
| synth-eth-like | 20031 | P1 | jp-random3 | 2891 | 2891 | 6.9 | 3.67 | 4.24 | 4.57 | 4.74 | 4.86 |
| synth-eth-like | 20031 | P2 | levels | 646 | 393 | 31.0 | 5.65 | 7.47 | 8.72 | 9.45 | 9.93 |
| synth-eth-like | 20031 | P2 | jp-random1 | 633 | 393 | 31.6 | 5.72 | 7.56 | 8.85 | 9.62 | 10.17 |
| synth-eth-like | 20031 | P2 | jp-random2 | 640 | 393 | 31.3 | 5.63 | 7.41 | 8.66 | 9.35 | 9.82 |
| synth-eth-like | 20031 | P2 | jp-random3 | 644 | 393 | 31.1 | 5.45 | 7.06 | 8.10 | 8.68 | 9.10 |
| synth-eth-like | 20031 | P3 | levels | 556 | 271 | 36.0 | 5.85 | 7.92 | 9.41 | 10.25 | 10.86 |
| synth-eth-like | 20031 | P3 | jp-random1 | 532 | 271 | 37.7 | 5.88 | 7.97 | 9.49 | 10.44 | 11.14 |
| synth-eth-like | 20031 | P3 | jp-random2 | 533 | 271 | 37.6 | 5.86 | 7.89 | 9.39 | 10.26 | 10.88 |
| synth-eth-like | 20031 | P3 | jp-random3 | 549 | 271 | 36.5 | 5.68 | 7.49 | 8.73 | 9.45 | 9.98 |
| synth-eth-like | 40060 | P1 | levels | 5901 | 5901 | 6.8 | 3.37 | 3.78 | 4.02 | 4.14 | 4.23 |
| synth-eth-like | 40060 | P1 | jp-random1 | 5901 | 5901 | 6.8 | 3.37 | 3.78 | 4.01 | 4.14 | 4.23 |
| synth-eth-like | 40060 | P1 | jp-random2 | 5901 | 5901 | 6.8 | 3.38 | 3.78 | 4.01 | 4.13 | 4.22 |
| synth-eth-like | 40060 | P1 | jp-random3 | 5901 | 5901 | 6.8 | 3.36 | 3.76 | 3.98 | 4.10 | 4.18 |
| synth-eth-like | 40060 | P2 | levels | 1311 | 780 | 30.6 | 5.27 | 6.56 | 7.35 | 7.79 | 8.12 |
| synth-eth-like | 40060 | P2 | jp-random1 | 1296 | 780 | 30.9 | 5.35 | 6.68 | 7.52 | 7.99 | 8.33 |
| synth-eth-like | 40060 | P2 | jp-random2 | 1309 | 780 | 30.6 | 5.33 | 6.60 | 7.39 | 7.85 | 8.18 |
| synth-eth-like | 40060 | P2 | jp-random3 | 1309 | 780 | 30.6 | 5.27 | 6.52 | 7.28 | 7.69 | 8.01 |
| synth-eth-like | 40060 | P3 | levels | 1118 | 554 | 35.8 | 5.53 | 7.06 | 8.04 | 8.61 | 9.04 |
| synth-eth-like | 40060 | P3 | jp-random1 | 1115 | 554 | 35.9 | 5.54 | 7.06 | 8.06 | 8.64 | 9.08 |
| synth-eth-like | 40060 | P3 | jp-random2 | 1101 | 554 | 36.4 | 5.56 | 7.10 | 8.07 | 8.64 | 9.10 |
| synth-eth-like | 40060 | P3 | jp-random3 | 1120 | 554 | 35.8 | 5.48 | 6.95 | 7.89 | 8.41 | 8.82 |
| synth-eth-like | 60070 | P1 | levels | 8741 | 8741 | 6.9 | 3.21 | 3.53 | 3.72 | 3.81 | 3.88 |
| synth-eth-like | 60070 | P1 | jp-random1 | 8742 | 8741 | 6.9 | 3.17 | 3.48 | 3.66 | 3.75 | 3.81 |
| synth-eth-like | 60070 | P1 | jp-random2 | 8741 | 8741 | 6.9 | 3.26 | 3.60 | 3.80 | 3.89 | 3.97 |
| synth-eth-like | 60070 | P1 | jp-random3 | 8741 | 8741 | 6.9 | 3.22 | 3.55 | 3.73 | 3.82 | 3.89 |
| synth-eth-like | 60070 | P2 | levels | 1943 | 1175 | 30.9 | 5.18 | 6.28 | 6.91 | 7.25 | 7.52 |
| synth-eth-like | 60070 | P2 | jp-random1 | 1941 | 1175 | 30.9 | 5.15 | 6.22 | 6.84 | 7.16 | 7.43 |
| synth-eth-like | 60070 | P2 | jp-random2 | 1946 | 1175 | 30.9 | 5.28 | 6.42 | 7.10 | 7.46 | 7.75 |
| synth-eth-like | 60070 | P2 | jp-random3 | 1952 | 1175 | 30.8 | 5.25 | 6.35 | 7.01 | 7.36 | 7.64 |
| synth-eth-like | 60070 | P3 | levels | 1658 | 826 | 36.2 | 5.46 | 6.80 | 7.61 | 8.06 | 8.42 |
| synth-eth-like | 60070 | P3 | jp-random1 | 1622 | 826 | 37.0 | 5.41 | 6.71 | 7.49 | 7.92 | 8.28 |
| synth-eth-like | 60070 | P3 | jp-random2 | 1654 | 826 | 36.3 | 5.48 | 6.84 | 7.67 | 8.14 | 8.52 |
| synth-eth-like | 60070 | P3 | jp-random3 | 1643 | 826 | 36.6 | 5.50 | 6.87 | 7.71 | 8.18 | 8.56 |
| synth-eth-like | 80001 | P1 | levels | 11664 | 11664 | 6.9 | 3.13 | 3.41 | 3.56 | 3.64 | 3.70 |
| synth-eth-like | 80001 | P1 | jp-random1 | 11664 | 11664 | 6.9 | 3.10 | 3.38 | 3.53 | 3.61 | 3.67 |
| synth-eth-like | 80001 | P1 | jp-random2 | 11665 | 11664 | 6.9 | 3.13 | 3.40 | 3.55 | 3.63 | 3.69 |
| synth-eth-like | 80001 | P1 | jp-random3 | 11665 | 11664 | 6.9 | 3.11 | 3.38 | 3.54 | 3.61 | 3.67 |
| synth-eth-like | 80001 | P2 | levels | 2582 | 1651 | 31.0 | 5.15 | 6.14 | 6.70 | 6.99 | 7.23 |
| synth-eth-like | 80001 | P2 | jp-random1 | 2592 | 1651 | 30.9 | 5.18 | 6.17 | 6.73 | 7.03 | 7.27 |
| synth-eth-like | 80001 | P2 | jp-random2 | 2605 | 1651 | 30.7 | 5.20 | 6.20 | 6.76 | 7.05 | 7.29 |
| synth-eth-like | 80001 | P2 | jp-random3 | 2595 | 1651 | 30.8 | 5.17 | 6.15 | 6.70 | 6.99 | 7.23 |
| synth-eth-like | 80001 | P3 | levels | 2220 | 1105 | 36.0 | 5.40 | 6.62 | 7.33 | 7.71 | 8.03 |
| synth-eth-like | 80001 | P3 | jp-random1 | 2213 | 1105 | 36.1 | 5.45 | 6.68 | 7.42 | 7.82 | 8.15 |
| synth-eth-like | 80001 | P3 | jp-random2 | 2247 | 1105 | 35.6 | 5.41 | 6.62 | 7.33 | 7.71 | 8.04 |
| synth-eth-like | 80001 | P3 | jp-random3 | 2238 | 1105 | 35.7 | 5.41 | 6.61 | 7.32 | 7.71 | 8.04 |
| synth-eth-like | 100119 | P1 | levels | 14444 | 14444 | 6.9 | 3.06 | 3.30 | 3.44 | 3.50 | 3.56 |
| synth-eth-like | 100119 | P1 | jp-random1 | 14444 | 14444 | 6.9 | 3.03 | 3.27 | 3.40 | 3.47 | 3.52 |
| synth-eth-like | 100119 | P1 | jp-random2 | 14445 | 14444 | 6.9 | 3.02 | 3.26 | 3.38 | 3.45 | 3.50 |
| synth-eth-like | 100119 | P1 | jp-random3 | 14447 | 14444 | 6.9 | 3.03 | 3.26 | 3.39 | 3.45 | 3.50 |
| synth-eth-like | 100119 | P2 | levels | 3163 | 2127 | 31.7 | 5.10 | 6.00 | 6.48 | 6.73 | 6.94 |
| synth-eth-like | 100119 | P2 | jp-random1 | 3170 | 2127 | 31.6 | 5.13 | 6.02 | 6.50 | 6.75 | 6.95 |
| synth-eth-like | 100119 | P2 | jp-random2 | 3148 | 2127 | 31.8 | 5.08 | 5.95 | 6.41 | 6.65 | 6.85 |
| synth-eth-like | 100119 | P2 | jp-random3 | 3162 | 2127 | 31.7 | 5.11 | 6.00 | 6.49 | 6.73 | 6.93 |
| synth-eth-like | 100119 | P3 | levels | 2717 | 1341 | 36.8 | 5.30 | 6.40 | 7.02 | 7.34 | 7.63 |
| synth-eth-like | 100119 | P3 | jp-random1 | 2723 | 1341 | 36.8 | 5.38 | 6.49 | 7.11 | 7.44 | 7.72 |
| synth-eth-like | 100119 | P3 | jp-random2 | 2683 | 1341 | 37.3 | 5.34 | 6.41 | 7.01 | 7.34 | 7.61 |
| synth-eth-like | 100119 | P3 | jp-random3 | 2714 | 1341 | 36.9 | 5.39 | 6.50 | 7.12 | 7.45 | 7.74 |
| synth-uniform-low | 10009 | P1 | levels | 4 | 4 | 2502.2 | 7.99 | 15.97 | 31.87 | 61.12 | 294.73 |
| synth-uniform-low | 10009 | P1 | jp-random1 | 4 | 4 | 2502.2 | 7.99 | 15.97 | 31.72 | 59.63 | 270.21 |
| synth-uniform-low | 10009 | P1 | jp-random2 | 4 | 4 | 2502.2 | 7.99 | 15.98 | 31.87 | 61.07 | 292.10 |
| synth-uniform-low | 10009 | P1 | jp-random3 | 4 | 4 | 2502.2 | 7.99 | 15.96 | 31.70 | 60.92 | 293.19 |
| synth-uniform-low | 10009 | P2 | levels | 3 | 3 | 3336.3 | 7.99 | 15.94 | 31.12 | 59.35 | 301.13 |
| synth-uniform-low | 10009 | P2 | jp-random1 | 3 | 3 | 3336.3 | 7.99 | 15.83 | 30.73 | 57.97 | 276.45 |
| synth-uniform-low | 10009 | P2 | jp-random2 | 3 | 3 | 3336.3 | 7.99 | 15.94 | 31.10 | 59.26 | 297.76 |
| synth-uniform-low | 10009 | P2 | jp-random3 | 3 | 3 | 3336.3 | 7.98 | 15.89 | 31.02 | 59.15 | 299.52 |
| synth-uniform-low | 10009 | P3 | levels | 3 | 3 | 3336.3 | 7.99 | 15.93 | 31.08 | 59.34 | 301.13 |
| synth-uniform-low | 10009 | P3 | jp-random1 | 3 | 3 | 3336.3 | 7.99 | 15.82 | 30.69 | 57.95 | 276.45 |
| synth-uniform-low | 10009 | P3 | jp-random2 | 3 | 3 | 3336.3 | 7.99 | 15.93 | 31.07 | 59.24 | 297.76 |
| synth-uniform-low | 10009 | P3 | jp-random3 | 3 | 3 | 3336.3 | 7.98 | 15.88 | 30.98 | 59.13 | 299.52 |
| synth-uniform-low | 20088 | P1 | levels | 5 | 5 | 4017.6 | 8.00 | 15.98 | 31.93 | 62.07 | 294.79 |
| synth-uniform-low | 20088 | P1 | jp-random1 | 5 | 5 | 4017.6 | 8.00 | 15.97 | 31.83 | 61.59 | 284.19 |
| synth-uniform-low | 20088 | P1 | jp-random2 | 5 | 5 | 4017.6 | 8.00 | 15.98 | 31.92 | 62.05 | 293.94 |
| synth-uniform-low | 20088 | P1 | jp-random3 | 5 | 5 | 4017.6 | 8.00 | 15.89 | 31.19 | 60.17 | 245.31 |
| synth-uniform-low | 20088 | P2 | levels | 3 | 3 | 6696.0 | 8.00 | 15.97 | 31.50 | 59.41 | 317.77 |
| synth-uniform-low | 20088 | P2 | jp-random1 | 3 | 3 | 6696.0 | 7.99 | 15.94 | 31.32 | 58.84 | 301.09 |
| synth-uniform-low | 20088 | P2 | jp-random2 | 3 | 3 | 6696.0 | 7.99 | 15.94 | 31.30 | 58.81 | 301.09 |
| synth-uniform-low | 20088 | P2 | jp-random3 | 3 | 3 | 6696.0 | 7.95 | 15.78 | 31.09 | 58.61 | 259.03 |
| synth-uniform-low | 20088 | P3 | levels | 3 | 3 | 6696.0 | 7.99 | 15.97 | 31.44 | 59.34 | 317.77 |
| synth-uniform-low | 20088 | P3 | jp-random1 | 3 | 3 | 6696.0 | 7.99 | 15.94 | 31.27 | 58.72 | 301.09 |
| synth-uniform-low | 20088 | P3 | jp-random2 | 3 | 3 | 6696.0 | 7.99 | 15.93 | 31.24 | 58.68 | 301.09 |
| synth-uniform-low | 20088 | P3 | jp-random3 | 3 | 3 | 6696.0 | 7.95 | 15.78 | 31.09 | 58.49 | 259.03 |
| synth-uniform-low | 40062 | P1 | levels | 7 | 7 | 5723.1 | 7.99 | 15.87 | 31.27 | 60.34 | 350.63 |
| synth-uniform-low | 40062 | P1 | jp-random1 | 7 | 7 | 5723.1 | 8.00 | 15.99 | 31.94 | 63.05 | 394.40 |
| synth-uniform-low | 40062 | P1 | jp-random2 | 7 | 7 | 5723.1 | 8.00 | 15.97 | 31.83 | 61.35 | 375.06 |
| synth-uniform-low | 40062 | P1 | jp-random3 | 7 | 7 | 5723.1 | 8.00 | 15.98 | 31.93 | 61.78 | 387.08 |
| synth-uniform-low | 40062 | P2 | levels | 4 | 4 | 10015.5 | 7.95 | 15.79 | 31.06 | 60.06 | 396.20 |
| synth-uniform-low | 40062 | P2 | jp-random1 | 4 | 4 | 10015.5 | 7.99 | 15.89 | 31.41 | 61.36 | 406.60 |
| synth-uniform-low | 40062 | P2 | jp-random2 | 5 | 4 | 8012.4 | 7.98 | 15.81 | 31.05 | 60.00 | 395.93 |
| synth-uniform-low | 40062 | P2 | jp-random3 | 4 | 4 | 10015.5 | 7.98 | 15.82 | 31.11 | 60.20 | 399.51 |
| synth-uniform-low | 40062 | P3 | levels | 4 | 4 | 10015.5 | 7.95 | 15.79 | 31.05 | 60.04 | 396.20 |
| synth-uniform-low | 40062 | P3 | jp-random1 | 4 | 4 | 10015.5 | 7.99 | 15.88 | 31.39 | 61.34 | 406.60 |
| synth-uniform-low | 40062 | P3 | jp-random2 | 4 | 4 | 10015.5 | 7.98 | 15.82 | 31.10 | 60.16 | 403.97 |
| synth-uniform-low | 40062 | P3 | jp-random3 | 4 | 4 | 10015.5 | 7.98 | 15.82 | 31.11 | 60.18 | 399.51 |
| synth-uniform-low | 60099 | P1 | levels | 8 | 8 | 7512.4 | 8.00 | 15.99 | 31.85 | 62.45 | 457.99 |
| synth-uniform-low | 60099 | P1 | jp-random1 | 8 | 8 | 7512.4 | 8.00 | 15.99 | 31.96 | 63.81 | 675.96 |
| synth-uniform-low | 60099 | P1 | jp-random2 | 8 | 8 | 7512.4 | 8.00 | 15.99 | 31.84 | 62.42 | 491.85 |
| synth-uniform-low | 60099 | P1 | jp-random3 | 8 | 8 | 7512.4 | 8.00 | 15.99 | 31.95 | 63.80 | 564.25 |
| synth-uniform-low | 60099 | P2 | levels | 7 | 7 | 8585.6 | 7.97 | 15.85 | 31.37 | 61.05 | 484.76 |
| synth-uniform-low | 60099 | P2 | jp-random1 | 7 | 7 | 8585.6 | 8.00 | 15.97 | 31.83 | 62.91 | 700.73 |
| synth-uniform-low | 60099 | P2 | jp-random2 | 7 | 7 | 8585.6 | 7.97 | 15.85 | 31.35 | 60.65 | 496.76 |
| synth-uniform-low | 60099 | P2 | jp-random3 | 7 | 7 | 8585.6 | 8.00 | 15.97 | 31.79 | 62.46 | 584.93 |
| synth-uniform-low | 60099 | P3 | levels | 7 | 7 | 8585.6 | 7.97 | 15.85 | 31.37 | 61.01 | 484.76 |
| synth-uniform-low | 60099 | P3 | jp-random1 | 7 | 7 | 8585.6 | 8.00 | 15.97 | 31.83 | 62.88 | 700.73 |
| synth-uniform-low | 60099 | P3 | jp-random2 | 7 | 7 | 8585.6 | 7.97 | 15.85 | 31.33 | 60.63 | 496.76 |
| synth-uniform-low | 60099 | P3 | jp-random3 | 7 | 7 | 8585.6 | 7.99 | 15.97 | 31.76 | 62.41 | 585.04 |
| synth-uniform-low | 80268 | P1 | levels | 9 | 9 | 8918.7 | 8.00 | 15.99 | 31.93 | 63.41 | 579.91 |
| synth-uniform-low | 80268 | P1 | jp-random1 | 9 | 9 | 8918.7 | 8.00 | 15.99 | 31.97 | 63.49 | 592.49 |
| synth-uniform-low | 80268 | P1 | jp-random2 | 9 | 9 | 8918.7 | 8.00 | 15.99 | 31.96 | 63.50 | 571.64 |
| synth-uniform-low | 80268 | P1 | jp-random3 | 9 | 9 | 8918.7 | 8.00 | 15.99 | 31.93 | 63.40 | 584.61 |
| synth-uniform-low | 80268 | P2 | levels | 5 | 5 | 16053.6 | 7.98 | 15.89 | 31.50 | 61.90 | 652.78 |
| synth-uniform-low | 80268 | P2 | jp-random1 | 5 | 5 | 16053.6 | 7.99 | 15.93 | 31.66 | 61.87 | 614.43 |
| synth-uniform-low | 80268 | P2 | jp-random2 | 5 | 5 | 16053.6 | 7.98 | 15.90 | 31.51 | 61.66 | 609.41 |
| synth-uniform-low | 80268 | P2 | jp-random3 | 5 | 5 | 16053.6 | 7.98 | 15.90 | 31.52 | 61.97 | 632.51 |
| synth-uniform-low | 80268 | P3 | levels | 5 | 5 | 16053.6 | 7.98 | 15.89 | 31.50 | 61.91 | 652.78 |
| synth-uniform-low | 80268 | P3 | jp-random1 | 5 | 5 | 16053.6 | 7.99 | 15.94 | 31.70 | 62.03 | 637.67 |
| synth-uniform-low | 80268 | P3 | jp-random2 | 5 | 5 | 16053.6 | 7.98 | 15.90 | 31.51 | 61.61 | 609.41 |
| synth-uniform-low | 80268 | P3 | jp-random3 | 5 | 5 | 16053.6 | 7.98 | 15.90 | 31.51 | 61.95 | 632.51 |
| synth-uniform-low | 100098 | P1 | levels | 10 | 10 | 10009.8 | 8.00 | 15.98 | 31.93 | 63.62 | 824.76 |
| synth-uniform-low | 100098 | P1 | jp-random1 | 10 | 10 | 10009.8 | 8.00 | 15.99 | 31.93 | 63.57 | 694.49 |
| synth-uniform-low | 100098 | P1 | jp-random2 | 10 | 10 | 10009.8 | 8.00 | 15.99 | 31.95 | 63.67 | 773.98 |
| synth-uniform-low | 100098 | P1 | jp-random3 | 10 | 10 | 10009.8 | 8.00 | 15.99 | 31.89 | 63.40 | 696.61 |
| synth-uniform-low | 100098 | P2 | levels | 6 | 5 | 16683.0 | 8.00 | 15.98 | 31.91 | 63.41 | 992.95 |
| synth-uniform-low | 100098 | P2 | jp-random1 | 6 | 5 | 16683.0 | 8.00 | 15.99 | 31.86 | 63.23 | 849.95 |
| synth-uniform-low | 100098 | P2 | jp-random2 | 6 | 5 | 16683.0 | 8.00 | 15.99 | 31.95 | 63.60 | 955.76 |
| synth-uniform-low | 100098 | P2 | jp-random3 | 5 | 5 | 20019.6 | 8.00 | 15.98 | 31.83 | 63.08 | 839.13 |
| synth-uniform-low | 100098 | P3 | levels | 5 | 5 | 20019.6 | 8.00 | 15.98 | 31.91 | 63.42 | 998.75 |
| synth-uniform-low | 100098 | P3 | jp-random1 | 6 | 5 | 16683.0 | 8.00 | 15.99 | 31.86 | 63.22 | 849.95 |
| synth-uniform-low | 100098 | P3 | jp-random2 | 6 | 5 | 16683.0 | 8.00 | 15.99 | 31.95 | 63.61 | 955.76 |
| synth-uniform-low | 100098 | P3 | jp-random3 | 5 | 5 | 20019.6 | 8.00 | 15.98 | 31.83 | 63.08 | 839.13 |
| synth-uniform-high | 10114 | P1 | levels | 131 | 115 | 77.2 | 7.40 | 9.69 | 9.89 | 9.89 | 9.89 |
| synth-uniform-high | 10114 | P1 | jp-random1 | 136 | 115 | 74.4 | 7.33 | 9.51 | 9.68 | 9.68 | 9.68 |
| synth-uniform-high | 10114 | P1 | jp-random2 | 129 | 115 | 78.4 | 7.59 | 9.73 | 9.83 | 9.83 | 9.83 |
| synth-uniform-high | 10114 | P1 | jp-random3 | 128 | 115 | 79.0 | 7.42 | 9.73 | 9.93 | 9.93 | 9.93 |
| synth-uniform-high | 10114 | P2 | levels | 47 | 40 | 215.2 | 7.76 | 13.97 | 18.38 | 19.22 | 19.28 |
| synth-uniform-high | 10114 | P2 | jp-random1 | 46 | 40 | 219.9 | 7.81 | 14.31 | 19.25 | 19.98 | 20.11 |
| synth-uniform-high | 10114 | P2 | jp-random2 | 48 | 40 | 210.7 | 7.83 | 14.83 | 20.37 | 20.84 | 20.84 |
| synth-uniform-high | 10114 | P2 | jp-random3 | 46 | 40 | 219.9 | 7.81 | 14.28 | 19.09 | 20.11 | 20.17 |
| synth-uniform-high | 10114 | P3 | levels | 47 | 40 | 215.2 | 7.76 | 13.92 | 18.09 | 19.32 | 19.51 |
| synth-uniform-high | 10114 | P3 | jp-random1 | 46 | 40 | 219.9 | 7.79 | 14.08 | 18.56 | 19.63 | 19.88 |
| synth-uniform-high | 10114 | P3 | jp-random2 | 47 | 40 | 215.2 | 7.83 | 14.60 | 20.31 | 20.85 | 20.98 |
| synth-uniform-high | 10114 | P3 | jp-random3 | 45 | 40 | 224.8 | 7.85 | 14.42 | 19.37 | 20.74 | 20.97 |
| synth-uniform-high | 20155 | P1 | levels | 257 | 215 | 78.4 | 7.37 | 9.63 | 9.78 | 9.78 | 9.78 |
| synth-uniform-high | 20155 | P1 | jp-random1 | 256 | 215 | 78.7 | 7.35 | 9.57 | 9.70 | 9.70 | 9.70 |
| synth-uniform-high | 20155 | P1 | jp-random2 | 249 | 215 | 80.9 | 7.41 | 9.95 | 10.12 | 10.13 | 10.13 |
| synth-uniform-high | 20155 | P1 | jp-random3 | 257 | 215 | 78.4 | 7.35 | 9.65 | 9.82 | 9.82 | 9.82 |
| synth-uniform-high | 20155 | P2 | levels | 86 | 71 | 234.4 | 7.90 | 14.54 | 19.67 | 20.30 | 20.34 |
| synth-uniform-high | 20155 | P2 | jp-random1 | 89 | 71 | 226.5 | 7.81 | 14.59 | 19.62 | 20.07 | 20.09 |
| synth-uniform-high | 20155 | P2 | jp-random2 | 86 | 71 | 234.4 | 7.92 | 14.71 | 20.08 | 20.90 | 20.96 |
| synth-uniform-high | 20155 | P2 | jp-random3 | 93 | 71 | 216.7 | 7.85 | 14.09 | 18.88 | 19.48 | 19.53 |
| synth-uniform-high | 20155 | P3 | levels | 85 | 71 | 237.1 | 7.89 | 14.43 | 19.04 | 19.90 | 20.08 |
| synth-uniform-high | 20155 | P3 | jp-random1 | 89 | 71 | 226.5 | 7.80 | 14.40 | 19.10 | 19.72 | 19.81 |
| synth-uniform-high | 20155 | P3 | jp-random2 | 86 | 71 | 234.4 | 7.89 | 14.37 | 19.11 | 20.04 | 20.16 |
| synth-uniform-high | 20155 | P3 | jp-random3 | 92 | 71 | 219.1 | 7.88 | 14.14 | 19.35 | 20.25 | 20.42 |
| synth-uniform-high | 40189 | P1 | levels | 507 | 399 | 79.3 | 7.34 | 9.49 | 9.60 | 9.60 | 9.60 |
| synth-uniform-high | 40189 | P1 | jp-random1 | 493 | 399 | 81.5 | 7.44 | 9.84 | 9.99 | 9.99 | 9.99 |
| synth-uniform-high | 40189 | P1 | jp-random2 | 515 | 399 | 78.0 | 7.43 | 9.68 | 9.78 | 9.78 | 9.78 |
| synth-uniform-high | 40189 | P1 | jp-random3 | 515 | 399 | 78.0 | 7.36 | 9.68 | 9.82 | 9.82 | 9.82 |
| synth-uniform-high | 40189 | P2 | levels | 169 | 127 | 237.8 | 7.92 | 14.77 | 19.87 | 20.23 | 20.23 |
| synth-uniform-high | 40189 | P2 | jp-random1 | 181 | 127 | 222.0 | 7.89 | 14.64 | 19.33 | 19.78 | 19.80 |
| synth-uniform-high | 40189 | P2 | jp-random2 | 172 | 127 | 233.7 | 7.91 | 14.87 | 20.13 | 20.44 | 20.45 |
| synth-uniform-high | 40189 | P2 | jp-random3 | 175 | 127 | 229.7 | 7.92 | 14.82 | 19.79 | 20.28 | 20.30 |
| synth-uniform-high | 40189 | P3 | levels | 168 | 127 | 239.2 | 7.93 | 14.82 | 20.23 | 20.80 | 20.84 |
| synth-uniform-high | 40189 | P3 | jp-random1 | 180 | 127 | 223.3 | 7.88 | 14.56 | 19.39 | 19.96 | 20.05 |
| synth-uniform-high | 40189 | P3 | jp-random2 | 172 | 127 | 233.7 | 7.90 | 14.79 | 20.22 | 20.76 | 20.78 |
| synth-uniform-high | 40189 | P3 | jp-random3 | 160 | 127 | 251.2 | 7.92 | 14.93 | 20.71 | 21.46 | 21.57 |
| synth-uniform-high | 60032 | P1 | levels | 747 | 594 | 80.4 | 7.42 | 9.78 | 9.90 | 9.90 | 9.90 |
| synth-uniform-high | 60032 | P1 | jp-random1 | 754 | 594 | 79.6 | 7.40 | 9.62 | 9.74 | 9.74 | 9.74 |
| synth-uniform-high | 60032 | P1 | jp-random2 | 752 | 594 | 79.8 | 7.41 | 9.80 | 9.92 | 9.92 | 9.92 |
| synth-uniform-high | 60032 | P1 | jp-random3 | 763 | 594 | 78.7 | 7.41 | 9.65 | 9.76 | 9.76 | 9.76 |
| synth-uniform-high | 60032 | P2 | levels | 254 | 177 | 236.3 | 7.93 | 14.90 | 20.20 | 20.58 | 20.58 |
| synth-uniform-high | 60032 | P2 | jp-random1 | 247 | 177 | 243.0 | 7.93 | 15.03 | 20.67 | 21.11 | 21.13 |
| synth-uniform-high | 60032 | P2 | jp-random2 | 255 | 177 | 235.4 | 7.94 | 14.99 | 20.57 | 20.99 | 21.00 |
| synth-uniform-high | 60032 | P2 | jp-random3 | 243 | 177 | 247.0 | 7.95 | 15.15 | 20.95 | 21.40 | 21.43 |
| synth-uniform-high | 60032 | P3 | levels | 242 | 177 | 248.1 | 7.93 | 14.96 | 20.78 | 21.35 | 21.37 |
| synth-uniform-high | 60032 | P3 | jp-random1 | 237 | 177 | 253.3 | 7.95 | 15.15 | 20.99 | 21.59 | 21.66 |
| synth-uniform-high | 60032 | P3 | jp-random2 | 248 | 177 | 242.1 | 7.95 | 14.95 | 20.38 | 20.91 | 20.98 |
| synth-uniform-high | 60032 | P3 | jp-random3 | 242 | 177 | 248.1 | 7.93 | 15.13 | 20.86 | 21.38 | 21.46 |
| synth-uniform-high | 80061 | P1 | levels | 999 | 781 | 80.1 | 7.33 | 9.42 | 9.52 | 9.52 | 9.52 |
| synth-uniform-high | 80061 | P1 | jp-random1 | 970 | 781 | 82.5 | 7.38 | 9.66 | 9.76 | 9.76 | 9.76 |
| synth-uniform-high | 80061 | P1 | jp-random2 | 1032 | 781 | 77.6 | 7.31 | 9.34 | 9.42 | 9.42 | 9.42 |
| synth-uniform-high | 80061 | P1 | jp-random3 | 998 | 781 | 80.2 | 7.32 | 9.48 | 9.58 | 9.58 | 9.58 |
| synth-uniform-high | 80061 | P2 | levels | 330 | 233 | 242.6 | 7.93 | 14.80 | 19.74 | 20.00 | 20.00 |
| synth-uniform-high | 80061 | P2 | jp-random1 | 334 | 233 | 239.7 | 7.93 | 14.81 | 19.71 | 19.96 | 19.97 |
| synth-uniform-high | 80061 | P2 | jp-random2 | 337 | 233 | 237.6 | 7.93 | 14.79 | 19.51 | 19.72 | 19.74 |
| synth-uniform-high | 80061 | P2 | jp-random3 | 329 | 233 | 243.3 | 7.93 | 14.88 | 19.85 | 20.21 | 20.22 |
| synth-uniform-high | 80061 | P3 | levels | 329 | 233 | 243.3 | 7.92 | 14.81 | 19.84 | 20.22 | 20.24 |
| synth-uniform-high | 80061 | P3 | jp-random1 | 325 | 233 | 246.3 | 7.92 | 14.84 | 20.25 | 20.68 | 20.72 |
| synth-uniform-high | 80061 | P3 | jp-random2 | 330 | 233 | 242.6 | 7.93 | 14.85 | 19.99 | 20.36 | 20.41 |
| synth-uniform-high | 80061 | P3 | jp-random3 | 315 | 233 | 254.2 | 7.92 | 14.91 | 20.27 | 20.72 | 20.76 |
| synth-uniform-high | 100081 | P1 | levels | 1245 | 976 | 80.4 | 7.37 | 9.60 | 9.70 | 9.70 | 9.70 |
| synth-uniform-high | 100081 | P1 | jp-random1 | 1249 | 976 | 80.1 | 7.35 | 9.55 | 9.64 | 9.64 | 9.64 |
| synth-uniform-high | 100081 | P1 | jp-random2 | 1239 | 976 | 80.8 | 7.38 | 9.66 | 9.76 | 9.76 | 9.76 |
| synth-uniform-high | 100081 | P1 | jp-random3 | 1227 | 976 | 81.6 | 7.40 | 9.67 | 9.76 | 9.76 | 9.76 |
| synth-uniform-high | 100081 | P2 | levels | 412 | 291 | 242.9 | 7.94 | 14.94 | 20.19 | 20.48 | 20.50 |
| synth-uniform-high | 100081 | P2 | jp-random1 | 412 | 291 | 242.9 | 7.95 | 14.93 | 20.04 | 20.35 | 20.36 |
| synth-uniform-high | 100081 | P2 | jp-random2 | 411 | 291 | 243.5 | 7.94 | 14.92 | 19.99 | 20.27 | 20.28 |
| synth-uniform-high | 100081 | P2 | jp-random3 | 411 | 291 | 243.5 | 7.95 | 15.00 | 20.51 | 20.87 | 20.87 |
| synth-uniform-high | 100081 | P3 | levels | 399 | 291 | 250.8 | 7.93 | 14.94 | 20.40 | 20.75 | 20.80 |
| synth-uniform-high | 100081 | P3 | jp-random1 | 401 | 291 | 249.6 | 7.94 | 14.94 | 20.42 | 20.83 | 20.87 |
| synth-uniform-high | 100081 | P3 | jp-random2 | 398 | 291 | 251.5 | 7.95 | 15.01 | 20.44 | 20.86 | 20.89 |
| synth-uniform-high | 100081 | P3 | jp-random3 | 403 | 291 | 248.3 | 7.95 | 15.01 | 20.71 | 21.17 | 21.19 |
| synth-zipf-low | 10100 | P1 | levels | 888 | 888 | 11.4 | 3.81 | 4.34 | 4.59 | 4.70 | 4.74 |
| synth-zipf-low | 10100 | P1 | jp-random1 | 888 | 888 | 11.4 | 4.03 | 4.62 | 4.90 | 5.01 | 5.06 |
| synth-zipf-low | 10100 | P1 | jp-random2 | 888 | 888 | 11.4 | 3.98 | 4.57 | 4.85 | 4.96 | 5.02 |
| synth-zipf-low | 10100 | P1 | jp-random3 | 888 | 888 | 11.4 | 3.91 | 4.44 | 4.70 | 4.79 | 4.84 |
| synth-zipf-low | 10100 | P2 | levels | 560 | 560 | 18.0 | 4.19 | 5.00 | 5.46 | 5.69 | 5.83 |
| synth-zipf-low | 10100 | P2 | jp-random1 | 560 | 560 | 18.0 | 4.27 | 5.13 | 5.61 | 5.83 | 5.98 |
| synth-zipf-low | 10100 | P2 | jp-random2 | 560 | 560 | 18.0 | 4.27 | 5.15 | 5.64 | 5.88 | 6.03 |
| synth-zipf-low | 10100 | P2 | jp-random3 | 560 | 560 | 18.0 | 4.20 | 4.99 | 5.45 | 5.66 | 5.80 |
| synth-zipf-low | 10100 | P3 | levels | 450 | 450 | 22.4 | 4.09 | 4.96 | 5.50 | 5.80 | 6.04 |
| synth-zipf-low | 10100 | P3 | jp-random1 | 450 | 450 | 22.4 | 4.18 | 5.10 | 5.66 | 5.96 | 6.21 |
| synth-zipf-low | 10100 | P3 | jp-random2 | 450 | 450 | 22.4 | 4.20 | 5.13 | 5.70 | 6.00 | 6.26 |
| synth-zipf-low | 10100 | P3 | jp-random3 | 450 | 450 | 22.4 | 4.13 | 5.01 | 5.56 | 5.84 | 6.08 |
| synth-zipf-low | 20003 | P1 | levels | 1790 | 1790 | 11.2 | 3.69 | 4.13 | 4.35 | 4.44 | 4.49 |
| synth-zipf-low | 20003 | P1 | jp-random1 | 1790 | 1790 | 11.2 | 3.75 | 4.18 | 4.40 | 4.49 | 4.54 |
| synth-zipf-low | 20003 | P1 | jp-random2 | 1790 | 1790 | 11.2 | 3.68 | 4.10 | 4.31 | 4.40 | 4.45 |
| synth-zipf-low | 20003 | P1 | jp-random3 | 1790 | 1790 | 11.2 | 3.78 | 4.22 | 4.44 | 4.53 | 4.58 |
| synth-zipf-low | 20003 | P2 | levels | 1123 | 1123 | 17.8 | 4.07 | 4.78 | 5.19 | 5.39 | 5.53 |
| synth-zipf-low | 20003 | P2 | jp-random1 | 1123 | 1123 | 17.8 | 4.10 | 4.83 | 5.23 | 5.43 | 5.58 |
| synth-zipf-low | 20003 | P2 | jp-random2 | 1123 | 1123 | 17.8 | 4.07 | 4.77 | 5.16 | 5.35 | 5.50 |
| synth-zipf-low | 20003 | P2 | jp-random3 | 1123 | 1123 | 17.8 | 4.03 | 4.72 | 5.10 | 5.29 | 5.43 |
| synth-zipf-low | 20003 | P3 | levels | 971 | 971 | 20.6 | 4.01 | 4.83 | 5.35 | 5.62 | 5.86 |
| synth-zipf-low | 20003 | P3 | jp-random1 | 971 | 971 | 20.6 | 4.00 | 4.82 | 5.31 | 5.59 | 5.83 |
| synth-zipf-low | 20003 | P3 | jp-random2 | 971 | 971 | 20.6 | 3.98 | 4.78 | 5.27 | 5.53 | 5.76 |
| synth-zipf-low | 20003 | P3 | jp-random3 | 971 | 971 | 20.6 | 4.04 | 4.86 | 5.36 | 5.63 | 5.88 |
| synth-zipf-low | 40197 | P1 | levels | 3663 | 3663 | 11.0 | 3.52 | 3.84 | 4.00 | 4.07 | 4.10 |
| synth-zipf-low | 40197 | P1 | jp-random1 | 3663 | 3663 | 11.0 | 3.40 | 3.70 | 3.84 | 3.90 | 3.93 |
| synth-zipf-low | 40197 | P1 | jp-random2 | 3663 | 3663 | 11.0 | 3.53 | 3.85 | 4.01 | 4.08 | 4.12 |
| synth-zipf-low | 40197 | P1 | jp-random3 | 3663 | 3663 | 11.0 | 3.48 | 3.78 | 3.94 | 4.01 | 4.05 |
| synth-zipf-low | 40197 | P2 | levels | 2322 | 2322 | 17.3 | 3.88 | 4.44 | 4.76 | 4.92 | 5.03 |
| synth-zipf-low | 40197 | P2 | jp-random1 | 2322 | 2322 | 17.3 | 3.84 | 4.39 | 4.70 | 4.84 | 4.95 |
| synth-zipf-low | 40197 | P2 | jp-random2 | 2322 | 2322 | 17.3 | 3.91 | 4.48 | 4.80 | 4.96 | 5.07 |
| synth-zipf-low | 40197 | P2 | jp-random3 | 2322 | 2322 | 17.3 | 3.93 | 4.51 | 4.83 | 5.00 | 5.12 |
| synth-zipf-low | 40197 | P3 | levels | 1995 | 1995 | 20.1 | 3.85 | 4.55 | 4.99 | 5.22 | 5.43 |
| synth-zipf-low | 40197 | P3 | jp-random1 | 1995 | 1995 | 20.1 | 3.90 | 4.63 | 5.07 | 5.30 | 5.52 |
| synth-zipf-low | 40197 | P3 | jp-random2 | 1995 | 1995 | 20.1 | 3.88 | 4.60 | 5.04 | 5.27 | 5.49 |
| synth-zipf-low | 40197 | P3 | jp-random3 | 1995 | 1995 | 20.1 | 3.85 | 4.55 | 4.98 | 5.21 | 5.42 |
| synth-zipf-low | 60055 | P1 | levels | 5476 | 5476 | 11.0 | 3.45 | 3.72 | 3.85 | 3.92 | 3.95 |
| synth-zipf-low | 60055 | P1 | jp-random1 | 5476 | 5476 | 11.0 | 3.39 | 3.65 | 3.78 | 3.83 | 3.87 |
| synth-zipf-low | 60055 | P1 | jp-random2 | 5476 | 5476 | 11.0 | 3.42 | 3.69 | 3.82 | 3.88 | 3.91 |
| synth-zipf-low | 60055 | P1 | jp-random3 | 5476 | 5476 | 11.0 | 3.37 | 3.64 | 3.77 | 3.83 | 3.86 |
| synth-zipf-low | 60055 | P2 | levels | 3446 | 3446 | 17.4 | 3.83 | 4.33 | 4.61 | 4.75 | 4.86 |
| synth-zipf-low | 60055 | P2 | jp-random1 | 3446 | 3446 | 17.4 | 3.83 | 4.33 | 4.60 | 4.74 | 4.84 |
| synth-zipf-low | 60055 | P2 | jp-random2 | 3446 | 3446 | 17.4 | 3.82 | 4.33 | 4.60 | 4.74 | 4.85 |
| synth-zipf-low | 60055 | P2 | jp-random3 | 3446 | 3446 | 17.4 | 3.87 | 4.38 | 4.67 | 4.81 | 4.92 |
| synth-zipf-low | 60055 | P3 | levels | 2968 | 2968 | 20.2 | 3.83 | 4.51 | 4.92 | 5.14 | 5.34 |
| synth-zipf-low | 60055 | P3 | jp-random1 | 2968 | 2968 | 20.2 | 3.83 | 4.51 | 4.92 | 5.14 | 5.34 |
| synth-zipf-low | 60055 | P3 | jp-random2 | 2968 | 2968 | 20.2 | 3.85 | 4.53 | 4.94 | 5.17 | 5.37 |
| synth-zipf-low | 60055 | P3 | jp-random3 | 2968 | 2968 | 20.2 | 3.81 | 4.47 | 4.88 | 5.10 | 5.30 |
| synth-zipf-low | 80103 | P1 | levels | 7183 | 7183 | 11.2 | 3.39 | 3.62 | 3.74 | 3.79 | 3.82 |
| synth-zipf-low | 80103 | P1 | jp-random1 | 7183 | 7183 | 11.2 | 3.36 | 3.59 | 3.69 | 3.74 | 3.78 |
| synth-zipf-low | 80103 | P1 | jp-random2 | 7183 | 7183 | 11.2 | 3.33 | 3.56 | 3.67 | 3.72 | 3.75 |
| synth-zipf-low | 80103 | P1 | jp-random3 | 7183 | 7183 | 11.2 | 3.34 | 3.57 | 3.68 | 3.73 | 3.76 |
| synth-zipf-low | 80103 | P2 | levels | 4607 | 4607 | 17.4 | 3.78 | 4.23 | 4.47 | 4.59 | 4.69 |
| synth-zipf-low | 80103 | P2 | jp-random1 | 4607 | 4607 | 17.4 | 3.77 | 4.21 | 4.45 | 4.56 | 4.66 |
| synth-zipf-low | 80103 | P2 | jp-random2 | 4607 | 4607 | 17.4 | 3.75 | 4.19 | 4.43 | 4.55 | 4.64 |
| synth-zipf-low | 80103 | P2 | jp-random3 | 4607 | 4607 | 17.4 | 3.74 | 4.18 | 4.42 | 4.54 | 4.63 |
| synth-zipf-low | 80103 | P3 | levels | 4005 | 4005 | 20.0 | 3.74 | 4.35 | 4.72 | 4.92 | 5.11 |
| synth-zipf-low | 80103 | P3 | jp-random1 | 4005 | 4005 | 20.0 | 3.73 | 4.35 | 4.71 | 4.90 | 5.09 |
| synth-zipf-low | 80103 | P3 | jp-random2 | 4005 | 4005 | 20.0 | 3.77 | 4.39 | 4.77 | 4.97 | 5.16 |
| synth-zipf-low | 80103 | P3 | jp-random3 | 4005 | 4005 | 20.0 | 3.76 | 4.38 | 4.76 | 4.96 | 5.15 |
| synth-zipf-low | 100103 | P1 | levels | 9009 | 9009 | 11.1 | 3.34 | 3.55 | 3.65 | 3.70 | 3.73 |
| synth-zipf-low | 100103 | P1 | jp-random1 | 9009 | 9009 | 11.1 | 3.27 | 3.46 | 3.56 | 3.60 | 3.63 |
| synth-zipf-low | 100103 | P1 | jp-random2 | 9009 | 9009 | 11.1 | 3.32 | 3.53 | 3.63 | 3.68 | 3.71 |
| synth-zipf-low | 100103 | P1 | jp-random3 | 9009 | 9009 | 11.1 | 3.30 | 3.49 | 3.59 | 3.63 | 3.67 |
| synth-zipf-low | 100103 | P2 | levels | 5680 | 5680 | 17.6 | 3.78 | 4.18 | 4.41 | 4.52 | 4.62 |
| synth-zipf-low | 100103 | P2 | jp-random1 | 5680 | 5680 | 17.6 | 3.80 | 4.22 | 4.45 | 4.57 | 4.66 |
| synth-zipf-low | 100103 | P2 | jp-random2 | 5680 | 5680 | 17.6 | 3.72 | 4.12 | 4.34 | 4.45 | 4.54 |
| synth-zipf-low | 100103 | P2 | jp-random3 | 5680 | 5680 | 17.6 | 3.74 | 4.14 | 4.36 | 4.48 | 4.57 |
| synth-zipf-low | 100103 | P3 | levels | 5045 | 5045 | 19.8 | 3.69 | 4.27 | 4.62 | 4.81 | 4.99 |
| synth-zipf-low | 100103 | P3 | jp-random1 | 5045 | 5045 | 19.8 | 3.69 | 4.26 | 4.61 | 4.80 | 4.97 |
| synth-zipf-low | 100103 | P3 | jp-random2 | 5045 | 5045 | 19.8 | 3.68 | 4.26 | 4.61 | 4.80 | 4.97 |
| synth-zipf-low | 100103 | P3 | jp-random3 | 5045 | 5045 | 19.8 | 3.71 | 4.30 | 4.65 | 4.84 | 5.02 |
| synth-zipf-high | 10115 | P1 | levels | 1426 | 1418 | 7.1 | 2.56 | 2.59 | 2.61 | 2.61 | 2.61 |
| synth-zipf-high | 10115 | P1 | jp-random1 | 1419 | 1418 | 7.1 | 2.62 | 2.66 | 2.67 | 2.67 | 2.67 |
| synth-zipf-high | 10115 | P1 | jp-random2 | 1425 | 1418 | 7.1 | 2.63 | 2.67 | 2.69 | 2.69 | 2.69 |
| synth-zipf-high | 10115 | P1 | jp-random3 | 1420 | 1418 | 7.1 | 2.61 | 2.64 | 2.65 | 2.66 | 2.66 |
| synth-zipf-high | 10115 | P2 | levels | 825 | 813 | 12.3 | 3.20 | 3.32 | 3.38 | 3.40 | 3.40 |
| synth-zipf-high | 10115 | P2 | jp-random1 | 857 | 813 | 11.8 | 3.16 | 3.28 | 3.33 | 3.35 | 3.36 |
| synth-zipf-high | 10115 | P2 | jp-random2 | 844 | 813 | 12.0 | 3.25 | 3.38 | 3.43 | 3.45 | 3.45 |
| synth-zipf-high | 10115 | P2 | jp-random3 | 828 | 813 | 12.2 | 3.22 | 3.34 | 3.39 | 3.41 | 3.42 |
| synth-zipf-high | 10115 | P3 | levels | 718 | 716 | 14.1 | 3.18 | 3.43 | 3.56 | 3.61 | 3.64 |
| synth-zipf-high | 10115 | P3 | jp-random1 | 724 | 716 | 14.0 | 3.20 | 3.45 | 3.58 | 3.63 | 3.66 |
| synth-zipf-high | 10115 | P3 | jp-random2 | 722 | 716 | 14.0 | 3.31 | 3.60 | 3.74 | 3.80 | 3.83 |
| synth-zipf-high | 10115 | P3 | jp-random3 | 722 | 716 | 14.0 | 3.18 | 3.41 | 3.54 | 3.59 | 3.62 |
| synth-zipf-high | 20256 | P1 | levels | 2843 | 2833 | 7.1 | 2.52 | 2.55 | 2.56 | 2.56 | 2.56 |
| synth-zipf-high | 20256 | P1 | jp-random1 | 2837 | 2833 | 7.1 | 2.54 | 2.55 | 2.56 | 2.56 | 2.56 |
| synth-zipf-high | 20256 | P1 | jp-random2 | 2835 | 2833 | 7.1 | 2.51 | 2.53 | 2.53 | 2.54 | 2.54 |
| synth-zipf-high | 20256 | P1 | jp-random3 | 2835 | 2833 | 7.1 | 2.50 | 2.51 | 2.52 | 2.52 | 2.52 |
| synth-zipf-high | 20256 | P2 | levels | 1666 | 1637 | 12.2 | 3.12 | 3.20 | 3.23 | 3.25 | 3.25 |
| synth-zipf-high | 20256 | P2 | jp-random1 | 1680 | 1637 | 12.1 | 3.13 | 3.20 | 3.23 | 3.25 | 3.25 |
| synth-zipf-high | 20256 | P2 | jp-random2 | 1695 | 1637 | 11.9 | 3.09 | 3.17 | 3.20 | 3.22 | 3.22 |
| synth-zipf-high | 20256 | P2 | jp-random3 | 1679 | 1637 | 12.1 | 3.07 | 3.13 | 3.17 | 3.18 | 3.19 |
| synth-zipf-high | 20256 | P3 | levels | 1507 | 1499 | 13.4 | 3.09 | 3.28 | 3.38 | 3.43 | 3.46 |
| synth-zipf-high | 20256 | P3 | jp-random1 | 1506 | 1499 | 13.4 | 3.10 | 3.30 | 3.40 | 3.45 | 3.47 |
| synth-zipf-high | 20256 | P3 | jp-random2 | 1506 | 1499 | 13.4 | 3.09 | 3.27 | 3.37 | 3.41 | 3.45 |
| synth-zipf-high | 20256 | P3 | jp-random3 | 1503 | 1499 | 13.5 | 3.10 | 3.29 | 3.39 | 3.44 | 3.47 |
| synth-zipf-high | 40061 | P1 | levels | 5568 | 5559 | 7.2 | 2.52 | 2.53 | 2.53 | 2.53 | 2.53 |
| synth-zipf-high | 40061 | P1 | jp-random1 | 5569 | 5559 | 7.2 | 2.53 | 2.54 | 2.55 | 2.55 | 2.55 |
| synth-zipf-high | 40061 | P1 | jp-random2 | 5570 | 5559 | 7.2 | 2.54 | 2.55 | 2.56 | 2.56 | 2.56 |
| synth-zipf-high | 40061 | P1 | jp-random3 | 5564 | 5559 | 7.2 | 2.52 | 2.54 | 2.54 | 2.54 | 2.54 |
| synth-zipf-high | 40061 | P2 | levels | 3302 | 3222 | 12.1 | 3.14 | 3.19 | 3.21 | 3.22 | 3.22 |
| synth-zipf-high | 40061 | P2 | jp-random1 | 3361 | 3222 | 11.9 | 3.13 | 3.18 | 3.20 | 3.21 | 3.21 |
| synth-zipf-high | 40061 | P2 | jp-random2 | 3307 | 3222 | 12.1 | 3.13 | 3.19 | 3.21 | 3.22 | 3.22 |
| synth-zipf-high | 40061 | P2 | jp-random3 | 3374 | 3222 | 11.9 | 3.14 | 3.19 | 3.21 | 3.22 | 3.22 |
| synth-zipf-high | 40061 | P3 | levels | 2970 | 2955 | 13.5 | 3.12 | 3.28 | 3.36 | 3.40 | 3.42 |
| synth-zipf-high | 40061 | P3 | jp-random1 | 2967 | 2955 | 13.5 | 3.11 | 3.27 | 3.35 | 3.39 | 3.41 |
| synth-zipf-high | 40061 | P3 | jp-random2 | 2958 | 2955 | 13.5 | 3.13 | 3.29 | 3.37 | 3.41 | 3.43 |
| synth-zipf-high | 40061 | P3 | jp-random3 | 2978 | 2955 | 13.5 | 3.12 | 3.27 | 3.35 | 3.39 | 3.41 |
| synth-zipf-high | 60106 | P1 | levels | 8295 | 8278 | 7.2 | 2.50 | 2.51 | 2.51 | 2.51 | 2.51 |
| synth-zipf-high | 60106 | P1 | jp-random1 | 8292 | 8278 | 7.2 | 2.53 | 2.53 | 2.54 | 2.54 | 2.54 |
| synth-zipf-high | 60106 | P1 | jp-random2 | 8289 | 8278 | 7.3 | 2.53 | 2.54 | 2.54 | 2.54 | 2.54 |
| synth-zipf-high | 60106 | P1 | jp-random3 | 8293 | 8278 | 7.2 | 2.50 | 2.51 | 2.51 | 2.51 | 2.51 |
| synth-zipf-high | 60106 | P2 | levels | 5000 | 4865 | 12.0 | 3.11 | 3.15 | 3.16 | 3.17 | 3.17 |
| synth-zipf-high | 60106 | P2 | jp-random1 | 5042 | 4865 | 11.9 | 3.11 | 3.14 | 3.16 | 3.17 | 3.17 |
| synth-zipf-high | 60106 | P2 | jp-random2 | 4981 | 4865 | 12.1 | 3.13 | 3.17 | 3.19 | 3.19 | 3.20 |
| synth-zipf-high | 60106 | P2 | jp-random3 | 5031 | 4865 | 11.9 | 3.10 | 3.14 | 3.15 | 3.16 | 3.16 |
| synth-zipf-high | 60106 | P3 | levels | 4473 | 4455 | 13.4 | 3.10 | 3.23 | 3.30 | 3.33 | 3.35 |
| synth-zipf-high | 60106 | P3 | jp-random1 | 4478 | 4455 | 13.4 | 3.13 | 3.27 | 3.34 | 3.37 | 3.39 |
| synth-zipf-high | 60106 | P3 | jp-random2 | 4472 | 4455 | 13.4 | 3.11 | 3.25 | 3.32 | 3.35 | 3.37 |
| synth-zipf-high | 60106 | P3 | jp-random3 | 4477 | 4455 | 13.4 | 3.11 | 3.24 | 3.31 | 3.34 | 3.36 |
| synth-zipf-high | 80168 | P1 | levels | 11164 | 11151 | 7.2 | 2.50 | 2.50 | 2.50 | 2.50 | 2.50 |
| synth-zipf-high | 80168 | P1 | jp-random1 | 11163 | 11151 | 7.2 | 2.51 | 2.51 | 2.52 | 2.52 | 2.52 |
| synth-zipf-high | 80168 | P1 | jp-random2 | 11173 | 11151 | 7.2 | 2.51 | 2.52 | 2.52 | 2.52 | 2.52 |
| synth-zipf-high | 80168 | P1 | jp-random3 | 11163 | 11151 | 7.2 | 2.50 | 2.51 | 2.51 | 2.51 | 2.51 |
| synth-zipf-high | 80168 | P2 | levels | 6648 | 6407 | 12.1 | 3.12 | 3.15 | 3.16 | 3.17 | 3.17 |
| synth-zipf-high | 80168 | P2 | jp-random1 | 6711 | 6407 | 11.9 | 3.10 | 3.13 | 3.14 | 3.15 | 3.15 |
| synth-zipf-high | 80168 | P2 | jp-random2 | 6624 | 6407 | 12.1 | 3.11 | 3.14 | 3.15 | 3.16 | 3.16 |
| synth-zipf-high | 80168 | P2 | jp-random3 | 6641 | 6407 | 12.1 | 3.11 | 3.15 | 3.16 | 3.16 | 3.17 |
| synth-zipf-high | 80168 | P3 | levels | 6053 | 6030 | 13.2 | 3.09 | 3.21 | 3.27 | 3.30 | 3.32 |
| synth-zipf-high | 80168 | P3 | jp-random1 | 6044 | 6030 | 13.3 | 3.10 | 3.22 | 3.28 | 3.31 | 3.33 |
| synth-zipf-high | 80168 | P3 | jp-random2 | 6050 | 6030 | 13.3 | 3.11 | 3.23 | 3.29 | 3.31 | 3.34 |
| synth-zipf-high | 80168 | P3 | jp-random3 | 6045 | 6030 | 13.3 | 3.10 | 3.21 | 3.27 | 3.30 | 3.32 |
| synth-zipf-high | 100081 | P1 | levels | 13875 | 13864 | 7.2 | 2.50 | 2.51 | 2.51 | 2.51 | 2.51 |
| synth-zipf-high | 100081 | P1 | jp-random1 | 13881 | 13864 | 7.2 | 2.50 | 2.51 | 2.51 | 2.51 | 2.51 |
| synth-zipf-high | 100081 | P1 | jp-random2 | 13886 | 13864 | 7.2 | 2.50 | 2.50 | 2.50 | 2.50 | 2.50 |
| synth-zipf-high | 100081 | P1 | jp-random3 | 13891 | 13864 | 7.2 | 2.50 | 2.50 | 2.51 | 2.51 | 2.51 |
| synth-zipf-high | 100081 | P2 | levels | 8292 | 7971 | 12.1 | 3.12 | 3.14 | 3.15 | 3.16 | 3.16 |
| synth-zipf-high | 100081 | P2 | jp-random1 | 8280 | 7971 | 12.1 | 3.12 | 3.15 | 3.16 | 3.16 | 3.17 |
| synth-zipf-high | 100081 | P2 | jp-random2 | 8256 | 7971 | 12.1 | 3.13 | 3.16 | 3.17 | 3.17 | 3.18 |
| synth-zipf-high | 100081 | P2 | jp-random3 | 8310 | 7971 | 12.0 | 3.11 | 3.13 | 3.14 | 3.15 | 3.15 |
| synth-zipf-high | 100081 | P3 | levels | 7543 | 7516 | 13.3 | 3.11 | 3.22 | 3.27 | 3.29 | 3.31 |
| synth-zipf-high | 100081 | P3 | jp-random1 | 7543 | 7516 | 13.3 | 3.11 | 3.21 | 3.27 | 3.29 | 3.31 |
| synth-zipf-high | 100081 | P3 | jp-random2 | 7531 | 7516 | 13.3 | 3.13 | 3.24 | 3.29 | 3.32 | 3.34 |
| synth-zipf-high | 100081 | P3 | jp-random3 | 7530 | 7516 | 13.3 | 3.12 | 3.23 | 3.29 | 3.31 | 3.33 |
