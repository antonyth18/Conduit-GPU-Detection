# Phase 10 profiler pass

```
date: 2026-11-04T07:16:13Z  host: Nikhil-PC  super-block: 2026-09-01/full
name, memory.total [MiB], driver_version, clocks.max.sm [MHz]
NVIDIA GeForce RTX 4050 Laptop GPU, 6141 MiB, 610.91, 3105 MHz
Version 2024.3.2.0 (build 34861637) (public-release)
NVIDIA Nsight Systems version 2024.5.1.113-245134619542v0
```

Metrics from Nsight Compute (`--page raw`), one pipeline pass (warm-up 0, 1 run) on the largest super-block. Occupancy = achieved / theoretical active warps per SM; memory % = `gpu__compute_memory_throughput` of peak; warp efficiency = average active lanes per executed instruction ÷ 32 (1.00 = no divergence); branch uniformity = % of branches where all active threads take the same target. Time-weighted over a kernel's launches.

## Policy P1

| kernel | launches | time µs | share | occupancy % (ach / theo) | SM % | memory % | DRAM % | DRAM GB/s | warp eff. | branch unif. % | grid × block, regs |
|---|---:|---:|---:|---|---:|---:|---:|---:|---:|---:|---|
| `DeviceRadixSortOnesweepKernel` | 14 | 6809.6 | 56.3% | 72.7 / 75.0 | 19.2 | 45.4 | 45.4 | 58.1 | 0.97 | 84.6 | 1045 × 384, 55 |
| `k_edge_emit` | 1 | 861.4 | 7.1% | 94.0 / 100.0 | 23.4 | 35.6 | 35.6 | 45.5 | 0.99 | 96.0 | 12920 × 256, 20 |
| `k_extract_write` | 1 | 727.4 | 6.0% | 86.8 / 100.0 | 17.6 | 69.2 | 45.4 | 58.1 | 0.99 | 97.1 | 7864 × 256, 40 |
| `k_record_count` | 1 | 562.8 | 4.7% | 89.5 / 100.0 | 10.7 | 48.2 | 48.2 | 61.6 | 0.95 | 85.5 | 15671 × 256, 16 |
| `DeviceScanKernel` | 4 | 560.7 | 4.6% | 74.7 / 75.0 | 34.0 | 45.8 | 45.1 | 57.7 | 0.99 | 98.5 | 4478 × 128, 56 |
| `k_head_flags` | 1 | 468.4 | 3.9% | 87.9 / 100.0 | 15.5 | 47.2 | 47.2 | 60.3 | 1.00 | 100.0 | 15671 × 256, 16 |
| `k_extract_count` | 1 | 389.5 | 3.2% | 87.1 / 100.0 | 17.5 | 47.7 | 47.7 | 61.0 | 0.99 | 97.5 | 7864 × 256, 37 |
| `k_csr_rows` | 1 | 384.8 | 3.2% | 94.9 / 100.0 | 74.3 | 22.1 | 21.4 | 27.4 | 0.98 | 95.7 | 7864 × 256, 18 |
| `k_scatter_starts` | 1 | 364.6 | 3.0% | 90.1 / 100.0 | 13.1 | 47.9 | 47.9 | 61.3 | 1.00 | 100.0 | 15671 × 256, 19 |
| `DeviceRadixSortHistogramKernel` | 2 | 332.5 | 2.8% | 57.3 / 75.0 | 31.8 | 47.1 | 47.1 | 60.3 | 1.00 | 98.3 | 180 × 128, 55 |
| `DeviceSelectSweepKernel` | 1 | 227.0 | 1.9% | 81.7 / 83.3 | 20.1 | 45.5 | 45.5 | 58.2 | 0.99 | 97.9 | 5168 × 128, 44 |
| `k_edge_to_linear` | 1 | 211.1 | 1.7% | 83.7 / 100.0 | 11.6 | 45.5 | 45.5 | 58.2 | 1.00 | 100.0 | 12920 × 256, 16 |
| `k_csr_cols` | 1 | 152.8 | 1.3% | 86.0 / 100.0 | 55.1 | 48.1 | 48.1 | 61.5 | 1.00 | 100.0 | 11993 × 256, 18 |
| `k_group_max` | 1 | 18.2 | 0.2% | 82.3 / 100.0 | 42.4 | 40.4 | 40.4 | 51.5 | 1.00 | 100.0 | 2751 × 256, 16 |
| `DeviceScanInitKernel` | 4 | 7.9 | 0.1% | 16.2 / 100.0 | 0.7 | 2.7 | 0.2 | 0.3 | 1.00 | 0.0 | 35 × 128, 16 |
| `DeviceRadixSortExclusiveSumKernel` | 2 | 5.1 | 0.0% | 23.0 / 100.0 | 1.2 | 1.3 | 1.2 | 1.5 | 1.00 | 100.0 | 8 × 256, 23 |
| `DeviceCompactInitKernel` | 1 | 2.3 | 0.0% | 29.8 / 100.0 | 1.8 | 3.2 | 0.7 | 0.9 | 1.00 | 0.0 | 41 × 128, 16 |

