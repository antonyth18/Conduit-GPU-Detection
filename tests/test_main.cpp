// Phase 5 unit tests. Hand-rolled framework: TEST registers a function, CHECK
// records a failure and carries on. Run: build/conduit_tests
//
// The reduced edge rule is checked against a brute-force full-edge reference
// written here from the conflict definition alone.
#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "conduit/json.hpp"
#include "conduit/pipeline.hpp"
#include "conduit/sha256.hpp"

using namespace conduit;

namespace {

struct TestCase {
    const char* name;
    std::function<void()> fn;
};
std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}
struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, fn}); }
};
int failures = 0;

#define TEST(name)                                       \
    static void name();                                  \
    static Registrar reg_##name(#name, name);            \
    static void name()

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            ++failures;                                                          \
            std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond  \
                      << "\n";                                                   \
        }                                                                        \
    } while (0)

using Access = std::vector<std::pair<uint64_t, uint8_t>>;   // (key, mode), keys unique

AccessSets make_sets(const std::vector<Access>& acc) {
    AccessSets s;
    s.offsets.push_back(0);
    for (const auto& a : acc) {
        for (const auto& [k, m] : a) {
            s.keys.push_back(k);
            s.modes.push_back(m);
        }
        s.offsets.push_back(int64_t(s.keys.size()));
        s.category.push_back(CAT_FALLBACK);
    }
    return s;
}

struct Out {
    std::vector<std::pair<uint32_t, uint32_t>> edges;
    std::vector<uint32_t> levels;
    EdgeStats stats;
};

Out pipeline(const std::vector<Access>& acc) {
    std::vector<Record> r = extract(make_sets(acc));
    sort_records(r);
    Out o;
    std::vector<uint64_t> e = detect(r, &o.stats);
    Graph g = build_csr(uint32_t(acc.size()), e);
    for (uint32_t u = 0; u < g.n; ++u)
        for (uint64_t k = g.row_offsets[u]; k < g.row_offsets[u + 1]; ++k)
            o.edges.push_back({u, g.cols[k]});
    o.levels = level(g);
    return o;
}

// Reference: every conflicting pair, longest path by definition.
bool conflict(uint8_t a, uint8_t b) {
    if (a == READ && b == READ) return false;
    if (a == COMMUTATIVE && b == COMMUTATIVE) return false;
    return true;
}

Out brute(const std::vector<Access>& acc) {
    Out o;
    const size_t n = acc.size();
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j) {
            bool c = false;
            for (const auto& [ka, ma] : acc[i])
                for (const auto& [kb, mb] : acc[j])
                    if (ka == kb && conflict(ma, mb)) c = true;
            if (c) o.edges.push_back({uint32_t(i), uint32_t(j)});
        }
    o.levels.assign(n, 0);
    for (const auto& [i, j] : o.edges) o.levels[j] = std::max(o.levels[j], o.levels[i] + 1);
    return o;
}

using E = std::vector<std::pair<uint32_t, uint32_t>>;
using L = std::vector<uint32_t>;
constexpr uint8_t R = READ, C = COMMUTATIVE, W = WRITE;

bool subset(const E& a, const E& b) {
    size_t j = 0;
    for (const auto& e : a) {
        while (j < b.size() && b[j] < e) ++j;
        if (j == b.size() || b[j] != e) return false;
    }
    return true;
}

void same_levels_as_brute(const std::vector<Access>& acc) {
    Out p = pipeline(acc), b = brute(acc);
    CHECK(p.levels == b.levels);
    CHECK(subset(p.edges, b.edges));   // reduced edges are true conflicts
}

}  // namespace

// ---- hand-built graphs -------------------------------------------------------

TEST(two_independent) {
    Out o = pipeline({{{1, W}}, {{2, W}}});
    CHECK(o.edges.empty());
    CHECK((o.levels == L{0, 0}));
}

TEST(read_read) {
    Out o = pipeline({{{1, R}}, {{1, R}}});
    CHECK(o.edges.empty());
    CHECK((o.levels == L{0, 0}));
}

