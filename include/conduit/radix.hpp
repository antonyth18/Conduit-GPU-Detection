// LSD radix sorts, 11-bit digits (2,048 buckets), 6 passes over a 64-bit key.
// A pass whose digit is the same for every element is skipped.
//
// Records are sorted on their 64-bit state key only. Extraction emits them in
// transaction order and LSD radix is stable, so within a key they stay in
// transaction order: the result equals sorting the full 128-bit record, which
// is what std::sort does in the seq-std baseline.
//
// The parallel versions give each thread a fixed contiguous chunk, histogram
// it, offset each (digit, thread) bucket by an exclusive scan in digit-major
// then thread order, and scatter. That is stable, so the output is identical
// for every thread count. CUB's device radix sort is also LSD, so Phase 8
// compares like with like.
#pragma once

#include <cstdint>
#include <vector>

#include "conduit/record.hpp"

namespace conduit {

struct RadixStats {
    int passes = 0;    // passes actually executed
    int skipped = 0;   // passes skipped because every element shared the digit
};

void radix_sort_records_seq(std::vector<Record>& r, RadixStats* stats = nullptr);
void radix_sort_u64_seq(std::vector<uint64_t>& v, RadixStats* stats = nullptr);

// threads <= 0 means the OpenMP default. Throws if built without OpenMP.
void radix_sort_records_omp(std::vector<Record>& r, int threads, RadixStats* stats = nullptr);
void radix_sort_u64_omp(std::vector<uint64_t>& v, int threads, RadixStats* stats = nullptr);

}  // namespace conduit
