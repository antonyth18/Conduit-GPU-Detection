#include "conduit/schedule.hpp"

#include <algorithm>
#include <functional>
#include <queue>
#include <sstream>
#include <tuple>

namespace conduit {

BatchCheck check_batches(const AccessSets& sets, const std::vector<uint32_t>& batch) {
    BatchCheck out;
    const size_t n = sets.size();
    struct Acc {
        uint64_t key;
        uint32_t batch;
        uint32_t tx;
        uint8_t mode;
    };
    std::vector<Acc> acc;
    acc.reserve(sets.keys.size());
    for (size_t i = 0; i < n; ++i)
        for (int64_t r = sets.offsets[i]; r < sets.offsets[i + 1]; ++r)
            acc.push_back({sets.keys[r], batch[i], uint32_t(i), sets.modes[r]});
    std::sort(acc.begin(), acc.end(), [](const Acc& a, const Acc& b) {
        return std::tie(a.key, a.batch, a.tx) < std::tie(b.key, b.batch, b.tx);
    });
    for (size_t i = 0; i < acc.size();) {
        size_t j = i;
        uint32_t w = 0, r = 0, c = 0;
        while (j < acc.size() && acc[j].key == acc[i].key && acc[j].batch == acc[i].batch) {
            w += acc[j].mode == WRITE;
            r += acc[j].mode == READ;
            c += acc[j].mode == COMMUTATIVE;
            ++j;
        }
        if (w >= 2 || (w >= 1 && r + c >= 1) || (r >= 1 && c >= 1)) {
            if (out.ok) {
                std::ostringstream s;
                s << "key 0x" << std::hex << acc[i].key << std::dec << " batch " << acc[i].batch
                  << ": " << w << "W " << r << "R " << c << "C (tx " << acc[i].tx << ")";
                out.first = s.str();
            }
            out.ok = false;
            ++out.violations;
        }
        i = j;
    }
    return out;
}

bool check_order(const Graph& g, const std::vector<uint32_t>& batch) {
    for (uint32_t u = 0; u < g.n; ++u)
        for (uint64_t e = g.row_offsets[u]; e < g.row_offsets[size_t(u) + 1]; ++e)
            if (batch[u] >= batch[g.cols[e]]) return false;
    return true;
}

std::vector<uint32_t> brute_levels(const AccessSets& sets) {
    const size_t n = sets.size();
    auto conflict = [](uint8_t a, uint8_t b) {
        return !((a == READ && b == READ) || (a == COMMUTATIVE && b == COMMUTATIVE));
    };
    std::vector<uint32_t> lv(n, 0);
    for (size_t j = 0; j < n; ++j)
        for (size_t i = 0; i < j; ++i) {
            // keys ascending within a tx: merge
            int64_t a = sets.offsets[i], ae = sets.offsets[i + 1];
            int64_t b = sets.offsets[j], be = sets.offsets[j + 1];
            bool c = false;
            while (a < ae && b < be && !c) {
                if (sets.keys[a] < sets.keys[b]) ++a;
                else if (sets.keys[b] < sets.keys[a]) ++b;
                else {
                    c = conflict(sets.modes[a], sets.modes[b]);
                    ++a;
                    ++b;
                }
            }
            if (c) lv[j] = std::max(lv[j], lv[i] + 1);
        }
    return lv;
}

uint32_t clique_bound(const AccessSets& sets) {
    struct Acc {
        uint64_t key;
        uint8_t mode;
    };
    std::vector<Acc> acc;
    acc.reserve(sets.keys.size());
    for (size_t r = 0; r < sets.keys.size(); ++r) acc.push_back({sets.keys[r], sets.modes[r]});
    std::sort(acc.begin(), acc.end(), [](const Acc& a, const Acc& b) { return a.key < b.key; });
    uint32_t best = sets.size() ? 1 : 0;
    for (size_t i = 0; i < acc.size();) {
        size_t j = i;
        uint32_t w = 0;
        bool r = false, c = false;
        while (j < acc.size() && acc[j].key == acc[i].key) {
            w += acc[j].mode == WRITE;
            r |= acc[j].mode == READ;
            c |= acc[j].mode == COMMUTATIVE;
            ++j;
        }
        best = std::max(best, w + uint32_t(r) + uint32_t(c));
        i = j;
    }
    return best;
}

std::vector<double> gas_costs(const Tx* tx, size_t n) {
    std::vector<double> c(n);
    for (size_t i = 0; i < n; ++i) c[i] = std::max<double>(tx[i].gas, 21000.0);
    return c;
}

ExecSim simulate(const std::vector<uint32_t>& batch, const std::vector<double>& cost,
                 uint32_t workers, double barrier) {
    ExecSim s;
    s.workers = workers;
    uint32_t B = 0;
    for (uint32_t b : batch) B = std::max(B, b + 1);
    s.batches = B;
    std::vector<std::vector<double>> by(B);
    for (size_t i = 0; i < batch.size(); ++i) {
        by[batch[i]].push_back(cost[i]);
        s.serial += cost[i];
    }
    for (auto& v : by) {
        if (v.empty()) continue;
        double span;
        if (workers == 0 || v.size() <= workers) {
            span = *std::max_element(v.begin(), v.end());
        } else {
            std::sort(v.begin(), v.end(), std::greater<double>());
            std::priority_queue<double, std::vector<double>, std::greater<double>> q;
            for (uint32_t w = 0; w < workers; ++w) q.push(0.0);
            for (double x : v) {
                double t = q.top();
                q.pop();
                q.push(t + x);
            }
            span = 0;
            while (!q.empty()) {
                span = std::max(span, q.top());
                q.pop();
            }
        }
        s.makespan += span + barrier;
    }
    s.speedup = s.makespan > 0 ? s.serial / s.makespan : 0;
    s.utilisation = workers && s.makespan > 0 ? s.serial / (double(workers) * s.makespan) : 0;
    return s;
}

}  // namespace conduit
