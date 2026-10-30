// Minimal JSON reader, enough for meta.json and superblocks.json. Objects keep
// their key order. Not a general-purpose parser: no streaming, no writer.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace conduit::json {

struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0;
    bool is_int = false;
    int64_t integer = 0;
    std::string str;
    std::vector<Value> arr;
    std::vector<std::pair<std::string, Value>> obj;

    bool has(const std::string& key) const;
    const Value& operator[](const std::string& key) const;   // throws if absent
    const Value& operator[](size_t i) const;
    size_t size() const;
    bool is_null() const { return type == Type::Null; }

    int64_t as_int() const;      // throws unless an integer
    uint64_t as_u64() const;     // throws unless a non-negative integer
    const std::string& as_str() const;
};

Value parse(const std::string& text);
Value parse_file(const std::string& path);

}  // namespace conduit::json