## Policy P2

| kernel | launches | time µs | share | occupancy % (ach / theo) | SM % | memory % | DRAM % | DRAM GB/s | warp eff. | branch unif. % | grid × block, regs |
|---|---:|---:|---:|---|---:|---:|---:|---:|---:|---:|---|
| `DeviceRadixSortOnesweepKernel` | 14 | 15483.3 | 57.7% | 73.9 / 75.0 | 19.4 | 45.1 | 45.1 | 55.8 | 0.94 | 77.0 | 2285 × 384, 55 |
| `k_extract_write` | 1 | 2531.0 | 9.4% | 88.1 / 100.0 | 18.2 | 85.3 | 23.8 | 30.4 | 0.67 | 73.7 | 7864 × 256, 40 |
| `DeviceScanKernel` | 4 | 1408.0 | 5.2% | 73.7 / 75.0 | 26.9 | 45.5 | 45.3 | 58.0 | 0.99 | 98.5 | 9793 × 128, 56 |
| `k_edge_emit` | 1 | 1310.0 | 4.9% | 92.6 / 100.0 | 25.3 | 38.0 | 38.0 | 48.6 | 0.97 | 96.4 | 19702 × 256, 20 |
| `k_record_count` | 1 | 1124.5 | 4.2% | 85.2 / 100.0 | 10.8 | 47.0 | 47.0 | 60.1 | 0.92 | 81.9 | 34274 × 256, 16 |
| `k_head_flags` | 1 | 1115.1 | 4.2% | 85.4 / 100.0 | 14.2 | 46.1 | 46.1 | 59.0 | 1.00 | 100.0 | 34274 × 256, 16 |
| `k_extract_count` | 1 | 1042.8 | 3.9% | 85.1 / 100.0 | 29.3 | 85.3 | 22.1 | 28.3 | 0.64 | 74.7 | 7864 × 256, 37 |
| `k_scatter_starts` | 1 | 803.6 | 3.0% | 89.4 / 100.0 | 12.8 | 48.0 | 48.0 | 61.3 | 1.00 | 100.0 | 34274 × 256, 19 |
| `DeviceRadixSortHistogramKernel` | 2 | 612.3 | 2.3% | 57.1 / 75.0 | 32.4 | 47.8 | 47.8 | 61.1 | 1.00 | 98.1 | 180 × 128, 55 |
| `k_csr_rows` | 1 | 389.5 | 1.5% | 96.0 / 100.0 | 74.1 | 25.6 | 25.6 | 32.8 | 0.99 | 95.8 | 7864 × 256, 18 |
| `k_edge_to_linear` | 1 | 372.5 | 1.4% | 81.4 / 100.0 | 10.0 | 45.9 | 45.9 | 58.7 | 1.00 | 100.0 | 19702 × 256, 16 |
| `DeviceSelectSweepKernel` | 1 | 355.2 | 1.3% | 83.4 / 83.3 | 19.4 | 45.1 | 45.1 | 57.7 | 0.97 | 97.9 | 7881 × 128, 44 |
| `k_csr_cols` | 1 | 205.4 | 0.8% | 85.0 / 100.0 | 51.9 | 48.2 | 48.2 | 61.6 | 1.00 | 100.0 | 15317 × 256, 18 |
| `k_group_max` | 1 | 54.2 | 0.2% | 79.0 / 100.0 | 46.3 | 45.8 | 45.8 | 58.6 | 1.00 | 100.0 | 9298 × 256, 16 |
| `DeviceScanInitKernel` | 4 | 8.0 | 0.0% | 27.6 / 100.0 | 1.5 | 5.0 | 0.2 | 0.3 | 1.00 | 0.0 | 77 × 128, 16 |
| `DeviceRadixSortExclusiveSumKernel` | 2 | 5.2 | 0.0% | 24.1 / 100.0 | 1.0 | 1.3 | 1.0 | 1.3 | 1.00 | 100.0 | 8 × 256, 23 |
| `DeviceCompactInitKernel` | 1 | 2.3 | 0.0% | 43.1 / 100.0 | 2.7 | 4.6 | 0.7 | 0.9 | 1.00 | 0.0 | 62 × 128, 16 |

## Policy P3

