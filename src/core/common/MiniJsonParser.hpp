#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace hftrec::json {

class MiniJsonParser {
  public:
    explicit MiniJsonParser(std::string_view json) noexcept : json_(json) {}

    bool finish() noexcept {
        skipWs_();
        return pos_ == json_.size();
    }

    bool parseObjectStart() noexcept {
        skipWs_();
        if (!consume_('{')) return false;
        skipWs_();
        return true;
    }

    bool parseObjectEnd() noexcept {
        skipWs_();
        return consume_('}');
    }

    bool parseArrayStart() noexcept {
        skipWs_();
        if (!consume_('[')) return false;
        skipWs_();
        return true;
    }

    bool parseArrayEnd() noexcept {
        skipWs_();
        return consume_(']');
    }

    bool parseComma() noexcept {
        skipWs_();
        return consume_(',');
    }

    bool peek(char ch) noexcept {
        skipWs_();
        return pos_ < json_.size() && json_[pos_] == ch;
    }

    bool parseString(std::string& out) noexcept {
        skipWs_();
        if (pos_ >= json_.size() || json_[pos_] != '"') return false;
        ++pos_;
        out.clear();
        while (pos_ < json_.size()) {
            const char ch = json_[pos_++];
            if (ch == '"') return true;
            if (ch != '\\') {
                out.push_back(ch);
                continue;
            }
            if (pos_ >= json_.size()) return false;
            const char esc = json_[pos_++];
            switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    unsigned value = 0;
                    for (int i = 0; i < 4; ++i) {
                        if (pos_ >= json_.size()) return false;
                        value <<= 4;
                        const char hex = json_[pos_++];
                        if (hex >= '0' && hex <= '9') value |= static_cast<unsigned>(hex - '0');
                        else if (hex >= 'a' && hex <= 'f') value |= static_cast<unsigned>(hex - 'a' + 10);
                        else if (hex >= 'A' && hex <= 'F') value |= static_cast<unsigned>(hex - 'A' + 10);
                        else return false;
                    }
                    if (value <= 0x7f) out.push_back(static_cast<char>(value));
                    else if (value <= 0x7ff) {
                        out.push_back(static_cast<char>(0xc0u | ((value >> 6) & 0x1fu)));
                        out.push_back(static_cast<char>(0x80u | (value & 0x3fu)));
                    } else {
                        out.push_back(static_cast<char>(0xe0u | ((value >> 12) & 0x0fu)));
                        out.push_back(static_cast<char>(0x80u | ((value >> 6) & 0x3fu)));
                        out.push_back(static_cast<char>(0x80u | (value & 0x3fu)));
                    }
                    break;
                }
                default:
                    return false;
            }
        }
        return false;
    }

    bool parseKey(std::string& key) noexcept {
        if (!parseString(key)) return false;
        skipWs_();
        return consume_(':');
    }

    bool parseInt64(std::int64_t& out) noexcept {
        skipWs_();
        if (pos_ >= json_.size()) return false;

        bool neg = false;
        if (json_[pos_] == '-') {
            neg = true;
            ++pos_;
        }
        if (pos_ >= json_.size() || json_[pos_] < '0' || json_[pos_] > '9') return false;

        std::uint64_t value = 0;
        while (pos_ < json_.size() && json_[pos_] >= '0' && json_[pos_] <= '9') {
            const std::uint64_t digit = static_cast<std::uint64_t>(json_[pos_] - '0');
            if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10u) return false;
            value = value * 10u + digit;
            ++pos_;
        }

        if (neg) {
            const auto limit = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1u;
            if (value > limit) return false;
            out = value == limit ? std::numeric_limits<std::int64_t>::min() : -static_cast<std::int64_t>(value);
            return true;
        }

        if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) return false;
        out = static_cast<std::int64_t>(value);
        return true;
    }

    bool parseUInt64(std::uint64_t& out) noexcept {
        std::int64_t value = 0;
        if (!parseInt64(value) || value < 0) return false;
        out = static_cast<std::uint64_t>(value);
        return true;
    }

    bool parseBool(bool& out) noexcept {
        skipWs_();
        if (consumeLiteral_("true")) {
            out = true;
            return true;
        }
        if (consumeLiteral_("false")) {
            out = false;
            return true;
        }
        return false;
    }

    bool skipValue() noexcept {
        skipWs_();
        if (pos_ >= json_.size()) return false;
        switch (json_[pos_]) {
            case '{':
                return skipObject_();
            case '[':
                return skipArray_();
            case '"': {
                std::string ignored;
                return parseString(ignored);
            }
            case 't':
                return consumeLiteral_("true");
            case 'f':
                return consumeLiteral_("false");
            case 'n':
                return consumeLiteral_("null");
            default: {
                std::int64_t ignored = 0;
                return parseInt64(ignored);
            }
        }
    }

  private:
    void skipWs_() noexcept {
        while (pos_ < json_.size()) {
            const char ch = json_[pos_];
            if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') break;
            ++pos_;
        }
    }

    bool consume_(char expected) noexcept {
        if (pos_ >= json_.size() || json_[pos_] != expected) return false;
        ++pos_;
        return true;
    }

    bool consumeLiteral_(std::string_view literal) noexcept {
        if (json_.substr(pos_, literal.size()) != literal) return false;
        pos_ += literal.size();
        return true;
    }

    bool skipObject_() noexcept {
        if (!parseObjectStart()) return false;
        if (peek('}')) return parseObjectEnd();
        std::string key;
        do {
            if (!parseKey(key)) return false;
            if (!skipValue()) return false;
            if (peek('}')) break;
        } while (parseComma());
        return parseObjectEnd();
    }

    bool skipArray_() noexcept {
        if (!parseArrayStart()) return false;
        if (peek(']')) return parseArrayEnd();
        do {
            if (!skipValue()) return false;
            if (peek(']')) break;
        } while (parseComma());
        return parseArrayEnd();
    }

    std::string_view json_{};
    std::size_t pos_{0};
};

}  // namespace hftrec::json

