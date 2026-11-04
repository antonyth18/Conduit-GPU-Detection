#include "conduit/json.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace conduit::json {

namespace {

class Parser {
public:
    explicit Parser(const std::string& s) : s_(s) {}

    Value parse_document() {
        Value v = parse_value();
        skip_ws();
        if (pos_ != s_.size()) fail("trailing characters");
        return v;
    }

private:
    const std::string& s_;
    size_t pos_ = 0;

    [[noreturn]] void fail(const std::string& what) const {
        throw std::runtime_error("json: " + what + " at offset " + std::to_string(pos_));
    }

    void skip_ws() {
        while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_]))) ++pos_;
    }

    char peek() {
        skip_ws();
        if (pos_ >= s_.size()) fail("unexpected end");
        return s_[pos_];
    }

    void expect(char c) {
        if (peek() != c) fail(std::string("expected '") + c + "'");
        ++pos_;
    }

    bool consume_literal(const char* lit) {
        size_t n = std::char_traits<char>::length(lit);
        if (s_.compare(pos_, n, lit) == 0) {
            pos_ += n;
            return true;
        }
        return false;
    }

    Value parse_value() {
        char c = peek();
        Value v;
        if (c == '{') {
            v.type = Value::Type::Object;
            ++pos_;
            if (peek() == '}') { ++pos_; return v; }
            while (true) {
                if (peek() != '"') fail("expected key");
                std::string key = parse_string();
                expect(':');
                v.obj.emplace_back(std::move(key), parse_value());
                if (peek() == ',') { ++pos_; continue; }
                expect('}');
                return v;
            }
        }
        if (c == '[') {
            v.type = Value::Type::Array;
            ++pos_;
            if (peek() == ']') { ++pos_; return v; }
            while (true) {
                v.arr.push_back(parse_value());
                if (peek() == ',') { ++pos_; continue; }
                expect(']');
                return v;
            }
        }
        if (c == '"') {
            v.type = Value::Type::String;
            v.str = parse_string();
            return v;
        }
        if (consume_literal("true")) { v.type = Value::Type::Bool; v.boolean = true; return v; }
        if (consume_literal("false")) { v.type = Value::Type::Bool; return v; }
        if (consume_literal("null")) return v;
        return parse_number();
    }

    Value parse_number() {
        size_t start = pos_;
        bool integral = true;
        if (s_[pos_] == '-') ++pos_;
        while (pos_ < s_.size()) {
            char c = s_[pos_];
            if (std::isdigit(static_cast<unsigned char>(c))) { ++pos_; continue; }
            if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
                integral = false;
                ++pos_;
                continue;
            }
            break;
        }
        if (pos_ == start) fail("unexpected character");
        std::string tok = s_.substr(start, pos_ - start);
        Value v;
        v.type = Value::Type::Number;
        v.number = std::strtod(tok.c_str(), nullptr);
        if (integral) {
            v.is_int = true;
            v.integer = std::strtoll(tok.c_str(), nullptr, 10);
        }
        return v;
    }

    static void append_utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += char(cp);
        } else if (cp < 0x800) {
            out += char(0xC0 | (cp >> 6));
            out += char(0x80 | (cp & 0x3F));
        } else {
            out += char(0xE0 | (cp >> 12));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        }
    }

    std::string parse_string() {
        expect('"');
        std::string out;
        while (true) {
            if (pos_ >= s_.size()) fail("unterminated string");
            char c = s_[pos_++];
            if (c == '"') return out;
            if (c != '\\') { out += c; continue; }
            if (pos_ >= s_.size()) fail("bad escape");
            char e = s_[pos_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    if (pos_ + 4 > s_.size()) fail("bad \\u escape");
                    unsigned cp = std::stoul(s_.substr(pos_, 4), nullptr, 16);
                    pos_ += 4;
                    append_utf8(out, cp);
                    break;
                }
                default: fail("bad escape");
            }
        }
    }
};

}  // namespace

bool Value::has(const std::string& key) const {
    for (const auto& kv : obj)
        if (kv.first == key) return true;
    return false;
}

const Value& Value::operator[](const std::string& key) const {
    for (const auto& kv : obj)
        if (kv.first == key) return kv.second;
    throw std::runtime_error("json: missing key '" + key + "'");
}

const Value& Value::operator[](size_t i) const {
    if (type != Type::Array || i >= arr.size()) throw std::runtime_error("json: bad index");
    return arr[i];
}

size_t Value::size() const {
    return type == Type::Array ? arr.size() : type == Type::Object ? obj.size() : 0;
}

int64_t Value::as_int() const {
    if (type != Type::Number || !is_int) throw std::runtime_error("json: not an integer");
    return integer;
}

uint64_t Value::as_u64() const {
    int64_t v = as_int();
    if (v < 0) throw std::runtime_error("json: negative where unsigned expected");
    return uint64_t(v);
}

const std::string& Value::as_str() const {
    if (type != Type::String) throw std::runtime_error("json: not a string");
    return str;
}

Value parse(const std::string& text) { return Parser(text).parse_document(); }

Value parse_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return parse(ss.str());
}

}  // namespace conduit::json