TEST(read_write_and_write_read) {
    CHECK((pipeline({{{1, R}}, {{1, W}}}).edges == E{{0, 1}}));
    CHECK((pipeline({{{1, W}}, {{1, R}}}).levels == L{0, 1}));
}

TEST(two_commutative) {
    Out o = pipeline({{{1, C}}, {{1, C}}});
    CHECK(o.edges.empty());
    CHECK((o.levels == L{0, 0}));
}

TEST(commutative_with_read_and_write) {
    CHECK((pipeline({{{1, C}}, {{1, R}}}).levels == L{0, 1}));
    CHECK((pipeline({{{1, R}}, {{1, C}}}).levels == L{0, 1}));
    CHECK((pipeline({{{1, W}}, {{1, C}}}).levels == L{0, 1}));
    CHECK((pipeline({{{1, C}}, {{1, W}}}).levels == L{0, 1}));
}

TEST(three_transaction_chain) {
    std::vector<Access> acc = {{{1, W}}, {{1, W}, {2, W}}, {{2, W}}};
    Out o = pipeline(acc);
    CHECK((o.edges == E{{0, 1}, {1, 2}}));
    CHECK((o.levels == L{0, 1, 2}));
    same_levels_as_brute(acc);
}

TEST(conflict_only_with_one_far_earlier) {
    std::vector<Access> acc = {{{1, W}}};
    for (int i = 0; i < 50; ++i) acc.push_back({{uint64_t(100 + i), W}});
    acc.push_back({{1, R}});
    Out o = pipeline(acc);
    CHECK((o.edges == E{{0, 51}}));
    CHECK(o.levels[51] == 1);
    for (int i = 1; i <= 50; ++i) CHECK(o.levels[i] == 0);
    same_levels_as_brute(acc);
}

TEST(key_touched_by_many_writers) {
    // like the hot sender's nonce: a chain, n-1 edges instead of n(n-1)/2
    std::vector<Access> acc(1500, Access{{7, W}});
    Out o = pipeline(acc);
    CHECK(o.edges.size() == 1499);
    CHECK(o.levels.back() == 1499);
    CHECK(o.stats.emitted == 1499);
}

TEST(many_readers_then_writer_then_readers) {
    std::vector<Access> acc(300, Access{{7, R}});
    acc.push_back({{7, W}});
    for (int i = 0; i < 300; ++i) acc.push_back({{7, R}});
    Out o = pipeline(acc);
    CHECK(o.edges.size() == 600);
    CHECK(o.levels[299] == 0);
    CHECK(o.levels[300] == 1);
    CHECK(o.levels[600] == 2);
    same_levels_as_brute(acc);
}

TEST(commutative_run_bracketed_by_reads) {
    std::vector<Access> acc = {{{5, R}}, {{5, C}}, {{5, C}}, {{5, C}}, {{5, R}}};
    Out o = pipeline(acc);
    CHECK((o.levels == L{0, 1, 1, 1, 2}));
    CHECK((o.edges == E{{0, 1}, {0, 2}, {0, 3}, {1, 4}, {2, 4}, {3, 4}}));
    CHECK(o.stats.read_comm_edges == 6);
    same_levels_as_brute(acc);
}

TEST(read_run_meets_commutative_run_is_bipartite) {
    std::vector<Access> acc = {{{5, R}}, {{5, R}}, {{5, C}}, {{5, C}}, {{5, C}}};
    Out o = pipeline(acc);
    CHECK(o.edges.size() == 6);
    CHECK(o.stats.read_comm_edges == 6);
    CHECK((o.levels == L{0, 0, 1, 1, 1}));
}

TEST(commutative_run_between_writes) {
    std::vector<Access> acc = {{{5, W}}, {{5, C}}, {{5, C}}, {{5, W}}};
    Out o = pipeline(acc);
    CHECK((o.levels == L{0, 1, 1, 2}));
    CHECK(o.edges.size() == 4);
    CHECK(o.stats.read_comm_edges == 0);
}