| kernel | launches | time µs | share | occupancy % (ach / theo) | SM % | memory % | DRAM % | DRAM GB/s | warp eff. | branch unif. % | grid × block, regs |
|---|---:|---:|---:|---|---:|---:|---:|---:|---:|---:|---|
| `DeviceRadixSortOnesweepKernel` | 14 | 13739.7 | 58.2% | 73.8 / 75.0 | 19.4 | 44.9 | 44.9 | 57.4 | 0.94 | 77.4 | 2285 × 384, 55 |
| `k_extract_write` | 1 | 2529.9 | 10.7% | 87.5 / 100.0 | 18.2 | 85.3 | 23.8 | 30.4 | 0.67 | 73.7 | 7864 × 256, 40 |
| `DeviceScanKernel` | 4 | 1406.5 | 6.0% | 74.3 / 75.0 | 26.6 | 45.8 | 45.6 | 58.3 | 0.99 | 98.4 | 9793 × 128, 56 |
| `k_head_flags` | 1 | 1119.1 | 4.7% | 83.5 / 100.0 | 14.2 | 46.4 | 46.4 | 59.3 | 1.00 | 100.0 | 34274 × 256, 16 |
| `k_extract_count` | 1 | 1041.9 | 4.4% | 84.2 / 100.0 | 29.3 | 85.3 | 22.2 | 28.4 | 0.64 | 74.7 | 7864 × 256, 37 |
| `k_record_count` | 1 | 890.7 | 3.8% | 87.3 / 100.0 | 12.1 | 47.0 | 47.0 | 60.1 | 0.94 | 89.2 | 34274 × 256, 16 |
| `k_scatter_starts` | 1 | 737.4 | 3.1% | 88.3 / 100.0 | 14.0 | 47.9 | 47.9 | 61.2 | 1.00 | 100.0 | 34274 × 256, 19 |
| `k_edge_emit` | 1 | 708.4 | 3.0% | 93.1 / 100.0 | 25.6 | 36.0 | 36.0 | 46.1 | 0.97 | 96.5 | 10831 × 256, 20 |
| `DeviceRadixSortHistogramKernel` | 2 | 509.3 | 2.2% | 57.1 / 75.0 | 33.5 | 47.7 | 47.7 | 61.0 | 1.00 | 97.7 | 180 × 128, 55 |
| `k_csr_rows` | 1 | 365.1 | 1.5% | 95.6 / 100.0 | 78.8 | 23.6 | 18.1 | 23.2 | 0.97 | 95.8 | 7864 × 256, 18 |
| `DeviceSelectSweepKernel` | 1 | 182.5 | 0.8% | 81.7 / 83.3 | 21.3 | 45.2 | 45.2 | 57.7 | 0.99 | 98.0 | 4333 × 128, 44 |
| `k_edge_to_linear` | 1 | 167.5 | 0.7% | 86.3 / 100.0 | 12.2 | 45.9 | 45.9 | 58.7 | 1.00 | 100.0 | 10831 × 256, 16 |
| `k_csr_cols` | 1 | 127.5 | 0.5% | 87.7 / 100.0 | 56.9 | 48.0 | 48.0 | 61.4 | 1.00 | 100.0 | 10208 × 256, 18 |
| `k_group_max` | 1 | 54.4 | 0.2% | 79.4 / 100.0 | 46.4 | 45.7 | 45.7 | 58.4 | 1.00 | 100.0 | 9298 × 256, 16 |
| `DeviceScanInitKernel` | 4 | 8.0 | 0.0% | 27.7 / 100.0 | 1.5 | 5.0 | 0.2 | 0.3 | 1.00 | 0.0 | 77 × 128, 16 |
| `DeviceRadixSortExclusiveSumKernel` | 2 | 5.0 | 0.0% | 23.7 / 100.0 | 1.3 | 1.3 | 1.1 | 1.3 | 1.00 | 100.0 | 8 × 256, 23 |
| `DeviceCompactInitKernel` | 1 | 2.3 | 0.0% | 26.7 / 100.0 | 1.5 | 2.7 | 0.7 | 0.9 | 1.00 | 0.0 | 34 × 128, 16 |

## Nsight Systems CUDA API summary (host side): nsys_P1_cuda_api_sum.csv

