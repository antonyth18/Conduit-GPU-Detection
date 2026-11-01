// Phase 8: CUDA implementation of the four-stage pipeline.
//
// All kernels are thin grid-stride (or block-stride) wrappers around the
// bodies in kernels.hpp, which the host emulation (emu_pipeline.cpp) runs
// too. Library primitives come from CUB (ships with the CUDA toolkit):
// DeviceRadixSort, DeviceScan, DeviceSelect::Unique.
//
// Buffers live in a process-wide context and only grow, so after a warm-up
// call no cudaMalloc happens inside a timed run. One stream; stage times are
// cudaEvent intervals on it, so they include any GPU idle time spent waiting
// for the host to read back a size (that is part of the real cost).
#include <cuda_runtime.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cub/cub.cuh>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "conduit/gpu.hpp"
#include "kernels.hpp"

namespace conduit {

namespace {

#define CDX_CUDA(call)                                                                   \
    do {                                                                                 \
        cudaError_t e_ = (call);                                                         \
        if (e_ != cudaSuccess)                                                           \
            throw std::runtime_error(std::string("CUDA error: ") + cudaGetErrorString(e_) + \
                                     " at " #call);                                      \
    } while (0)

#define CDX_KCHECK() CDX_CUDA(cudaGetLastError())

#define GRID_STRIDE(i, N)                                                              \
    for (uint64_t i = uint64_t(blockIdx.x) * blockDim.x + threadIdx.x; i < uint64_t(N); \
         i += uint64_t(gridDim.x) * blockDim.x)

constexpr int BLOCK = 256;

unsigned grid_for(uint64_t n) {
    uint64_t g = (n + BLOCK - 1) / BLOCK;
    if (g < 1) g = 1;
    if (g > 65535ull * 16) g = 65535ull * 16;
    return unsigned(g);
}

template <class T>
struct DBuf {
    T* p = nullptr;
    size_t cap = 0;
    void ensure(size_t k) {
        if (k <= cap && p) return;
        if (p) cudaFree(p);
        p = nullptr;
        const size_t c = std::max<size_t>(k + k / 4, 1024);
        CDX_CUDA(cudaMalloc(&p, c * sizeof(T)));
        cap = c;
    }
    size_t bytes() const { return cap * sizeof(T); }
    ~DBuf() {
        if (p) cudaFree(p);
    }
};

template <class T>
struct HBuf {   // pinned host buffer
    T* p = nullptr;
    size_t cap = 0;
    void ensure(size_t k) {
        if (k <= cap && p) return;
        if (p) cudaFreeHost(p);
        p = nullptr;
        const size_t c = std::max<size_t>(k + k / 4, 1024);
        CDX_CUDA(cudaMallocHost(&p, c * sizeof(T)));
        cap = c;
    }
    ~HBuf() {
        if (p) cudaFreeHost(p);
    }
};

// Round state of Kahn / JP, resident on the device so rounds can be enqueued
// without reading sizes back. Round r reads buffer [r & 1], writes [(r+1) & 1].
struct RoundState {
    uint32_t size;       // kahn: frontier size; jp: active txs
    uint32_t nrec;       // jp: active records
    uint32_t next;       // counter for the next frontier / active txs
    uint32_t next_rec;   // jp: counter for the next active records
    uint32_t round;
    uint32_t pad[3];
};

// ---- kernels ------------------------------------------------------------------

__global__ void k_extract_count(const Tx* tx, uint32_t n, int pol, gk::DevSelectors s,
                                uint32_t* counts) {
    GRID_STRIDE(i, n) gk::extract_count(uint32_t(i), tx, pol, s, counts);
}

__global__ void k_extract_write(const Tx* tx, uint32_t n, int pol, gk::DevSelectors s,
                                const uint32_t* off, uint64_t* hi, uint64_t* lo) {
    GRID_STRIDE(i, n) gk::extract_write(uint32_t(i), tx, pol, s, off, hi, lo);
}

__global__ void k_head_flags(uint32_t R, const uint64_t* hi, const uint64_t* lo, uint32_t* gf,
                             uint32_t* rf) {
    GRID_STRIDE(i, R) gk::head_flags(uint32_t(i), hi, lo, gf, rf);
}

__global__ void k_scatter_starts(uint32_t R, const uint32_t* gf, const uint32_t* rf,
                                 const uint32_t* gs, const uint32_t* rs, uint32_t* gstart,
                                 uint32_t* rstart) {
    GRID_STRIDE(i, R) gk::scatter_starts(uint32_t(i), gf, rf, gs, rs, gstart, rstart);
    if (blockIdx.x == 0 && threadIdx.x == 0) {
        // sentinels: gstart[G] = rstart[NR] = R (G, NR = last inclusive scans)
        if (R) {
            gstart[gs[R - 1]] = R;
            rstart[rs[R - 1]] = R;
        } else {
            gstart[0] = 0;
            rstart[0] = 0;
        }
    }
}

__global__ void k_group_max(uint32_t G, const uint32_t* gstart, uint32_t* out) {
    GRID_STRIDE(g, G) gk::group_size_max(uint32_t(g), gstart, out);
}

__global__ void k_group_count(uint32_t G, const uint64_t* lo, const uint32_t* gstart,
                              uint64_t* gcount, uint64_t* rc) {
    GRID_STRIDE(g, G) gk::group_count(uint32_t(g), lo, gstart, gcount, rc);
}

__global__ void k_group_emit(uint32_t G, const uint64_t* lo, const uint32_t* gstart,
                             const uint64_t* goff, uint64_t* edges) {
    GRID_STRIDE(g, G) gk::group_emit(uint32_t(g), lo, gstart, goff, edges);
}

__global__ void k_record_count(uint32_t R, const uint64_t* lo, const uint32_t* gf,
                               const uint32_t* rs, const uint32_t* rstart, uint64_t* ecount,
                               uint64_t* rc) {
    GRID_STRIDE(i, R) gk::record_count(uint32_t(i), lo, gf, rs, rstart, ecount, rc);
}

__global__ void k_edge_emit(uint64_t E, uint32_t R, const uint64_t* eoff, const uint64_t* lo,
                            const uint32_t* rs, const uint32_t* rstart, uint64_t* edges) {
    GRID_STRIDE(e, E) gk::edge_emit(e, R, eoff, lo, rs, rstart, edges);
}

__global__ void k_edge_to_linear(uint64_t E, uint32_t n, const uint64_t* edges, uint64_t* lin) {
    GRID_STRIDE(e, E) gk::edge_to_linear(e, n, edges, lin);
}

__global__ void k_csr_cols(uint64_t M, uint32_t n, const uint64_t* lin, uint32_t* cols) {
    GRID_STRIDE(e, M) gk::csr_cols(e, n, lin, cols);
}

__global__ void k_csr_rows(uint32_t n, uint64_t M, const uint64_t* lin, uint64_t* row) {
    GRID_STRIDE(u, uint64_t(n) + 1) gk::csr_rows(uint32_t(u), n, M, lin, row);
}

__global__ void k_kahn_indeg(uint64_t M, const uint32_t* cols, uint32_t* indeg) {
    GRID_STRIDE(e, M) gk::kahn_indeg(e, cols, indeg);
}

__global__ void k_kahn_init(uint32_t n, const uint32_t* indeg, uint32_t* level, uint32_t* fr,
                            uint32_t* count) {
    GRID_STRIDE(v, n) gk::kahn_init(uint32_t(v), indeg, level, fr, count);
}

__global__ void k_kahn_round(RoundState* st, uint32_t* f0, uint32_t* f1, const uint64_t* row,
                             const uint32_t* cols, uint32_t* indeg, uint32_t* level) {
    const uint32_t size = st->size;
    if (size == 0) return;
    const uint32_t r = st->round;
    const uint32_t* cur = (r & 1) ? f1 : f0;
    uint32_t* nxt = (r & 1) ? f0 : f1;
    GRID_STRIDE(i, size)
    gk::kahn_expand(uint32_t(i), cur, row, cols, indeg, level, r + 1, nxt, &st->next);
}

// one thread: close the round (no-op once finished, so a chunk can overshoot)
__global__ void k_round_advance(RoundState* st) {
    if (st->size == 0) return;
    st->size = st->next;
    st->nrec = st->next_rec;
    st->next = 0;
    st->next_rec = 0;
    ++st->round;
}

// Persistent single block: rounds separated by __syncthreads, no launches.
__global__ void k_kahn_tail(RoundState* st, uint32_t* f0, uint32_t* f1, uint32_t T,
                            const uint64_t* row, const uint32_t* cols, uint32_t* indeg,
                            uint32_t* level) {
    __shared__ uint32_t s_next;
    uint32_t size = st->size, r = st->round;
    for (;;) {
        if (threadIdx.x == 0) s_next = 0;
        __syncthreads();
        const uint32_t* cur = (r & 1) ? f1 : f0;
        uint32_t* nxt = (r & 1) ? f0 : f1;
        for (uint32_t i = threadIdx.x; i < size; i += blockDim.x)
            gk::kahn_expand(i, cur, row, cols, indeg, level, r + 1, nxt, &s_next);
        __syncthreads();
        size = s_next;
        ++r;
        __syncthreads();
        if (size == 0 || size > T) break;
    }
    if (threadIdx.x == 0) {
        st->size = size;
        st->round = r;
        st->next = 0;
        st->next_rec = 0;
    }
}

__global__ void k_iota(uint32_t n, uint32_t* out) {
    GRID_STRIDE(i, n) out[i] = uint32_t(i);
}

__global__ void k_jp_unpack(uint32_t R, const uint32_t* gs, const uint64_t* lo, uint32_t* gid,
                            uint32_t* tx, uint8_t* mode) {
    GRID_STRIDE(i, R) gk::jp_unpack(uint32_t(i), gs, lo, gid, tx, mode);
}

__global__ void k_jp_a(const RoundState* st, const uint32_t* ar0, const uint32_t* ar1,
                       gk::JpArgs a) {
    if (st->size == 0) return;
    const uint32_t r = st->round, nrec = st->nrec;
    const uint32_t* car = (r & 1) ? ar1 : ar0;
    GRID_STRIDE(j, nrec) gk::jp_phase_a(uint32_t(j), car, a, r);
}

__global__ void k_jp_b(const RoundState* st, const uint32_t* ar0, const uint32_t* ar1,
                       gk::JpArgs a) {
    if (st->size == 0) return;
    const uint32_t r = st->round, nrec = st->nrec;
    const uint32_t* car = (r & 1) ? ar1 : ar0;
    GRID_STRIDE(j, nrec) gk::jp_phase_b(uint32_t(j), car, a, r);
}

__global__ void k_jp_c(RoundState* st, uint32_t* at0, uint32_t* at1, uint32_t* ar0,
                       uint32_t* ar1, gk::JpArgs a) {
    const uint32_t ntx = st->size;
    if (ntx == 0) return;
    const uint32_t r = st->round, nrec = st->nrec;
    const uint32_t* cat = (r & 1) ? at1 : at0;
    uint32_t* nat = (r & 1) ? at0 : at1;
    const uint32_t* car = (r & 1) ? ar1 : ar0;
    uint32_t* nar = (r & 1) ? ar0 : ar1;
    const uint32_t span = ntx > nrec ? ntx : nrec;
    GRID_STRIDE(j, span)
    gk::jp_phase_c(uint32_t(j), cat, ntx, car, nrec, a, r, nat, &st->next, nar, &st->next_rec);
}

__global__ void k_jp_tail(RoundState* st, uint32_t* at0, uint32_t* at1, uint32_t* ar0,
                          uint32_t* ar1, gk::JpArgs a, uint32_t T) {
    __shared__ uint32_t s_nt, s_nr;
    uint32_t ntx = st->size, nrec = st->nrec, r = st->round;
    for (;;) {
        if (threadIdx.x == 0) {
            s_nt = 0;
            s_nr = 0;
        }
        const uint32_t* cat = (r & 1) ? at1 : at0;
        uint32_t* nat = (r & 1) ? at0 : at1;
        const uint32_t* car = (r & 1) ? ar1 : ar0;
        uint32_t* nar = (r & 1) ? ar0 : ar1;
        for (uint32_t j = threadIdx.x; j < nrec; j += blockDim.x) gk::jp_phase_a(j, car, a, r);
        __syncthreads();
        for (uint32_t j = threadIdx.x; j < nrec; j += blockDim.x) gk::jp_phase_b(j, car, a, r);
        __syncthreads();
        const uint32_t span = ntx > nrec ? ntx : nrec;
        for (uint32_t j = threadIdx.x; j < span; j += blockDim.x)
            gk::jp_phase_c(j, cat, ntx, car, nrec, a, r, nat, &s_nt, nar, &s_nr);
        __syncthreads();
        ntx = s_nt;
        nrec = s_nr;
        ++r;
        __syncthreads();
        if (ntx == 0 || (ntx > nrec ? ntx : nrec) > T) break;
    }
    if (threadIdx.x == 0) {
        st->size = ntx;
        st->nrec = nrec;
        st->round = r;
        st->next = 0;
        st->next_rec = 0;
    }
}

// ---- context -----------------------------------------------------------------

struct Ctx {
    cudaStream_t stream = nullptr;
    cudaEvent_t ev[10];
    DBuf<Tx> tx;
    DBuf<uint32_t> counts, off;
    DBuf<uint64_t> hi, lo, hi2, lo2;
    DBuf<uint32_t> gflag, rflag, gscan, rscan, gstart, rstart;
    DBuf<uint64_t> gcount, goff, ecount, eoff, edges, lin, lin2, row;
    DBuf<uint32_t> cols, indeg, level, fa, fb;
    DBuf<uint32_t> r_gid, r_tx, at, at2, ar, ar2, fail, color;
    DBuf<uint8_t> r_mode;
    DBuf<uint64_t> maxA, maxW, maxR, maxC;
    DBuf<uint64_t> scalars64;   // [0] read_comm
    DBuf<uint32_t> scalars32;   // [0] max_group [1] counter [2] counter2 [3] unique count
    DBuf<RoundState> rstate;
    unsigned dgrid = 0;   // grid of the device-driven round kernels
    DBuf<uint8_t> temp;
    HBuf<Tx> h_tx;
    HBuf<uint64_t> h_row;
    HBuf<uint32_t> h_cols, h_lv;
    HBuf<uint64_t> h_small;
    uint64_t launches = 0;
    uint64_t syncs = 0;

    Ctx() {
        CDX_CUDA(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));
        for (auto& e : ev) CDX_CUDA(cudaEventCreate(&e));
        scalars64.ensure(8);
        scalars32.ensure(8);
        rstate.ensure(1);
        int dev = 0, sms = 0;
        CDX_CUDA(cudaGetDevice(&dev));
        CDX_CUDA(cudaDeviceGetAttribute(&sms, cudaDevAttrMultiProcessorCount, dev));
        dgrid = unsigned(std::max(1, sms) * 8);
        h_small.ensure(16);
    }
    uint64_t device_bytes() const {
        uint64_t b = 0;
        b += tx.bytes() + counts.bytes() + off.bytes() + hi.bytes() + lo.bytes() + hi2.bytes() +
             lo2.bytes() + gflag.bytes() + rflag.bytes() + gscan.bytes() + rscan.bytes() +
             gstart.bytes() + rstart.bytes() + gcount.bytes() + goff.bytes() + ecount.bytes() +
             eoff.bytes() + edges.bytes() + lin.bytes() + lin2.bytes() + row.bytes() +
             cols.bytes() + indeg.bytes() + level.bytes() + fa.bytes() + fb.bytes() +
             r_gid.bytes() + r_tx.bytes() + at.bytes() + at2.bytes() + ar.bytes() + ar2.bytes() +
             fail.bytes() + color.bytes() + r_mode.bytes() + maxA.bytes() + maxW.bytes() +
             maxR.bytes() + maxC.bytes() + temp.bytes();
        return b;
    }
    // read one value back (synchronises the stream)
    template <class T>
    T read(const T* dptr) {
        T v;
        CDX_CUDA(cudaMemcpyAsync(h_small.p, dptr, sizeof(T), cudaMemcpyDeviceToHost, stream));
        CDX_CUDA(cudaStreamSynchronize(stream));
        std::memcpy(&v, h_small.p, sizeof(T));
        ++syncs;
        return v;
    }
};

Ctx& ctx() {
    static Ctx* c = new Ctx();   // leaked on purpose: no CUDA calls during static teardown
    return *c;
}

void check_int(uint64_t k) {
    if (k > 0x7FFFFFFFull) throw std::runtime_error("CUB call over 2^31 items");
}

template <class T>
void ex_sum(Ctx& c, const T* in, T* out, uint64_t num) {
    check_int(num);
    size_t bytes = 0;
    CDX_CUDA(cub::DeviceScan::ExclusiveSum(nullptr, bytes, in, out, int(num), c.stream));
    c.temp.ensure(bytes);
    CDX_CUDA(cub::DeviceScan::ExclusiveSum(c.temp.p, bytes, in, out, int(num), c.stream));
    ++c.launches;
}

template <class T>
void in_sum(Ctx& c, const T* in, T* out, uint64_t num) {
    check_int(num);
    size_t bytes = 0;
    CDX_CUDA(cub::DeviceScan::InclusiveSum(nullptr, bytes, in, out, int(num), c.stream));
    c.temp.ensure(bytes);
    CDX_CUDA(cub::DeviceScan::InclusiveSum(c.temp.p, bytes, in, out, int(num), c.stream));
    ++c.launches;
}

double ms_between(cudaEvent_t a, cudaEvent_t b) {
    float ms = 0;
    CDX_CUDA(cudaEventElapsedTime(&ms, a, b));
    return double(ms) * 1e-3;
}

}  // namespace

bool have_cuda() {
    int count = 0;
    if (cudaGetDeviceCount(&count) != cudaSuccess) {
        cudaGetLastError();
        return false;
    }
    return count > 0;
}

std::string cuda_device_info() {
    if (!have_cuda()) return "no CUDA device visible";
    int dev = 0, drv = 0, rt = 0;
    CDX_CUDA(cudaGetDevice(&dev));
    cudaDeviceProp p;
    CDX_CUDA(cudaGetDeviceProperties(&p, dev));
    cudaDriverGetVersion(&drv);
    cudaRuntimeGetVersion(&rt);
    std::ostringstream s;
    s << p.name << ", " << p.multiProcessorCount << " SMs, " << (p.totalGlobalMem >> 20)
      << " MiB, cc " << p.major << "." << p.minor << ", driver " << drv / 1000 << "."
      << (drv % 1000) / 10 << ", runtime " << rt / 1000 << "." << (rt % 1000) / 10;
    return s.str();
}

namespace gpu_detail {

Result run_cuda(const Tx* tx, size_t n_in, Policy policy, const Selectors& sels,
                const GpuOptions& opts, GpuTimings* timings) {
    using clk = std::chrono::steady_clock;
    if (n_in > 0x7FFFFFFFull) throw std::runtime_error("slice too large for the GPU pipeline");
    check_ids(tx, n_in);
    Ctx& c = ctx();
    c.launches = 0;
    c.syncs = 0;
    const uint32_t n = uint32_t(n_in);
    const gk::DevSelectors ds{sels.plain, sels.transfer, sels.approve, sels.transfer_from};
    const int pol = int(policy);
    const cudaStream_t s = c.stream;
    GpuTimings tm;
    Result res;
    res.graph.n = n;
    res.graph.row_offsets.assign(size_t(n) + 1, 0);
    if (n == 0) {
        if (timings) *timings = tm;
        return res;
    }
    const auto w0 = clk::now();

    // ---- H2D ----------------------------------------------------------------
    c.tx.ensure(n);
    c.counts.ensure(size_t(n) + 1);
    c.off.ensure(size_t(n) + 1);
    const Tx* src = tx;
    if (opts.pinned) {
        c.h_tx.ensure(n);
        std::memcpy(c.h_tx.p, tx, size_t(n) * sizeof(Tx));
        src = c.h_tx.p;
    }
    CDX_CUDA(cudaEventRecord(c.ev[0], s));
    CDX_CUDA(cudaMemcpyAsync(c.tx.p, src, size_t(n) * sizeof(Tx), cudaMemcpyHostToDevice, s));
    CDX_CUDA(cudaEventRecord(c.ev[1], s));

    // ---- stage 1: extraction ----------------------------------------------------
    CDX_CUDA(cudaMemsetAsync(c.counts.p + n, 0, sizeof(uint32_t), s));
    k_extract_count<<<grid_for(n), BLOCK, 0, s>>>(c.tx.p, n, pol, ds, c.counts.p);
    CDX_KCHECK();
    ++c.launches;
    ex_sum(c, c.counts.p, c.off.p, uint64_t(n) + 1);
    const uint32_t R = c.read(c.off.p + n);
    c.hi.ensure(R);
    c.lo.ensure(R);
    c.hi2.ensure(R);
    c.lo2.ensure(R);
    k_extract_write<<<grid_for(n), BLOCK, 0, s>>>(c.tx.p, n, pol, ds, c.off.p, c.hi.p, c.lo.p);
    CDX_KCHECK();
    ++c.launches;
    CDX_CUDA(cudaEventRecord(c.ev[2], s));

    // ---- stage 2a: stable radix sort by key ---------------------------------------
    if (R) {
        check_int(R);
        size_t bytes = 0;
        CDX_CUDA(cub::DeviceRadixSort::SortPairs(nullptr, bytes, c.hi.p, c.hi2.p, c.lo.p, c.lo2.p,
                                                 int(R), 0, 64, s));
        c.temp.ensure(bytes);
        CDX_CUDA(cub::DeviceRadixSort::SortPairs(c.temp.p, bytes, c.hi.p, c.hi2.p, c.lo.p,
                                                 c.lo2.p, int(R), 0, 64, s));
        ++c.launches;
    }
    uint64_t* shi = c.hi2.p;
    uint64_t* slo = c.lo2.p;
    CDX_CUDA(cudaEventRecord(c.ev[3], s));

    // ---- stage 2b: detection --------------------------------------------------------
    c.gflag.ensure(R + 1);
    c.rflag.ensure(R + 1);
    c.gscan.ensure(R + 1);
    c.rscan.ensure(R + 1);
    c.gstart.ensure(R + 1);
    c.rstart.ensure(R + 1);
    CDX_CUDA(cudaMemsetAsync(c.scalars64.p, 0, 8 * sizeof(uint64_t), s));
    CDX_CUDA(cudaMemsetAsync(c.scalars32.p, 0, 8 * sizeof(uint32_t), s));
    uint32_t G = 0, NR = 0;
    uint64_t E = 0;
    if (R) {
        k_head_flags<<<grid_for(R), BLOCK, 0, s>>>(R, shi, slo, c.gflag.p, c.rflag.p);
        CDX_KCHECK();
        in_sum(c, c.gflag.p, c.gscan.p, R);
        in_sum(c, c.rflag.p, c.rscan.p, R);
        k_scatter_starts<<<grid_for(R), BLOCK, 0, s>>>(R, c.gflag.p, c.rflag.p, c.gscan.p,
                                                        c.rscan.p, c.gstart.p, c.rstart.p);
        CDX_KCHECK();
        c.launches += 2;
        CDX_CUDA(cudaMemcpyAsync(c.h_small.p, c.gscan.p + R - 1, 4, cudaMemcpyDeviceToHost, s));
        CDX_CUDA(cudaMemcpyAsync(reinterpret_cast<char*>(c.h_small.p) + 4, c.rscan.p + R - 1, 4,
                                 cudaMemcpyDeviceToHost, s));
        CDX_CUDA(cudaStreamSynchronize(s));
        ++c.syncs;
        std::memcpy(&G, c.h_small.p, 4);
        std::memcpy(&NR, reinterpret_cast<char*>(c.h_small.p) + 4, 4);
        k_group_max<<<grid_for(G), BLOCK, 0, s>>>(G, c.gstart.p, c.scalars32.p);
        CDX_KCHECK();
        ++c.launches;
        if (opts.detect == DetectMode::Group) {
            c.gcount.ensure(size_t(G) + 1);
            c.goff.ensure(size_t(G) + 1);
            CDX_CUDA(cudaMemsetAsync(c.gcount.p + G, 0, sizeof(uint64_t), s));
            k_group_count<<<grid_for(G), BLOCK, 0, s>>>(G, slo, c.gstart.p, c.gcount.p,
                                                         c.scalars64.p);
            CDX_KCHECK();
            ++c.launches;
            ex_sum(c, c.gcount.p, c.goff.p, uint64_t(G) + 1);
            E = c.read(c.goff.p + G);
            c.edges.ensure(E);
            if (E) {
                k_group_emit<<<grid_for(G), BLOCK, 0, s>>>(G, slo, c.gstart.p, c.goff.p,
                                                            c.edges.p);
                CDX_KCHECK();
                ++c.launches;
            }
        } else {
            c.ecount.ensure(size_t(R) + 1);
            c.eoff.ensure(size_t(R) + 1);
            CDX_CUDA(cudaMemsetAsync(c.ecount.p + R, 0, sizeof(uint64_t), s));
            k_record_count<<<grid_for(R), BLOCK, 0, s>>>(R, slo, c.gflag.p, c.rscan.p,
                                                          c.rstart.p, c.ecount.p, c.scalars64.p);
            CDX_KCHECK();
            ++c.launches;
            ex_sum(c, c.ecount.p, c.eoff.p, uint64_t(R) + 1);
            E = c.read(c.eoff.p + R);
            c.edges.ensure(E);
            if (E) {
                k_edge_emit<<<grid_for(E), BLOCK, 0, s>>>(E, R, c.eoff.p, slo, c.rscan.p,
                                                           c.rstart.p, c.edges.p);
                CDX_KCHECK();
                ++c.launches;
            }
        }
    }
    CDX_CUDA(cudaEventRecord(c.ev[4], s));

    // ---- stage 3: CSR ---------------------------------------------------------------
    c.row.ensure(size_t(n) + 1);
    uint64_t M = 0;
    if (E) {
        c.lin.ensure(E);
        c.lin2.ensure(E);
        k_edge_to_linear<<<grid_for(E), BLOCK, 0, s>>>(E, n, c.edges.p, c.lin.p);
        CDX_KCHECK();
        ++c.launches;
        check_int(E);
        const int end_bit = int(gk::bits_for(uint64_t(n) * n));
        size_t bytes = 0;
        CDX_CUDA(cub::DeviceRadixSort::SortKeys(nullptr, bytes, c.lin.p, c.lin2.p, int(E), 0,
                                                end_bit, s));
        c.temp.ensure(bytes);
        CDX_CUDA(cub::DeviceRadixSort::SortKeys(c.temp.p, bytes, c.lin.p, c.lin2.p, int(E), 0,
                                                end_bit, s));
        int* d_num = reinterpret_cast<int*>(c.scalars32.p + 3);
        bytes = 0;
        CDX_CUDA(cub::DeviceSelect::Unique(nullptr, bytes, c.lin2.p, c.lin.p, d_num, int(E), s));
        c.temp.ensure(bytes);
        CDX_CUDA(cub::DeviceSelect::Unique(c.temp.p, bytes, c.lin2.p, c.lin.p, d_num, int(E), s));
        c.launches += 2;
        M = uint64_t(c.read(d_num));
        c.cols.ensure(M);
        k_csr_cols<<<grid_for(M), BLOCK, 0, s>>>(M, n, c.lin.p, c.cols.p);
        CDX_KCHECK();
        ++c.launches;
    } else {
        c.cols.ensure(1);
    }
    k_csr_rows<<<grid_for(uint64_t(n) + 1), BLOCK, 0, s>>>(n, M, c.lin.p, c.row.p);
    CDX_KCHECK();
    ++c.launches;
    CDX_CUDA(cudaEventRecord(c.ev[5], s));

    // ---- stage 4 on the GPU (kahn / jp) ------------------------------------------------
    const uint32_t T = opts.tail_threshold;
    const int TB = opts.tail_block;
    const uint32_t K = std::max<uint32_t>(1, opts.chunk);
    bool gpu_level = opts.level != GpuLevel::Sweep;
    uint32_t rounds = 0;
    uint64_t tail_rounds = 0, relax = 0;
    uint32_t* d_result = nullptr;
    RoundState* d_st = c.rstate.p;
    if (gpu_level) CDX_CUDA(cudaMemsetAsync(d_st, 0, sizeof(RoundState), s));
    if (opts.level == GpuLevel::Kahn) {
        c.indeg.ensure(n);
        c.level.ensure(n);
        c.fa.ensure(n);
        c.fb.ensure(n);
        CDX_CUDA(cudaMemsetAsync(c.indeg.p, 0, size_t(n) * 4, s));
        CDX_CUDA(cudaMemsetAsync(c.level.p, 0, size_t(n) * 4, s));
        if (M) {
            k_kahn_indeg<<<grid_for(M), BLOCK, 0, s>>>(M, c.cols.p, c.indeg.p);
            CDX_KCHECK();
            ++c.launches;
        }
        k_kahn_init<<<grid_for(n), BLOCK, 0, s>>>(n, c.indeg.p, c.level.p, c.fa.p, &d_st->size);
        CDX_KCHECK();
        ++c.launches;
        RoundState st = c.read(d_st);
        while (st.size > 0) {
            const uint32_t before = st.round;
            if (st.size > T) {
                for (uint32_t k = 0; k < K; ++k) {
                    k_kahn_round<<<c.dgrid, BLOCK, 0, s>>>(d_st, c.fa.p, c.fb.p, c.row.p, c.cols.p,
                                                           c.indeg.p, c.level.p);
                    k_round_advance<<<1, 1, 0, s>>>(d_st);
                }
                CDX_KCHECK();
                c.launches += 2 * K;
                st = c.read(d_st);
            } else {
                k_kahn_tail<<<1, TB, 0, s>>>(d_st, c.fa.p, c.fb.p, T, c.row.p, c.cols.p,
                                             c.indeg.p, c.level.p);
                CDX_KCHECK();
                ++c.launches;
                st = c.read(d_st);
                tail_rounds += st.round - before;
            }
        }
        rounds = st.round;
        d_result = c.level.p;
        relax = M;
    } else if (opts.level == GpuLevel::JpIndex || opts.level == GpuLevel::JpRandom) {
        const bool random = opts.level == GpuLevel::JpRandom;
        const uint32_t tx_bits = gk::bits_for(uint64_t(n) + 1);
        const uint32_t hash_bits =
            random ? std::min<uint32_t>(24, uint32_t(gk::JP_PRIO_BITS) - tx_bits) : 0;
        c.r_gid.ensure(R + 1);
        c.r_tx.ensure(R + 1);
        c.r_mode.ensure(R + 1);
        c.maxA.ensure(G + 1);
        c.maxW.ensure(G + 1);
        c.maxR.ensure(G + 1);
        c.maxC.ensure(G + 1);
        c.fail.ensure(n);
        c.color.ensure(n);
        c.at.ensure(n);
        c.at2.ensure(n);
        c.ar.ensure(R + 1);
        c.ar2.ensure(R + 1);
        CDX_CUDA(cudaMemsetAsync(c.maxA.p, 0, (size_t(G) + 1) * 8, s));
        CDX_CUDA(cudaMemsetAsync(c.maxW.p, 0, (size_t(G) + 1) * 8, s));
        CDX_CUDA(cudaMemsetAsync(c.maxR.p, 0, (size_t(G) + 1) * 8, s));
        CDX_CUDA(cudaMemsetAsync(c.maxC.p, 0, (size_t(G) + 1) * 8, s));
        CDX_CUDA(cudaMemsetAsync(c.fail.p, 0, size_t(n) * 4, s));
        CDX_CUDA(cudaMemsetAsync(c.color.p, 0xFF, size_t(n) * 4, s));
        if (R) {
            k_jp_unpack<<<grid_for(R), BLOCK, 0, s>>>(R, c.gscan.p, slo, c.r_gid.p, c.r_tx.p,
                                                       c.r_mode.p);
            CDX_KCHECK();
            k_iota<<<grid_for(R), BLOCK, 0, s>>>(R, c.ar.p);
            CDX_KCHECK();
            c.launches += 2;
        }
        k_iota<<<grid_for(n), BLOCK, 0, s>>>(n, c.at.p);
        CDX_KCHECK();
        ++c.launches;
        RoundState init{};
        init.size = n;
        init.nrec = R;
        CDX_CUDA(cudaMemcpyAsync(d_st, &init, sizeof(RoundState), cudaMemcpyHostToDevice, s));
        gk::JpArgs a{c.r_gid.p, c.r_tx.p, c.r_mode.p, c.maxA.p, c.maxW.p, c.maxR.p, c.maxC.p,
                     c.fail.p, c.color.p, tx_bits, hash_bits, opts.jp_seed};
        RoundState st = init;
        while (st.size > 0) {
            const uint32_t before = st.round;
            if (std::max(st.size, st.nrec) > T) {
                for (uint32_t k = 0; k < K; ++k) {
                    k_jp_a<<<c.dgrid, BLOCK, 0, s>>>(d_st, c.ar.p, c.ar2.p, a);
                    k_jp_b<<<c.dgrid, BLOCK, 0, s>>>(d_st, c.ar.p, c.ar2.p, a);
                    k_jp_c<<<c.dgrid, BLOCK, 0, s>>>(d_st, c.at.p, c.at2.p, c.ar.p, c.ar2.p, a);
                    k_round_advance<<<1, 1, 0, s>>>(d_st);
                }
                CDX_KCHECK();
                c.launches += 4 * K;
                st = c.read(d_st);
            } else {
                k_jp_tail<<<1, TB, 0, s>>>(d_st, c.at.p, c.at2.p, c.ar.p, c.ar2.p, a, T);
                CDX_KCHECK();
                ++c.launches;
                st = c.read(d_st);
                tail_rounds += st.round - before;
            }
        }
        rounds = st.round;
        relax = 0;   // not tracked on the device
        d_result = c.color.p;
    }
    CDX_CUDA(cudaEventRecord(c.ev[6], s));

    // ---- D2H: graph (always) and GPU levels ---------------------------------------
    uint64_t* h_row = res.graph.row_offsets.data();
    res.graph.cols.resize(M);
    uint32_t* h_cols = res.graph.cols.data();
    std::vector<uint32_t> lv;
    if (gpu_level) lv.resize(n);
    if (opts.pinned) {
        c.h_row.ensure(size_t(n) + 1);
        c.h_cols.ensure(M + 1);
        c.h_lv.ensure(n);
        h_row = c.h_row.p;
        h_cols = c.h_cols.p;
    }
    CDX_CUDA(cudaMemcpyAsync(h_row, c.row.p, (size_t(n) + 1) * 8, cudaMemcpyDeviceToHost, s));
    if (M) CDX_CUDA(cudaMemcpyAsync(h_cols, c.cols.p, M * 4, cudaMemcpyDeviceToHost, s));
    if (gpu_level)
        CDX_CUDA(cudaMemcpyAsync(opts.pinned ? c.h_lv.p : lv.data(), d_result, size_t(n) * 4,
                                 cudaMemcpyDeviceToHost, s));
    CDX_CUDA(cudaEventRecord(c.ev[7], s));
    CDX_CUDA(cudaEventSynchronize(c.ev[7]));
    if (opts.pinned) {
        std::memcpy(res.graph.row_offsets.data(), c.h_row.p, (size_t(n) + 1) * 8);
        if (M) std::memcpy(res.graph.cols.data(), c.h_cols.p, M * 4);
        if (gpu_level) std::memcpy(lv.data(), c.h_lv.p, size_t(n) * 4);
    }
    const uint32_t max_group = R ? c.read(c.scalars32.p) : 0;
    const uint64_t read_comm = c.read(c.scalars64.p);

    // ---- stage 4 on the CPU (sweep) ----------------------------------------------
    double sweep_secs = 0;
    if (!gpu_level) {
        const auto a = clk::now();
        res.levels = level(res.graph, &res.level_stats);
        sweep_secs = std::chrono::duration<double>(clk::now() - a).count();
    } else {
        res.levels = std::move(lv);
        res.level_stats = {relax, 2 * relax, rounds};
    }
    const auto w1 = clk::now();

    res.records = R;
    res.stats.emitted = E;
    res.stats.read_comm_edges = read_comm;
    res.stats.groups = G;
    res.stats.imbalance = 0;

    tm.h2d = ms_between(c.ev[0], c.ev[1]);
    tm.extract = ms_between(c.ev[1], c.ev[2]);
    tm.sort = ms_between(c.ev[2], c.ev[3]);
    tm.detect = ms_between(c.ev[3], c.ev[4]);
    tm.csr = ms_between(c.ev[4], c.ev[5]);
    tm.level = gpu_level ? ms_between(c.ev[5], c.ev[6]) : sweep_secs;
    tm.d2h = ms_between(c.ev[6], c.ev[7]);
    tm.wall = std::chrono::duration<double>(w1 - w0).count();
    tm.launches = c.launches;
    tm.host_syncs = c.syncs;
    tm.level_rounds = rounds;
    tm.tail_rounds = tail_rounds;
    tm.max_group = max_group;
    tm.device_bytes = c.device_bytes();
    if (timings) *timings = tm;
    return res;
}

}  // namespace gpu_detail
}  // namespace conduit