TEST(duplicate_edges_across_keys_deduplicated) {
    std::vector<Access> acc = {{{1, W}, {2, W}, {3, W}}, {{1, W}, {2, W}, {3, W}}};
    Out o = pipeline(acc);
    CHECK(o.stats.emitted == 3);
    CHECK((o.edges == E{{0, 1}}));
}

TEST(random_cases_match_brute_force) {
    uint64_t s = 12345;   // fixed-seed LCG: deterministic
    auto rnd = [&](uint64_t m) {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return (s >> 33) % m;
    };
    const uint8_t modes[3] = {R, C, W};
    for (int t = 0; t < 400; ++t) {
        size_t n = 1 + rnd(40);
        std::vector<Access> acc(n);
        for (auto& a : acc) {
            size_t k = rnd(4);
            for (size_t j = 0; j < k; ++j) {
                uint64_t key = rnd(6);
                bool dup = false;
                for (const auto& p : a) dup |= p.first == key;
                if (!dup) a.push_back({key, modes[rnd(3)]});
            }
        }
        same_levels_as_brute(acc);
    }
}

// ---- records, keys, policies ---------------------------------------------------

TEST(record_order_groups_by_key_then_position) {
    CHECK(pack(5, 1, W) < pack(5, 2, R));
    CHECK(pack(5, 900, R) < pack(6, 0, R));
    CHECK(pack(allowance_key(0, 0, 0), 0, R) < pack(allowance_key(0, 0, 1), 0, R));
    CHECK(pack(token_bal_key(1, 2), 7, R) < pack(allowance_key(0, 0, 0), 0, R));
    Record r = pack(allowance_key(MAX_ID, 3, 4), 123456, COMMUTATIVE);
    CHECK(rec_key(r) == allowance_key(MAX_ID, 3, 4));
    CHECK(rec_tx(r) == 123456);
    CHECK(rec_mode(r) == COMMUTATIVE);
}

TEST(key_layout_matches_python) {
    CHECK(account_key(10) == (uint64_t(10) << 21));
    CHECK(nonce_key(10) == ((uint64_t(1) << 60) | (uint64_t(10) << 21)));
    CHECK(token_bal_key(20, 11) == ((uint64_t(3) << 60) | (uint64_t(20) << 21) | 11));
    CHECK(allowance_key(1, 2, 3) ==
          ((uint64_t(1) << 63) | (uint64_t(1) << 42) | (uint64_t(2) << 21) | 3));
}

namespace {
Tx make_tx(uint16_t sel, uint32_t frm, uint32_t to, uint32_t a1 = ABSENT, uint32_t a2 = ABSENT,
           uint8_t flags = 0) {
    Tx t{};
    t.sel = sel;
    t.frm = frm;
    t.to = to;
    t.a1 = a1;
    t.a2 = a2;
    t.flags = flags;
    t.status = 1;
    return t;
}
const Selectors SELS({"0x", "0xa9059cbb", "0x095ea7b3", "0x23b872dd", "0xdeadbeef"});
constexpr uint32_t ALICE = 10, BOB = 11, ROUTER = 30, USDT = 20;
}  // namespace

TEST(policy_usdt_transfer) {
    Tx t = make_tx(1, ALICE, USDT, BOB, ABSENT, FLAG_DECODED);
    AccessSets p2 = infer(&t, 1, Policy::P2, SELS);
    std::vector<std::pair<uint64_t, uint8_t>> got;
    for (size_t r = 0; r < p2.keys.size(); ++r) got.push_back({p2.keys[r], p2.modes[r]});
    std::vector<std::pair<uint64_t, uint8_t>> want = {
        {account_key(ALICE), R}, {account_key(USDT), R}, {nonce_key(ALICE), W},
        {eth_bal_key(ALICE), W}, {token_bal_key(USDT, ALICE), W}, {token_bal_key(USDT, BOB), W}};
    std::sort(want.begin(), want.end());
    CHECK(got == want);
    AccessSets p3 = infer(&t, 1, Policy::P3, SELS);
    for (size_t r = 0; r < p3.keys.size(); ++r)
        if (p3.keys[r] == token_bal_key(USDT, BOB)) CHECK(p3.modes[r] == C);
}

