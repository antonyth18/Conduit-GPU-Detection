#include "conduit/corpus.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <fstream>
#include <stdexcept>

#include "conduit/json.hpp"
#include "conduit/sha256.hpp"

namespace conduit {

MappedFile::MappedFile(const std::string& path) {
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) throw std::runtime_error("cannot open " + path);
    struct stat st;
    if (::fstat(fd, &st) != 0) {
        ::close(fd);
        throw std::runtime_error("cannot stat " + path);
    }
    size_ = size_t(st.st_size);
    if (size_ > 0) {
        data_ = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, 0);
        if (data_ == MAP_FAILED) {
            data_ = nullptr;
            ::close(fd);
            throw std::runtime_error("cannot mmap " + path);
        }
    }
    ::close(fd);
}

MappedFile::~MappedFile() {
    if (data_) ::munmap(data_, size_);
}

Corpus::Corpus(const std::string& dir)
    : dir_(dir), tx_(dir + "/tx.bin"), blocks_(dir + "/blocks.bin") {
    const uint16_t probe = 1;
    if (*reinterpret_cast<const uint8_t*>(&probe) != 1)
        throw std::runtime_error("corpus records are little-endian; big-endian host");
    if (tx_.size() % sizeof(Tx) != 0) throw std::runtime_error("tx.bin size not a multiple of 40");
    if (blocks_.size() % sizeof(Block) != 0)
        throw std::runtime_error("blocks.bin size not a multiple of 12");

    json::Value meta = json::parse_file(dir + "/meta.json");
    for (const auto& d : meta["days"].arr) {
        days.push_back({d["date"].as_str(), int(d["day_id"].as_int()),
                        d["transactions"].as_u64(), d["blocks"].as_u64(),
                        d["first_tx_index"].as_u64()});
    }
    if (meta["total_transactions"].as_u64() != size())
        throw std::runtime_error("meta.json total_transactions disagrees with tx.bin");

    std::ifstream f(dir + "/selectors.txt");
    if (!f) throw std::runtime_error("cannot open selectors.txt");
    std::string line;
    while (std::getline(f, line)) selectors.push_back(line);
}

const SuperBlock& Manifest::get(const std::string& id) const {
    for (const auto& s : superblocks)
        if (s.id == id) return s;
    throw std::runtime_error("no super-block " + id);
}

std::vector<const SuperBlock*> Manifest::entries(const std::string& date, uint32_t target) const {
    std::vector<const SuperBlock*> out;
    for (const auto& s : superblocks)
        if (s.date == date && s.target == target) out.push_back(&s);
    return out;
}

std::vector<std::string> Manifest::dates() const {
    std::vector<std::string> out;
    for (const auto& s : superblocks)
        if (out.empty() || out.back() != s.date) {
            bool seen = false;
            for (const auto& d : out) seen |= d == s.date;
            if (!seen) out.push_back(s.date);
        }
    return out;
}

Manifest load_manifest(const std::string& path, const Corpus& corpus, bool verify) {
    json::Value m = json::parse_file(path);
    Manifest out;
    out.tx_bin_sha256 = m["corpus"]["tx_bin_sha256"].as_str();
    out.total_transactions = m["corpus"]["total_transactions"].as_u64();
    for (const auto& t : m["targets"].arr) out.targets.push_back(uint32_t(t.as_u64()));
    for (const auto& e : m["superblocks"].arr) {
        SuperBlock s;
        s.id = e["id"].as_str();
        s.date = e["date"].as_str();
        s.day_id = int(e["day_id"].as_int());
        s.target = uint32_t(e["target"].as_u64());
        s.sample = uint32_t(e["sample"].as_u64());
        s.tx_start = e["tx_start"].as_u64();
        s.tx_count = e["tx_count"].as_u64();
        s.block_first = uint32_t(e["block_first"].as_u64());
        s.block_last = uint32_t(e["block_last"].as_u64());
        s.block_count = uint32_t(e["block_count"].as_u64());
        s.block_index_start = e["block_index_start"].as_u64();
        s.missing_blocks = uint32_t(e["missing_blocks"].as_u64());
        s.max_sender_tx = uint32_t(e["max_sender_tx"].as_u64());
        if (s.tx_start + s.tx_count > corpus.size())
            throw std::runtime_error("super-block " + s.id + " runs past tx.bin");
        out.superblocks.push_back(std::move(s));
    }
    if (out.total_transactions != corpus.size())
        throw std::runtime_error("manifest built for a corpus of a different size");
    if (verify) {
        std::string have = sha256_hex(corpus.tx(), corpus.size() * sizeof(Tx));
        if (have != out.tx_bin_sha256)
            throw std::runtime_error("manifest built from tx.bin " + out.tx_bin_sha256.substr(0, 12) +
                                     "..., corpus is " + have.substr(0, 12) +
                                     "...; rebuild the manifest");
    }
    return out;
}

}  // namespace conduit