```
Time (%),Total Time (ns),Num Calls,Avg (ns),Med (ns),Min (ns),Max (ns),StdDev (ns),Name
50.1,217837952,1,217837952.0,217837952.0,217837952,217837952,0.0,cudaStreamCreateWithFlags
21.5,93310297,5,18662059.4,10084421.0,1800065,60490434,23794905.4,cudaHostAlloc
16.8,73103287,24,3045970.3,1619180.0,47226,8691150,3340373.2,cudaStreamSynchronize
3.1,13535983,152,89052.5,14194.5,2777,8994010,728110.5,cudaLaunchKernel
3.1,13460070,25,538402.8,494934.0,1743,1308800,354143.7,cudaMalloc
2.8,12080458,4,3020114.5,2924992.0,2839277,3391197,252662.3,cudaEventSynchronize
0.8,3433146,88,39013.0,12818.0,2820,1638470,175353.7,cudaMemsetAsync
0.7,2981845,1,2981845.0,2981845.0,2981845,2981845,0.0,cudaFree
0.6,2633950,32,82310.9,13418.5,3213,1960931,343898.9,cudaEventRecord
0.5,2063046,40,51576.2,32643.5,5031,695279,107451.7,cudaMemcpyAsync
0.0,12495,10,1249.5,316.5,275,5844,1841.5,cudaEventCreate
0.0,844,1,844.0,844.0,844,844,0.0,cuModuleGetLoadingMode
```

## Nsight Systems CUDA API summary (host side): nsys_P2_cuda_api_sum.csv

```
Time (%),Total Time (ns),Num Calls,Avg (ns),Med (ns),Min (ns),Max (ns),StdDev (ns),Name
36.0,136071502,24,5669645.9,3614049.0,38130,19855964,6729739.6,cudaStreamSynchronize
31.2,118053584,1,118053584.0,118053584.0,118053584,118053584,0.0,cudaStreamCreateWithFlags
19.3,72846085,5,14569217.0,11913847.0,1110358,41215398,15586603.6,cudaHostAlloc
3.4,12801072,4,3200268.0,3110134.5,2954359,3626444,299159.5,cudaEventSynchronize
3.2,12125218,25,485008.7,418351.0,1543,1169406,305054.2,cudaMalloc
3.1,11782228,152,77514.7,11205.0,2741,8094977,655597.2,cudaLaunchKernel
2.0,7722095,1,7722095.0,7722095.0,7722095,7722095,0.0,cudaFree
0.6,2385179,32,74536.8,10574.5,2337,1693785,297930.3,cudaEventRecord
0.6,2166314,88,24617.2,9752.5,2860,1082943,114626.3,cudaMemsetAsync
0.5,1952136,40,48803.4,33056.0,5183,591174,91752.2,cudaMemcpyAsync
0.0,38746,10,3874.6,337.5,275,30874,9565.0,cudaEventCreate
0.0,715,1,715.0,715.0,715,715,0.0,cuModuleGetLoadingMode
```

## Nsight Systems CUDA API summary (host side): nsys_P3_cuda_api_sum.csv

```
Time (%),Total Time (ns),Num Calls,Avg (ns),Med (ns),Min (ns),Max (ns),StdDev (ns),Name
36.6,133471842,1,133471842.0,133471842.0,133471842,133471842,0.0,cudaStreamCreateWithFlags
33.3,121373880,24,5057245.0,2012768.5,8399,19809993,6830978.8,cudaStreamSynchronize
16.4,59672604,5,11934520.8,5843419.0,1725205,38676477,15125810.7,cudaHostAlloc
3.6,12984970,25,519398.8,408243.0,1564,1323915,357023.7,cudaMalloc
3.4,12523966,152,82394.5,12354.5,3740,8146129,660080.4,cudaLaunchKernel
2.7,9890558,4,2472639.5,2397807.5,2161486,2933457,326982.6,cudaEventSynchronize
2.1,7511576,1,7511576.0,7511576.0,7511576,7511576,0.0,cudaFree
0.8,2839426,88,32266.2,11744.0,3046,1541990,163438.2,cudaMemsetAsync
0.6,2296136,32,71754.3,12427.5,2608,1660314,290921.0,cudaEventRecord
0.6,2233515,40,55837.9,28190.0,4042,609739,100583.5,cudaMemcpyAsync
0.0,41352,10,4135.2,295.5,275,33677,10455.9,cudaEventCreate
0.0,905,1,905.0,905.0,905,905,0.0,cuModuleGetLoadingMode
```

## Nsight Systems: no GPU activity recorded

These Nsight Systems reports are empty: `nsys_P1_cuda_gpu_kern_sum.csv`, `nsys_P2_cuda_gpu_kern_sum.csv`, `nsys_P3_cuda_gpu_kern_sum.csv`, `nsys_P1_cuda_gpu_mem_time_sum.csv`, `nsys_P2_cuda_gpu_mem_time_sum.csv`, `nsys_P3_cuda_gpu_mem_time_sum.csv`. The capture contains host-side CUDA API calls but no GPU kernel or memcpy records (no `CUPTI_ACTIVITY_KIND_KERNEL` table), which happens when Nsight Systems cannot trace GPU activity under WSL2. The per-kernel times above (Nsight Compute) and the cudaEvent stage timings in `results/phase8/<host>-real-large/` cover the same ground.

