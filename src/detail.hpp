// Internal helpers shared by the sequential and OpenMP pipelines.
#pragma once

#include <cstddef>
#include <cstdint>

#include "conduit/policy.hpp"
#include "conduit/record.hpp"

namespace conduit::detail {

// Reduced edge rule for one key group r[b, e) (see pipeline.hpp): split into
// runs (consecutive READs, consecutive COMMUTATIVEs, or one WRITE) and emit
// every pair between adjacent runs. emit(u, v) is called in a fixed order, so
// any caller that writes edges in call order produces identical output.
// Returns the number of READ<->COMMUTATIVE edges.
template <class Emit>
inline uint64_t walk_group(const Record* r, size_t b, size_t e, Emit&& emit) {
    uint64_t read_comm = 0;
    size_t prev_begin = 0, prev_end = 0;
    uint8_t prev_mode = 0;
    size_t p = b;
    while (p < e) {
        const uint8_t mode = rec_mode(r[p]);
        size_t q = p + 1;
        if (mode != WRITE)
            while (q < e && rec_mode(r[q]) == mode) ++q;
        if (prev_end > prev_begin) {
            for (size_t a = prev_begin; a < prev_end; ++a)
                for (size_t c = p; c < q; ++c) emit(rec_tx(r[a]), rec_tx(r[c]));
            if ((prev_mode | mode) == (READ | COMMUTATIVE))
                read_comm += uint64_t(prev_end - prev_begin) * (q - p);
        }
        prev_begin = p;
        prev_end = q;
        prev_mode = mode;
        p = q;
    }
    return read_comm;
}

// Number of edges walk_group would emit for r[b, e), from run sizes alone.
inline uint64_t count_group(const Record* r, size_t b, size_t e, uint64_t* read_comm) {
    uint64_t edges = 0;
    uint64_t prev_len = 0;
    uint8_t prev_mode = 0;
    size_t p = b;
    while (p < e) {
        const uint8_t mode = rec_mode(r[p]);
        size_t q = p + 1;
        if (mode != WRITE)
            while (q < e && rec_mode(r[q]) == mode) ++q;
        const uint64_t len = q - p;
        edges += prev_len * len;
        if (prev_len && (prev_mode | mode) == (READ | COMMUTATIVE)) *read_comm += prev_len * len;
        prev_len = len;
        prev_mode = mode;
        p = q;
    }
    return edges;
}

}  // namespace conduit::detail
