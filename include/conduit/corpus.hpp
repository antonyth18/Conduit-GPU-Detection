// Corpus loader: memory-maps corpus/tx.bin and corpus/blocks.bin, reads
// meta.json, selectors.txt and the super-block manifest. Nothing is copied;
// a super-block is a (tx_start, tx_count) range into the mapped records.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace conduit {

constexpr uint32_t ABSENT = 0xFFFFFFFFu;

constexpr uint8_t FLAG_DECODED = 1;
constexpr uint8_t FLAG_TRUNC = 2;
constexpr uint8_t FLAG_MALFORMED = 4;
constexpr uint8_t FLAG_CREATE = 8;

// Fixed 40-byte little-endian record, identical to ingest.py's layout. The
// fields fall on natural alignment, so no packing pragma is needed.
struct Tx {
    uint32_t block;
    uint16_t idx;
    uint16_t sel;
    uint32_t frm;
    uint32_t to;
    uint32_t a1;
    uint32_t a2;
    uint64_t value_gwei;
    uint32_t gas;
    uint8_t status;
    uint8_t ttype;
    uint8_t flags;
    uint8_t day;
};
static_assert(sizeof(Tx) == 40, "Tx must match the 40-byte corpus record");

struct Block {
    uint32_t block;
    uint32_t start;
    uint32_t count;
};
static_assert(sizeof(Block) == 12, "Block must match the 12-byte record");

// Read-only memory map, unmapped on destruction.
class MappedFile {
public:
    explicit MappedFile(const std::string& path);
    ~MappedFile();
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    const void* data() const { return data_; }
    size_t size() const { return size_; }

private:
    void* data_ = nullptr;
    size_t size_ = 0;
};

struct Day {
    std::string date;
    int day_id;
    uint64_t transactions;
    uint64_t blocks;
    uint64_t first_tx_index;
};

class Corpus {
public:
    explicit Corpus(const std::string& dir);

    const Tx* tx() const { return static_cast<const Tx*>(tx_.data()); }
    size_t size() const { return tx_.size() / sizeof(Tx); }
    const Block* blocks() const { return static_cast<const Block*>(blocks_.data()); }
    size_t num_blocks() const { return blocks_.size() / sizeof(Block); }
    const std::string& dir() const { return dir_; }

    std::vector<Day> days;
    std::vector<std::string> selectors;   // line N = selector with id N

private:
    std::string dir_;
    MappedFile tx_;
    MappedFile blocks_;
};

struct SuperBlock {
    std::string id;
    std::string date;
    int day_id;
    uint32_t target;
    uint32_t sample;
    uint64_t tx_start;
    uint64_t tx_count;
    uint32_t block_first;
    uint32_t block_last;
    uint32_t block_count;
    uint64_t block_index_start;
    uint32_t missing_blocks;
    uint32_t max_sender_tx;
};

struct Manifest {
    std::string tx_bin_sha256;
    uint64_t total_transactions;
    std::vector<uint32_t> targets;
    std::vector<SuperBlock> superblocks;   // manifest order

    const SuperBlock& get(const std::string& id) const;
    // Entries of one (date, target), in sample order.
    std::vector<const SuperBlock*> entries(const std::string& date, uint32_t target) const;
    // Dates in manifest order.
    std::vector<std::string> dates() const;
};

// Loads the manifest; with verify, refuses one built from a different tx.bin.
Manifest load_manifest(const std::string& path, const Corpus& corpus, bool verify = true);

}  // namespace conduit