TEST(policy_undecodable_falls_back_to_p1) {
    Tx t = make_tx(4, ALICE, ROUTER);
    AccessSets p1 = infer(&t, 1, Policy::P1, SELS);
    AccessSets p3 = infer(&t, 1, Policy::P3, SELS);
    CHECK(p1.keys == p3.keys);
    CHECK(p1.modes == p3.modes);
    CHECK(p3.category[0] == CAT_FALLBACK);
}

TEST(policy_self_transfer_merges_keys) {
    Tx t = make_tx(1, ALICE, USDT, ALICE, ABSENT, FLAG_DECODED);
    AccessSets p3 = infer(&t, 1, Policy::P3, SELS);
    CHECK(p3.keys.size() == 5);
    for (size_t r = 1; r < p3.keys.size(); ++r) CHECK(p3.keys[r - 1] < p3.keys[r]);
}

TEST(policy_rejects_oversized_id) {
    Tx t = make_tx(0, MAX_ID + 1, BOB);
    bool threw = false;
    try {
        infer(&t, 1, Policy::P1, SELS);
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(nonce_chain_survives_p3) {
    // ALICE sends three transfers to different recipients: commutative balances,
    // but the nonce keeps them in a chain.
    std::vector<Tx> txs = {make_tx(1, ALICE, USDT, BOB, ABSENT, FLAG_DECODED),
                           make_tx(1, ALICE, USDT, ROUTER, ABSENT, FLAG_DECODED),
                           make_tx(0, ALICE, BOB)};
    Result r = run_pipeline(txs.data(), txs.size(), Policy::P3, SELS);
    CHECK((r.levels == L{0, 1, 2}));
}

TEST(pipeline_is_deterministic) {
    std::vector<Tx> txs;
    for (uint32_t i = 0; i < 500; ++i)
        txs.push_back(make_tx(i % 3 == 0 ? 0 : 1, 100 + i % 17, i % 5 == 0 ? USDT : 200 + i % 23,
                              300 + i % 11, ABSENT, FLAG_DECODED));
    for (Policy p : {Policy::P1, Policy::P2, Policy::P3}) {
        Result a = run_pipeline(txs.data(), txs.size(), p, SELS);
        Result b = run_pipeline(txs.data(), txs.size(), p, SELS);
        CHECK(a.levels == b.levels);
        CHECK(a.graph.cols == b.graph.cols);
        CHECK(a.graph.row_offsets == b.graph.row_offsets);
    }
}

// ---- support code ----------------------------------------------------------------

TEST(sha256_known_vectors) {
    CHECK(sha256_hex("", 0) ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256_hex("abc", 3) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::string m(1000, 'a');
    CHECK(sha256_hex(m.data(), m.size()) ==
          "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
}

TEST(json_parses_manifest_shapes) {
    json::Value v = json::parse(
        R"({"a": [1, 2, {"b": "x\"y"}], "seed": null, "n": -3, "f": 1.5, "t": true})");
    CHECK(v["a"].size() == 3);
    CHECK(v["a"][2]["b"].as_str() == "x\"y");
    CHECK(v["seed"].is_null());
    CHECK(v["n"].as_int() == -3);
    CHECK(!v["f"].is_int);
    CHECK(v["t"].boolean);
}

int main() {
    for (const auto& t : registry()) {
        int before = failures;
        t.fn();
        std::cout << (failures == before ? "  ok    " : "  FAILED ") << t.name << "\n";
    }
    std::cout << "\n" << registry().size() << " tests, " << failures << " failed checks\n";
    return failures ? 1 : 0;
}
