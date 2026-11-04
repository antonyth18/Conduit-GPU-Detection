// Per-thread kernel bodies shared by the CUDA backend (cuda_pipeline.cu) and
// the host emulation (emu_pipeline.cpp). Each body handles ONE index of a
// launch; the CUDA side wraps it in a grid-stride (or block-stride) loop, the
// emulation in a plain for loop. Bodies that are separated by a barrier on
// the GPU are separate functions here, so the emulation keeps the barriers
// where the GPU has them.
//
// Nothing here may depend on the order in which indices run within one
// phase: atomics are only used for commutative updates (add, max) or to take
// a slot in an output list whose order is irrelevant to the result.
#pragma once

#include <cstdint>

#include "conduit/corpus.hpp"

#if defined(__CUDACC__)
#define CDX_HD __host__ __device__ __forceinline__
#else
#define CDX_HD inline
#endif

namespace conduit::gk {

// ---- atomics: real on the device, plain read-modify-write on the host --------

CDX_HD uint32_t atomic_add_u32(uint32_t* p, uint32_t v) {
#if defined(__CUDA_ARCH__)
    return atomicAdd(p, v);
#else
    const uint32_t o = *p;
    *p = o + v;
    return o;
#endif
}

CDX_HD uint32_t atomic_sub_u32(uint32_t* p, uint32_t v) {
#if defined(__CUDA_ARCH__)
    return atomicSub(p, v);
#else
    const uint32_t o = *p;
    *p = o - v;
    return o;
#endif
}

CDX_HD void atomic_max_u32(uint32_t* p, uint32_t v) {
#if defined(__CUDA_ARCH__)
    atomicMax(p, v);
#else
    if (*p < v) *p = v;
#endif
}

CDX_HD void atomic_add_u64(uint64_t* p, uint64_t v) {
#if defined(__CUDA_ARCH__)
    atomicAdd(reinterpret_cast<unsigned long long*>(p), static_cast<unsigned long long>(v));
#else
    *p += v;
#endif
}

CDX_HD void atomic_max_u64(uint64_t* p, uint64_t v) {
#if defined(__CUDA_ARCH__)
    atomicMax(reinterpret_cast<unsigned long long*>(p), static_cast<unsigned long long>(v));
#else
    if (*p < v) *p = v;
#endif
}

// ---- stage 1: access-set inference (port of policy.cpp, must stay identical) --

constexpr uint32_t K_ABSENT = 0xFFFFFFFFu;
constexpr uint8_t K_READ = 1, K_COMM = 2, K_WRITE = 4;
constexpr uint8_t K_FLAG_DECODED = 1, K_FLAG_CREATE = 8;
constexpr int K_MAX_ACC = 7;
constexpr uint8_t K_CAT_FALLBACK = 0, K_CAT_PLAIN = 1, K_CAT_TRANSFER = 2, K_CAT_APPROVE = 3,
                  K_CAT_TRANSFER_FROM = 4;

struct DevSelectors {
    int64_t plain, transfer, approve, transfer_from;
};

CDX_HD uint64_t k_kind(uint64_t kind, uint64_t a, uint64_t b) { return (kind << 60) | (a << 21) | b; }
CDX_HD uint64_t k_account(uint64_t a) { return k_kind(0, a, 0); }
CDX_HD uint64_t k_nonce(uint64_t a) { return k_kind(1, a, 0); }
CDX_HD uint64_t k_eth_bal(uint64_t a) { return k_kind(2, a, 0); }
CDX_HD uint64_t k_token_bal(uint64_t t, uint64_t h) { return k_kind(3, t, h); }
CDX_HD uint64_t k_allowance(uint64_t t, uint64_t o, uint64_t s) {
    return (uint64_t(1) << 63) | (t << 42) | (o << 21) | s;
}

CDX_HD uint8_t k_resolve(uint8_t m) {
    if ((m & K_WRITE) || ((m & K_READ) && (m & K_COMM))) return K_WRITE;
    if (m & K_COMM) return K_COMM;
    return K_READ;
}

CDX_HD uint8_t k_classify(const Tx& t, const DevSelectors& s) {
    const int64_t sel = t.sel;
    const bool create = (t.flags & K_FLAG_CREATE) != 0;
    const bool ends = t.frm != K_ABSENT && t.to != K_ABSENT;
    const bool ok = (t.flags & K_FLAG_DECODED) && !create && ends;
    const bool has1 = t.a1 != K_ABSENT;
    const bool has2 = t.a2 != K_ABSENT;
    if (sel == s.plain && !create && ends) return K_CAT_PLAIN;
    if (ok && sel == s.transfer && has1) return K_CAT_TRANSFER;
    if (ok && sel == s.approve && has1) return K_CAT_APPROVE;
    if (ok && sel == s.transfer_from && has1 && has2) return K_CAT_TRANSFER_FROM;
    return K_CAT_FALLBACK;
}

// policy: 0 = P1, 1 = P2, 2 = P3. Writes up to 7 (key, mode), keys ascending
// and unique; returns the count.
CDX_HD int k_tx_access(const Tx& t, int policy, const DevSelectors& sels, uint64_t* keys,
                       uint8_t* modes) {
    const uint8_t bal = policy == 2 ? K_COMM : K_WRITE;
    const bool has_frm = t.frm != K_ABSENT;
    const bool has_to = t.to != K_ABSENT;
    const uint64_t frm = t.frm, to = t.to, a1 = t.a1, a2 = t.a2;
    uint64_t ck[K_MAX_ACC];
    uint8_t cm[K_MAX_ACC];
    int k = 0;
    if (policy == 0) {
        if (has_frm) { ck[k] = k_account(frm); cm[k++] = K_WRITE; }
        if (has_to) { ck[k] = k_account(to); cm[k++] = K_WRITE; }
    } else {
        const uint8_t cat = k_classify(t, sels);
        const bool fb = cat == K_CAT_FALLBACK;
        if (!fb && has_frm) {
            ck[k] = k_nonce(frm); cm[k++] = K_WRITE;
            ck[k] = k_eth_bal(frm); cm[k++] = bal;
            ck[k] = k_account(frm); cm[k++] = K_READ;
        }
        if (fb && has_frm) { ck[k] = k_account(frm); cm[k++] = K_WRITE; }
        if (has_to) { ck[k] = k_account(to); cm[k++] = fb ? K_WRITE : K_READ; }
        if (cat == K_CAT_PLAIN) {
            ck[k] = k_eth_bal(to); cm[k++] = bal;
        } else if (cat == K_CAT_TRANSFER) {
            ck[k] = k_token_bal(to, frm); cm[k++] = bal;
            ck[k] = k_token_bal(to, a1); cm[k++] = bal;
        } else if (cat == K_CAT_APPROVE) {
            ck[k] = k_allowance(to, frm, a1); cm[k++] = K_WRITE;
        } else if (cat == K_CAT_TRANSFER_FROM) {
            ck[k] = k_token_bal(to, a1); cm[k++] = bal;
            ck[k] = k_token_bal(to, a2); cm[k++] = bal;
            ck[k] = k_allowance(to, a1, frm); cm[k++] = K_WRITE;
        }
    }
    // insertion sort by key (<= 7 items), then merge duplicates
    for (int a = 1; a < k; ++a)
        for (int b = a; b > 0 && ck[b] < ck[b - 1]; --b) {
            const uint64_t tk = ck[b]; ck[b] = ck[b - 1]; ck[b - 1] = tk;
            const uint8_t tm = cm[b]; cm[b] = cm[b - 1]; cm[b - 1] = tm;
        }
    int out = 0;
    for (int a = 0; a < k;) {
        uint8_t m = 0;
        int b = a;
        while (b < k && ck[b] == ck[a]) m |= cm[b++];
        keys[out] = ck[a];
        modes[out] = k_resolve(m);
        ++out;
        a = b;
    }
    return out;
}

CDX_HD void extract_count(uint32_t i, const Tx* tx, int policy, DevSelectors sels,
                          uint32_t* counts) {
    uint64_t keys[K_MAX_ACC];
    uint8_t modes[K_MAX_ACC];
    counts[i] = uint32_t(k_tx_access(tx[i], policy, sels, keys, modes));
}

// hi = key, lo = tx << 3 | mode (record.hpp layout), at offsets[i].
CDX_HD void extract_write(uint32_t i, const Tx* tx, int policy, DevSelectors sels,
                          const uint32_t* offsets, uint64_t* hi, uint64_t* lo) {
    uint64_t keys[K_MAX_ACC];
    uint8_t modes[K_MAX_ACC];
    const int k = k_tx_access(tx[i], policy, sels, keys, modes);
    const uint32_t base = offsets[i];
    for (int j = 0; j < k; ++j) {
        hi[base + j] = keys[j];
        lo[base + j] = (uint64_t(i) << 3) | modes[j];
    }
}

// ---- stage 2: conflict detection ----------------------------------------------

CDX_HD uint32_t lo_tx(uint64_t lo) { return uint32_t(lo >> 3); }
CDX_HD uint8_t lo_mode(uint64_t lo) { return uint8_t(lo & 7); }

// group head flag and run head flag. A run is a maximal block of equal
// non-WRITE modes, or a single WRITE (detail::walk_group).
CDX_HD void head_flags(uint32_t i, const uint64_t* hi, const uint64_t* lo, uint32_t* gflag,
                       uint32_t* rflag) {
    const bool g = i == 0 || hi[i] != hi[i - 1];
    const uint8_t m = lo_mode(lo[i]);
    const bool r = g || m == K_WRITE || m != lo_mode(lo[i - 1]);
    gflag[i] = g;
    rflag[i] = r;
}

// After inclusive scans: gid[i] = group of record i, rid[i] = run. Scatter
// the start position of each group / run.
CDX_HD void scatter_starts(uint32_t i, const uint32_t* gflag, const uint32_t* rflag,
                           const uint32_t* gscan, const uint32_t* rscan, uint32_t* gstart,
                           uint32_t* rstart) {
    if (gflag[i]) gstart[gscan[i] - 1] = i;
    if (rflag[i]) rstart[rscan[i] - 1] = i;
}

// --- naive: one thread per key group (same code path as detail::walk_group) ---

CDX_HD void group_count(uint32_t g, const uint64_t* lo, const uint32_t* gstart,
                        uint64_t* gcount, uint64_t* read_comm) {
    const uint32_t b = gstart[g], e = gstart[g + 1];
    uint64_t edges = 0, rc = 0, prev_len = 0;
    uint8_t prev_mode = 0;
    uint32_t p = b;
    while (p < e) {
        const uint8_t mode = lo_mode(lo[p]);
        uint32_t q = p + 1;
        if (mode != K_WRITE)
            while (q < e && lo_mode(lo[q]) == mode) ++q;
        const uint64_t len = q - p;
        edges += prev_len * len;
        if (prev_len && (prev_mode | mode) == (K_READ | K_COMM)) rc += prev_len * len;
        prev_len = len;
        prev_mode = mode;
        p = q;
    }
    gcount[g] = edges;
    if (rc) atomic_add_u64(read_comm, rc);
}

CDX_HD void group_size_max(uint32_t g, const uint32_t* gstart, uint32_t* out) {
    atomic_max_u32(out, gstart[g + 1] - gstart[g]);
}

CDX_HD void group_emit(uint32_t g, const uint64_t* lo, const uint32_t* gstart,
                       const uint64_t* goff, uint64_t* edges) {
    const uint32_t b = gstart[g], e = gstart[g + 1];
    uint64_t out = goff[g];
    uint32_t pb = 0, pe = 0;
    uint32_t p = b;
    while (p < e) {
        const uint8_t mode = lo_mode(lo[p]);
        uint32_t q = p + 1;
        if (mode != K_WRITE)
            while (q < e && lo_mode(lo[q]) == mode) ++q;
        for (uint32_t a = pb; a < pe; ++a)
            for (uint32_t c = p; c < q; ++c)
                edges[out++] = (uint64_t(lo_tx(lo[a])) << 32) | lo_tx(lo[c]);
        pb = p;
        pe = q;
        p = q;
    }
}

// --- balanced: per-record in-edge counts, then one thread per output edge ---

// Record i in run r depends on every record of run r-1 when both runs are in
// the same group (run r does not start a group).
CDX_HD void record_count(uint32_t i, const uint64_t* lo, const uint32_t* gflag,
                         const uint32_t* rscan, const uint32_t* rstart, uint64_t* ecount,
                         uint64_t* read_comm) {
    const uint32_t r = rscan[i] - 1;
    const uint32_t s = rstart[r];
    if (gflag[s]) {
        ecount[i] = 0;
        return;
    }
    const uint32_t ps = rstart[r - 1];
    const uint64_t len = s - ps;
    ecount[i] = len;
    if ((lo_mode(lo[ps]) | lo_mode(lo[i])) == (K_READ | K_COMM)) atomic_add_u64(read_comm, len);
}

// Middle mapping: one thread per record writes all of its in-edges (the
// previous run) at its own offset. Balanced across key groups, but a record
// that follows a long run (a WRITE after thousands of READs) is still one
// thread's serial loop.
CDX_HD void record_emit(uint32_t c, const uint64_t* eoff, const uint64_t* lo,
                        const uint32_t* gflag, const uint32_t* rscan, const uint32_t* rstart,
                        uint64_t* edges) {
    const uint32_t r = rscan[c] - 1;
    const uint32_t s = rstart[r];
    if (gflag[s]) return;
    const uint32_t ps = rstart[r - 1];
    uint64_t out = eoff[c];
    const uint64_t v = lo_tx(lo[c]);
    for (uint32_t a = ps; a < s; ++a) edges[out++] = (uint64_t(lo_tx(lo[a])) << 32) | v;
}

// eoff: exclusive scan of ecount, length R + 1 (eoff[R] = E).
CDX_HD void edge_emit(uint64_t e, uint32_t R, const uint64_t* eoff, const uint64_t* lo,
                      const uint32_t* rscan, const uint32_t* rstart, uint64_t* edges) {
    // last c with eoff[c] <= e (upper_bound - 1); that record has ecount > 0
    uint32_t lo_i = 0, hi_i = R;   // search in [0, R)
    while (hi_i - lo_i > 1) {
        const uint32_t mid = lo_i + (hi_i - lo_i) / 2;
        if (eoff[mid] <= e) lo_i = mid;
        else hi_i = mid;
    }
    const uint32_t c = lo_i;
    const uint32_t r = rscan[c] - 1;
    const uint32_t a = rstart[r - 1] + uint32_t(e - eoff[c]);
    edges[e] = (uint64_t(lo_tx(lo[a])) << 32) | lo_tx(lo[c]);
}

// ---- stage 3: CSR ----------------------------------------------------------

CDX_HD void edge_to_linear(uint64_t e, uint32_t n, const uint64_t* edges, uint64_t* lin) {
    const uint64_t u = edges[e] >> 32, v = edges[e] & 0xFFFFFFFFull;
    lin[e] = u * n + v;
}

CDX_HD void csr_cols(uint64_t e, uint32_t n, const uint64_t* lin, uint32_t* cols) {
    cols[e] = uint32_t(lin[e] % n);
}

// row_offsets[u] = first e with lin[e] >= u * n, for u in [0, n].
CDX_HD void csr_rows(uint32_t u, uint32_t n, uint64_t M, const uint64_t* lin,
                     uint64_t* row_offsets) {
    const uint64_t key = uint64_t(u) * n;
    uint64_t a = 0, b = M;
    while (a < b) {
        const uint64_t mid = a + (b - a) / 2;
        if (lin[mid] < key) a = mid + 1;
        else b = mid;
    }
    row_offsets[u] = a;
}

// ---- stage 4a: Kahn levelling on the reduced DAG ---------------------------

CDX_HD void kahn_indeg(uint64_t e, const uint32_t* cols, uint32_t* indeg) {
    atomic_add_u32(&indeg[cols[e]], 1);
}

CDX_HD void kahn_init(uint32_t v, const uint32_t* indeg, uint32_t* level, uint32_t* frontier,
                      uint32_t* count) {
    if (indeg[v] == 0) {
        level[v] = 0;
        frontier[atomic_add_u32(count, 1)] = v;
    }
}

// A vertex enters the next frontier in the round its last predecessor is
// processed: its level is that round's index (= longest path).
CDX_HD void kahn_expand(uint32_t i, const uint32_t* cur, const uint64_t* row,
                        const uint32_t* cols, uint32_t* indeg, uint32_t* level,
                        uint32_t next_level, uint32_t* nxt, uint32_t* count) {
    const uint32_t u = cur[i];
    for (uint64_t e = row[u]; e < row[u + 1]; ++e) {
        const uint32_t v = cols[e];
        if (atomic_sub_u32(&indeg[v], 1) == 1) {
            level[v] = next_level;
            nxt[atomic_add_u32(count, 1)] = v;
        }
    }
}

// ---- stage 4b: Jones-Plassmann on the implicit conflict graph ---------------
//
// Records sorted by key carry (group id, tx, mode). Two accesses to one key
// conflict unless both are shared (READ-READ or COMM-COMM; Phase 2 never puts
// READ and COMM on one key, and the conflict rule is applied exactly below
// when it does: a READ and a COMM record count as conflicting).
// Round r, for every still-uncoloured tx v with packed priority pk(v):
//   A  maxA[g] = max pk over active records of g; maxW[g], maxR[g], maxC[g]
//      the same over active WRITE / READ / COMM records
//   B  record (v, g, mode) is a local maximum iff pk(v) beats every active
//      record of g it conflicts with:
//        WRITE  pk == maxA[g]
//        READ   pk > maxW[g] and pk > maxC[g]
//        COMM   pk > maxW[g] and pk > maxR[g]
//      otherwise fail[v] = r + 1
//   C  v with fail[v] != r + 1 gets colour r; the rest stay active.
// pk = (r + 1) << 44 | prio(v): the round in the high bits makes stale maxima
// from earlier rounds lose every comparison, so nothing is ever reset.

constexpr int JP_PRIO_BITS = 44;
constexpr uint32_t JP_UNCOLORED = 0xFFFFFFFFu;

CDX_HD uint32_t jp_hash(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// tx_bits >= bits(n); hash_bits = 0 gives index priority (lower index wins).
CDX_HD uint64_t jp_prio(uint32_t tx, uint32_t tx_bits, uint32_t hash_bits, uint32_t seed) {
    const uint64_t mask = (uint64_t(1) << tx_bits) - 1;
    if (hash_bits == 0) return mask - tx;
    const uint64_t h = jp_hash(tx ^ seed) & ((uint64_t(1) << hash_bits) - 1);
    return (h << tx_bits) | tx;
}

CDX_HD uint64_t jp_pack(uint32_t round, uint64_t prio) {
    return (uint64_t(round + 1) << JP_PRIO_BITS) | prio;
}

struct JpArgs {
    const uint32_t* r_gid;
    const uint32_t* r_tx;
    const uint8_t* r_mode;
    uint64_t* maxA;
    uint64_t* maxW;
    uint64_t* maxR;
    uint64_t* maxC;
    uint32_t* fail;
    uint32_t* color;
    uint32_t tx_bits, hash_bits, seed;
};

CDX_HD void jp_phase_a(uint32_t j, const uint32_t* ar, const JpArgs& a, uint32_t round) {
    const uint32_t i = ar[j];
    const uint32_t g = a.r_gid[i];
    const uint64_t pk = jp_pack(round, jp_prio(a.r_tx[i], a.tx_bits, a.hash_bits, a.seed));
    const uint8_t m = a.r_mode[i];
    atomic_max_u64(&a.maxA[g], pk);
    if (m == K_WRITE) atomic_max_u64(&a.maxW[g], pk);
    else if (m == K_READ) atomic_max_u64(&a.maxR[g], pk);
    else atomic_max_u64(&a.maxC[g], pk);
}

CDX_HD void jp_phase_b(uint32_t j, const uint32_t* ar, const JpArgs& a, uint32_t round) {
    const uint32_t i = ar[j];
    const uint32_t g = a.r_gid[i];
    const uint32_t t = a.r_tx[i];
    const uint64_t pk = jp_pack(round, jp_prio(t, a.tx_bits, a.hash_bits, a.seed));
    const uint8_t m = a.r_mode[i];
    bool ok;
    if (m == K_WRITE) ok = a.maxA[g] == pk;
    else if (m == K_READ) ok = pk > a.maxW[g] && pk > a.maxC[g];
    else ok = pk > a.maxW[g] && pk > a.maxR[g];
    if (!ok) a.fail[t] = round + 1;
}

// Over max(n_tx, n_rec) indices: colours the winners among active txs and
// compacts both active lists (order inside the lists is irrelevant).
CDX_HD void jp_phase_c(uint32_t j, const uint32_t* at, uint32_t n_tx, const uint32_t* ar,
                       uint32_t n_rec, const JpArgs& a, uint32_t round, uint32_t* nat,
                       uint32_t* nat_count, uint32_t* nar, uint32_t* nar_count) {
    if (j < n_tx) {
        const uint32_t t = at[j];
        if (a.fail[t] != round + 1) a.color[t] = round;
        else nat[atomic_add_u32(nat_count, 1)] = t;
    }
    if (j < n_rec) {
        const uint32_t i = ar[j];
        if (a.fail[a.r_tx[i]] == round + 1) nar[atomic_add_u32(nar_count, 1)] = i;
    }
}

// Records sorted by key -> (gid, tx, mode) arrays for JP.
CDX_HD void jp_unpack(uint32_t i, const uint32_t* gscan, const uint64_t* lo, uint32_t* r_gid,
                      uint32_t* r_tx, uint8_t* r_mode) {
    r_gid[i] = gscan[i] - 1;
    r_tx[i] = lo_tx(lo[i]);
    r_mode[i] = lo_mode(lo[i]);
}

CDX_HD uint32_t bits_for(uint64_t n) {
    uint32_t b = 1;
    while ((uint64_t(1) << b) < n) ++b;
    return b;
}

}  // namespace conduit::gk
