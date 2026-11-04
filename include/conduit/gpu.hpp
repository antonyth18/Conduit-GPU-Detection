// Phase 8-10: GPU pipeline (CUDA) and its CPU emulation.
//
// Two backends run the SAME per-thread kernel bodies (src/gpu/kernels.hpp):
//   cuda   real kernels on the device; CUB radix sort / scan / unique.
//   emu    every kernel body executed sequentially on the host, phase by
//          phase, with the barriers in the same places. It needs no GPU and
//          is the reference the CUDA backend is checked against when no GPU is
//          present (and a correctness check of the kernel logic itself).
//
// Stages (same boundaries as pipeline.hpp):
//   1 extract   one thread per tx: count accesses, scan, write records
//   2 sort      stable radix sort of records by key (records are generated
//               in tx order, so a stable key sort gives the full 128-bit order)
//   2 detect    group heads by flag + scan, then one of three thread mappings
//               (Phase 9 measures all three on the same input):
//                 group    one thread per key group walks walk_group (naive;
//                          a hot key serialises onto one thread)  [Phase 9 "before"]
//                 record   one thread per record writes its in-edges (the
//                          previous run); per-record work = that run's length
//                 balanced one thread per OUTPUT EDGE, located by binary search
//                          over per-record edge offsets (load-balanced search,
//                          Merrill & Garland 2016)                  [Phase 9 "after"]
//   3 csr       radix sort of u*n+v, unique, row offsets by binary search
//   4 level     sweep   CSR copied back, sequential sweep on the CPU (default,
//                       as the handover asks)
//               kahn    GPU frontier Kahn on the reduced DAG (levels)
//               jp-index  Jones-Plassmann rounds on the IMPLICIT conflict
//                       graph (key groups, no edges needed), priority = block
//                       position: round(v) = longest path = level(v). Order-
//                       preserving; must equal the sweep exactly.
//               jp-random Jones-Plassmann with hashed priorities: every round
//                       is an independent set of the full conflict graph, so
//                       batches are conflict-free but REORDER transactions
//                       (valid for a block builder, not for replaying a fixed
//                       block order). Result::levels then holds batch ids.
// Rounds are device-driven: sizes and the round number live in device memory,
// so large frontiers run as multi-block kernels enqueued GpuOptions::chunk
// rounds at a time with one host check per chunk (a finished round makes the
// remaining kernels of the chunk no-ops). Once a frontier / active set is at
// most GpuOptions::tail_threshold, a single persistent block runs the
// remaining rounds with __syncthreads instead of kernel launches.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "conduit/pipeline.hpp"

namespace conduit {

enum class GpuBackend { Cuda, Emu };
enum class DetectMode { Group, Record, Balanced };
enum class GpuLevel { Sweep, Kahn, JpIndex, JpRandom };

// Intermediate outputs, for verifying the port stage by stage (Phase 8):
// records as extracted (block order), records after the sort, and the raw
// edge list as detection emitted it (before stage 3 sorts and deduplicates).
struct GpuCapture {
    std::vector<Record> extracted;
    std::vector<Record> sorted;
    std::vector<uint64_t> edges;
};

struct GpuOptions {
    GpuBackend backend = GpuBackend::Cuda;
    DetectMode detect = DetectMode::Balanced;
    GpuLevel level = GpuLevel::Sweep;
    uint32_t jp_seed = 0x9E3779B9u;
    uint32_t tail_threshold = 8192;   // <= this: single persistent block
    int tail_block = 1024;            // threads in the persistent block
    uint32_t chunk = 32;              // multi-block rounds enqueued per host check
    bool pinned = true;               // stage H2D/D2H through pinned buffers
    GpuCapture* capture = nullptr;    // if set, copy intermediates back (untimed use only)
};

// Seconds, like Timings. For cuda the stage times are cudaEvent intervals on
// one stream; wall is host steady_clock around the whole call (after the
// first call has allocated the buffers), so wall >= h2d + stages + d2h.
struct GpuTimings {
    double h2d = 0, extract = 0, sort = 0, detect = 0, csr = 0, d2h = 0, level = 0;
    double wall = 0;
    // host-side work inside wall but outside the device timeline:
    //   host_prep  id check + copy of the input into the pinned staging buffer
    //   host_post  copy of the results out of the pinned buffers into the
    //              returned vectors (and their allocation)
    // wall - (h2d + kernels + d2h) - host_prep - host_post = launch / sync gaps
    double host_prep = 0, host_post = 0;
    double kernels() const { return extract + sort + detect + csr + level; }
    double with_transfer() const { return h2d + kernels() + d2h; }
    uint64_t launches = 0;           // kernel launches (CUB calls count as 1)
    uint64_t level_rounds = 0;       // rounds of kahn / jp
    uint64_t tail_rounds = 0;        // of which run inside the persistent block
    uint64_t host_syncs = 0;         // device->host reads of a size/counter
    uint32_t max_group = 0;          // largest key group (records)
    uint64_t device_bytes = 0;       // device memory held by the context
};

GpuBackend parse_gpu_backend(const std::string& s);
DetectMode parse_detect(const std::string& s);
GpuLevel parse_gpu_level(const std::string& s);
const char* gpu_backend_name(GpuBackend b);
const char* detect_name(DetectMode d);
const char* gpu_level_name(GpuLevel l);

// True when built with CUDA and at least one device is visible.
bool have_cuda();
// One line: device name, SMs, memory, compute capability, driver/runtime.
std::string cuda_device_info();

// PCIe / launch microbenchmark (Phase 8 transfer-cost arithmetic). Seconds per
// transfer, median of reps, cudaEvent-timed; launch = empty kernel launch +
// synchronise; sync = a 4-byte device->host read (one host round trip).
struct PcieSample {
    size_t bytes = 0;
    double h2d_pinned = 0, d2h_pinned = 0, h2d_pageable = 0, d2h_pageable = 0;
};
struct PcieProbe {
    std::vector<PcieSample> samples;
    double launch = 0, sync = 0;
};
PcieProbe pcie_probe(const std::vector<size_t>& sizes, int reps);

// Result::levels holds levels for sweep / kahn / jp-index and batch ids for
// jp-random; Result::level_stats.rounds = number of batches for kahn / jp.
Result run_pipeline_gpu(const Tx* tx, size_t n, Policy policy, const Selectors& sels,
                        const GpuOptions& opts, GpuTimings* timings = nullptr);

namespace gpu_detail {
Result run_emu(const Tx* tx, size_t n, Policy policy, const Selectors& sels,
               const GpuOptions& opts, GpuTimings* timings);
Result run_cuda(const Tx* tx, size_t n, Policy policy, const Selectors& sels,
                const GpuOptions& opts, GpuTimings* timings);
}  // namespace gpu_detail

}  // namespace conduit
