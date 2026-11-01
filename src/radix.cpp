#include "conduit/radix.hpp"

#include <algorithm>
#include <stdexcept>

#ifdef CONDUIT_HAVE_OPENMP
#include <omp.h>
#endif

namespace conduit {

namespace {

constexpr int BITS = 11;
constexpr int RADIX = 1 << BITS;
constexpr int PASSES = (64 + BITS - 1) / BITS;   // 6
constexpr uint64_t MASK = RADIX - 1;

inline uint64_t key_of(const Record& r) { return r.hi; }
inline uint64_t key_of(uint64_t v) { return v; }
inline int digit(uint64_t k, int pass) { return int((k >> (pass * BITS)) & MASK); }

template <class T>
void lsd_seq(std::vector<T>& a, RadixStats* stats) {
    const size_t n = a.size();
    RadixStats st;
    if (n > 1) {
        // all PASSES histograms in one read of the input
        std::vector<uint64_t> hist(size_t(PASSES) * RADIX, 0);
        for (const T& x : a) {
            const uint64_t k = key_of(x);
            for (int p = 0; p < PASSES; ++p) ++hist[size_t(p) * RADIX + digit(k, p)];
        }
        std::vector<T> tmp(n);
        T* src = a.data();
        T* dst = tmp.data();
        std::vector<uint64_t> off(RADIX);
        for (int p = 0; p < PASSES; ++p) {
            const uint64_t* h = &hist[size_t(p) * RADIX];
            if (*std::max_element(h, h + RADIX) == n) {
                ++st.skipped;
                continue;
            }
            uint64_t sum = 0;
            for (int d = 0; d < RADIX; ++d) {
                off[d] = sum;
                sum += h[d];
            }
            for (size_t i = 0; i < n; ++i) dst[off[digit(key_of(src[i]), p)]++] = src[i];
            std::swap(src, dst);
            ++st.passes;
        }
        if (src != a.data()) std::copy(src, src + n, a.data());
    }
    if (stats) *stats = st;
}

#ifdef CONDUIT_HAVE_OPENMP
template <class T>
void lsd_omp(std::vector<T>& a, int threads, RadixStats* stats) {
    const int64_t n = int64_t(a.size());
    RadixStats st;
    if (n > 1) {
        const int nt = threads > 0 ? threads : omp_get_max_threads();
        std::vector<T> tmp(static_cast<size_t>(n));
        // global histograms, to decide which passes can be skipped
        std::vector<uint64_t> global(size_t(PASSES) * RADIX, 0);
        // per-thread counts for the current pass, digit-major after the scan
        std::vector<uint64_t> local(size_t(nt) * RADIX);
        std::vector<int> run;   // passes to execute
        T* src = a.data();
        T* dst = tmp.data();

#pragma omp parallel num_threads(nt)
        {
            const int t = omp_get_thread_num();
            const int64_t lo = n * t / nt, hi = n * (t + 1) / nt;
            std::vector<uint64_t> mine(size_t(PASSES) * RADIX, 0);
            for (int64_t i = lo; i < hi; ++i) {
                const uint64_t k = key_of(src[i]);
                for (int p = 0; p < PASSES; ++p) ++mine[size_t(p) * RADIX + digit(k, p)];
            }
#pragma omp critical
            for (size_t j = 0; j < mine.size(); ++j) global[j] += mine[j];
#pragma omp barrier
#pragma omp single
            {
                for (int p = 0; p < PASSES; ++p) {
                    const uint64_t* h = &global[size_t(p) * RADIX];
                    if (*std::max_element(h, h + RADIX) == uint64_t(n)) ++st.skipped;
                    else run.push_back(p);
                }
                st.passes = int(run.size());
            }
            for (int p : run) {
                uint64_t* cnt = &local[size_t(t) * RADIX];
                std::fill(cnt, cnt + RADIX, 0);
                for (int64_t i = lo; i < hi; ++i) ++cnt[digit(key_of(src[i]), p)];
#pragma omp barrier
#pragma omp single
                {
                    // exclusive scan, digit-major then thread: stable order
                    uint64_t sum = 0;
                    for (int d = 0; d < RADIX; ++d)
                        for (int u = 0; u < nt; ++u) {
                            uint64_t& c = local[size_t(u) * RADIX + d];
                            const uint64_t v = c;
                            c = sum;
                            sum += v;
                        }
                }
                for (int64_t i = lo; i < hi; ++i) dst[cnt[digit(key_of(src[i]), p)]++] = src[i];
#pragma omp barrier
#pragma omp single
                std::swap(src, dst);
            }
        }
        if (src != a.data()) std::copy(src, src + n, a.data());
    }
    if (stats) *stats = st;
}
#endif

#ifndef CONDUIT_HAVE_OPENMP
[[noreturn]] void no_openmp() {
    throw std::runtime_error("built without OpenMP; the omp implementation is unavailable");
}
#endif

}  // namespace

void radix_sort_records_seq(std::vector<Record>& r, RadixStats* s) { lsd_seq(r, s); }
void radix_sort_u64_seq(std::vector<uint64_t>& v, RadixStats* s) { lsd_seq(v, s); }

#ifdef CONDUIT_HAVE_OPENMP
void radix_sort_records_omp(std::vector<Record>& r, int threads, RadixStats* s) {
    lsd_omp(r, threads, s);
}
void radix_sort_u64_omp(std::vector<uint64_t>& v, int threads, RadixStats* s) {
    lsd_omp(v, threads, s);
}
#else
void radix_sort_records_omp(std::vector<Record>&, int, RadixStats*) { no_openmp(); }
void radix_sort_u64_omp(std::vector<uint64_t>&, int, RadixStats*) { no_openmp(); }
#endif

}  // namespace conduit
