// Read/write-set inference: a port of access_sets.py. Output must be
// byte-identical to the Python implementation (same keys, same per-tx order,
// same modes); tools/xcheck_access.py enforces that.
//
// Key layout (prefix code over interned ids, collision-free by construction):
//   bit 63 = 1  ALLOWANCE      [62:42] token  [41:21] owner  [20:0] spender
//   bit 63 = 0  [62:60] kind   [59:42] zero   [41:21] a      [20:0] b
//      kind 0   ACCOUNT(a)   kind 1 NONCE(a)   kind 2 ETH_BAL(a)
//      kind 3   TOKEN_BAL(a = token, b = holder)
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "conduit/corpus.hpp"

namespace conduit {

// Access modes are bit flags, as in Python: duplicates within one tx are
// OR-ed, then any WRITE, or READ plus COMMUTATIVE, resolves to WRITE.
constexpr uint8_t READ = 1;
constexpr uint8_t COMMUTATIVE = 2;
constexpr uint8_t WRITE = 4;

constexpr int ID_BITS = 21;
constexpr uint32_t MAX_ID = (1u << ID_BITS) - 2;

constexpr uint64_t KIND_ACCOUNT = 0;
constexpr uint64_t KIND_NONCE = 1;
constexpr uint64_t KIND_ETH_BAL = 2;
constexpr uint64_t KIND_TOKEN_BAL = 3;

constexpr uint8_t CAT_FALLBACK = 0;
constexpr uint8_t CAT_PLAIN = 1;
constexpr uint8_t CAT_TRANSFER = 2;
constexpr uint8_t CAT_APPROVE = 3;
constexpr uint8_t CAT_TRANSFER_FROM = 4;

enum class Policy { P1, P2, P3 };
Policy parse_policy(const std::string& name);
const char* policy_name(Policy p);

constexpr uint64_t kind_key(uint64_t kind, uint64_t a, uint64_t b = 0) {
    return (kind << 60) | (a << 21) | b;
}
constexpr uint64_t account_key(uint64_t a) { return kind_key(KIND_ACCOUNT, a); }
constexpr uint64_t nonce_key(uint64_t a) { return kind_key(KIND_NONCE, a); }
constexpr uint64_t eth_bal_key(uint64_t a) { return kind_key(KIND_ETH_BAL, a); }
constexpr uint64_t token_bal_key(uint64_t token, uint64_t holder) {
    return kind_key(KIND_TOKEN_BAL, token, holder);
}
constexpr uint64_t allowance_key(uint64_t token, uint64_t owner, uint64_t spender) {
    return (uint64_t(1) << 63) | (token << 42) | (owner << 21) | spender;
}

// Interned ids of the selectors the policies use; -1 if absent.
struct Selectors {
    int64_t plain = -1;
    int64_t transfer = -1;
    int64_t approve = -1;
    int64_t transfer_from = -1;
    explicit Selectors(const std::vector<std::string>& names);
};

// CSR, as in Python: offsets[i]..offsets[i+1] index keys/modes of tx i, keys
// ascending within each tx, each key at most once.
struct AccessSets {
    std::vector<int64_t> offsets;
    std::vector<uint64_t> keys;
    std::vector<uint8_t> modes;
    std::vector<uint8_t> category;
    size_t size() const { return offsets.empty() ? 0 : offsets.size() - 1; }
};

uint8_t classify(const Tx& t, const Selectors& sels);

// Throws if any operand id in tx[0..n) exceeds MAX_ID.
void check_ids(const Tx* tx, size_t n);

// One transaction's access set: at most MAX_TX_ACCESSES (key, mode) pairs,
// keys ascending and unique. Shared by the sequential and parallel paths so
// both emit exactly what was cross-checked against Python. Does not check ids.
constexpr int MAX_TX_ACCESSES = 7;
int tx_access(const Tx& t, Policy policy, const Selectors& sels, uint64_t keys[MAX_TX_ACCESSES],
              uint8_t modes[MAX_TX_ACCESSES], uint8_t* category);

// Throws if any operand id exceeds MAX_ID (the key layout would overflow).
AccessSets infer(const Tx* tx, size_t n, Policy policy, const Selectors& sels);

}  // namespace conduit
