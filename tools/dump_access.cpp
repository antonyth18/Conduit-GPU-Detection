// Dumps access records for the cross-language check (tools/xcheck_access.py).
//
//   dump_access --policy P2 --out FILE [--corpus DIR] [--manifest PATH --id ID]
//
// Without --id, the whole corpus. Format (little-endian), shared with
// tools/dump_access.py:
//   "CDXA" | u32 version=1 | u64 n | u64 m | i64 offsets[n+1] | u64 keys[m] | u8 modes[m]
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

#include "conduit/corpus.hpp"
#include "conduit/policy.hpp"

using namespace conduit;

static void write_or_die(std::FILE* f, const void* p, size_t bytes) {
    if (bytes && std::fwrite(p, 1, bytes, f) != bytes) {
        std::cerr << "write failed\n";
        std::exit(1);
    }
}

int main(int argc, char** argv) {
    std::string corpus_dir = "corpus", manifest_path = "superblocks.json";
    std::string policy, out, id;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { std::cerr << a << " needs a value\n"; std::exit(2); }
            return argv[++i];
        };
        if (a == "--corpus") corpus_dir = next();
        else if (a == "--manifest") manifest_path = next();
        else if (a == "--policy") policy = next();
        else if (a == "--out") out = next();
        else if (a == "--id") id = next();
        else { std::cerr << "unknown argument " << a << "\n"; return 2; }
    }
    if (policy.empty() || out.empty()) {
        std::cerr << "usage: dump_access --policy P1|P2|P3 --out FILE [--id ID]\n";
        return 2;
    }
    try {
        Corpus corpus(corpus_dir);
        Selectors sels(corpus.selectors);
        const Tx* tx = corpus.tx();
        size_t n = corpus.size();
        if (!id.empty()) {
            Manifest m = load_manifest(manifest_path, corpus);
            const SuperBlock& sb = m.get(id);
            tx += sb.tx_start;
            n = sb.tx_count;
        }
        AccessSets s = infer(tx, n, parse_policy(policy), sels);

        std::FILE* f = std::fopen(out.c_str(), "wb");
        if (!f) { std::cerr << "cannot open " << out << "\n"; return 1; }
        const uint32_t version = 1;
        const uint64_t nn = s.size(), mm = s.keys.size();
        write_or_die(f, "CDXA", 4);
        write_or_die(f, &version, 4);
        write_or_die(f, &nn, 8);
        write_or_die(f, &mm, 8);
        write_or_die(f, s.offsets.data(), s.offsets.size() * 8);
        write_or_die(f, s.keys.data(), s.keys.size() * 8);
        write_or_die(f, s.modes.data(), s.modes.size());
        std::fclose(f);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
