// STREAM-style triad, a[i] = b[i] + s * c[i], to record the machine's
// achievable memory bandwidth alongside the Phase 7 timings. Counts 24 bytes
// per element (two reads, one write). Reports the best of --reps runs at each
// thread count.
//
//   membw [--mb 512] [--reps 10] [--threads 1,2,4,8]
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#ifdef CONDUIT_HAVE_OPENMP
#include <omp.h>
#endif

int main(int argc, char** argv) {
    size_t mb = 512;
    int reps = 10;
    std::vector<int> threads = {1, 2, 4, 8};
    for (int i = 1; i + 1 < argc; i += 2) {
        std::string a = argv[i], v = argv[i + 1];
        if (a == "--mb") mb = std::stoul(v);
        else if (a == "--reps") reps = std::stoi(v);
        else if (a == "--threads") {
            threads.clear();
            std::stringstream ss(v);
            std::string t;
            while (std::getline(ss, t, ',')) threads.push_back(std::stoi(t));
        }
    }
    const long long n = (long long)(mb * 1024 * 1024 / 3 / sizeof(double));
    std::vector<double> a(n), b(n, 1.0), c(n, 2.0);
    std::printf("triad over 3 x %.0f MB arrays\n", n * 8.0 / 1e6);
#ifndef CONDUIT_HAVE_OPENMP
    threads = {1};
#endif
    for (int t : threads) {
        double best = 0;
        for (int r = 0; r < reps; ++r) {
            auto t0 = std::chrono::steady_clock::now();
#ifdef CONDUIT_HAVE_OPENMP
#pragma omp parallel for num_threads(t) schedule(static)
#endif
            for (long long i = 0; i < n; ++i) a[i] = b[i] + 3.0 * c[i];
            const double s =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            best = std::max(best, 24.0 * n / s / 1e9);
        }
        std::printf("threads %2d  best %.1f GB/s  (check %.1f)\n", t, best, a[n / 2]);
    }
    return 0;
}
