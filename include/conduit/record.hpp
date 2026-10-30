// Packed access record: one 128-bit unsigned integer held as two u64 words.
//
//   hi [63:0]   state_key (Phase 2 key, unchanged; allowances use all 64 bits)
//   lo [63:35]  zero
//   lo [34:3]   tx index, local to the super-block
//   lo [2:0]    mode (READ=1, COMMUTATIVE=2, WRITE=4)
//
// Sorting records as plain integers groups them by key and, within a key,
// orders them by block position. A tx lists each key at most once, so no two
// records are equal and any sort yields the same order. The CUDA port can
// radix-sort the same layout (CUB with a decomposer, or two stable passes).
#pragma once

#include <cstdint>

namespace conduit {

struct Record {
    uint64_t hi;
    uint64_t lo;
};

inline bool operator<(const Record& a, const Record& b) {
    return a.hi < b.hi || (a.hi == b.hi && a.lo < b.lo);
}
inline bool operator==(const Record& a, const Record& b) {
    return a.hi == b.hi && a.lo == b.lo;
}

inline Record pack(uint64_t key, uint32_t tx, uint8_t mode) {
    return {key, (uint64_t(tx) << 3) | mode};
}
inline uint64_t rec_key(const Record& r) { return r.hi; }
inline uint32_t rec_tx(const Record& r) { return uint32_t(r.lo >> 3); }
inline uint8_t rec_mode(const Record& r) { return uint8_t(r.lo & 7); }

}  // namespace conduit
