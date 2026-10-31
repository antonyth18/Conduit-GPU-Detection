#include "conduit/policy.hpp"

#include <stdexcept>

namespace conduit {

Policy parse_policy(const std::string& name) {
    if (name == "P1") return Policy::P1;
    if (name == "P2") return Policy::P2;
    if (name == "P3") return Policy::P3;
    throw std::runtime_error("unknown policy " + name);
}

const char* policy_name(Policy p) {
    switch (p) {
        case Policy::P1: return "P1";
        case Policy::P2: return "P2";
        case Policy::P3: return "P3";
    }
    return "?";
}

Selectors::Selectors(const std::vector<std::string>& names) {
    // last occurrence wins, like the Python dict comprehension
    for (size_t i = 0; i < names.size(); ++i) {
        const std::string& s = names[i];
        if (s == "0x") plain = int64_t(i);
        else if (s == "0xa9059cbb") transfer = int64_t(i);
        else if (s == "0x095ea7b3") approve = int64_t(i);
        else if (s == "0x23b872dd") transfer_from = int64_t(i);
    }
}

void check_ids(const Tx* tx, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        const uint32_t ids[4] = {tx[i].frm, tx[i].to, tx[i].a1, tx[i].a2};
        for (uint32_t id : ids)
            if (id != ABSENT && id > MAX_ID)
                throw std::runtime_error("id " + std::to_string(id) + " exceeds " +
                                         std::to_string(MAX_ID) +
                                         "; the key layout needs a wider id field");
    }
}

namespace {

struct Cand {
    uint64_t key;
    uint8_t mode;
};

uint8_t resolve(uint8_t m) {
    if ((m & WRITE) || ((m & READ) && (m & COMMUTATIVE))) return WRITE;
    if (m & COMMUTATIVE) return COMMUTATIVE;
    return READ;
}

}  // namespace

// Truncation only cuts off the amount (arg3), which no key uses, so
// truncated-but-decoded calls are not fallbacks.
uint8_t classify(const Tx& t, const Selectors& sels) {
    const int64_t s = t.sel;
    const bool create = t.flags & FLAG_CREATE;
    const bool ends = t.frm != ABSENT && t.to != ABSENT;
    const bool ok = (t.flags & FLAG_DECODED) && !create && ends;
    const bool has1 = t.a1 != ABSENT;
    const bool has2 = t.a2 != ABSENT;
    if (s == sels.plain && !create && ends) return CAT_PLAIN;
    if (ok && s == sels.transfer && has1) return CAT_TRANSFER;
    if (ok && s == sels.approve && has1) return CAT_APPROVE;
    if (ok && s == sels.transfer_from && has1 && has2) return CAT_TRANSFER_FROM;
    return CAT_FALLBACK;
}

int tx_access(const Tx& t, Policy policy, const Selectors& sels, uint64_t keys[MAX_TX_ACCESSES],
              uint8_t modes[MAX_TX_ACCESSES], uint8_t* category) {
    const uint8_t bal = policy == Policy::P3 ? COMMUTATIVE : WRITE;
    const bool has_frm = t.frm != ABSENT;
    const bool has_to = t.to != ABSENT;
    const uint64_t frm = t.frm, to = t.to, a1 = t.a1, a2 = t.a2;
    Cand c[MAX_TX_ACCESSES];
    int k = 0;
    uint8_t cat = CAT_FALLBACK;

    if (policy == Policy::P1) {
        if (has_frm) c[k++] = {account_key(frm), WRITE};
        if (has_to) c[k++] = {account_key(to), WRITE};
    } else {
        cat = classify(t, sels);
        const bool fb = cat == CAT_FALLBACK;
        // sender: nonce + gas payment, or the whole account on fallback
        if (!fb && has_frm) {
            c[k++] = {nonce_key(frm), WRITE};
            c[k++] = {eth_bal_key(frm), bal};
            c[k++] = {account_key(frm), READ};
        }
        if (fb && has_frm) c[k++] = {account_key(frm), WRITE};
        // to: parent of the recipient's ETH balance or the token's slots
        if (has_to) c[k++] = {account_key(to), fb ? WRITE : READ};
        switch (cat) {
            case CAT_PLAIN:
                c[k++] = {eth_bal_key(to), bal};
                break;
            case CAT_TRANSFER:
                c[k++] = {token_bal_key(to, frm), bal};
                c[k++] = {token_bal_key(to, a1), bal};
                break;
            case CAT_APPROVE:
                c[k++] = {allowance_key(to, frm, a1), WRITE};
                break;
            case CAT_TRANSFER_FROM:
                c[k++] = {token_bal_key(to, a1), bal};
                c[k++] = {token_bal_key(to, a2), bal};
                c[k++] = {allowance_key(to, a1, frm), WRITE};
                break;
            default:
                break;
        }
    }
    if (category) *category = cat;

    // keys ascending (insertion sort on <= 7 items), duplicates merged
    for (int a = 1; a < k; ++a)
        for (int b = a; b > 0 && c[b].key < c[b - 1].key; --b) std::swap(c[b], c[b - 1]);
    int out = 0;
    for (int a = 0; a < k;) {
        uint8_t m = 0;
        int b = a;
        while (b < k && c[b].key == c[a].key) m |= c[b++].mode;
        keys[out] = c[a].key;
        modes[out] = resolve(m);
        ++out;
        a = b;
    }
    return out;
}

AccessSets infer(const Tx* tx, size_t n, Policy policy, const Selectors& sels) {
    check_ids(tx, n);
    AccessSets out;
    out.offsets.assign(n + 1, 0);
    out.category.assign(n, CAT_FALLBACK);
    out.keys.reserve(n * 5);
    out.modes.reserve(n * 5);
    uint64_t keys[MAX_TX_ACCESSES];
    uint8_t modes[MAX_TX_ACCESSES];
    for (size_t i = 0; i < n; ++i) {
        const int k = tx_access(tx[i], policy, sels, keys, modes, &out.category[i]);
        out.keys.insert(out.keys.end(), keys, keys + k);
        out.modes.insert(out.modes.end(), modes, modes + k);
        out.offsets[i + 1] = int64_t(out.keys.size());
    }
    return out;
}

}  // namespace conduit
