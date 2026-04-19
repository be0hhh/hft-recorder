#include "core/replay/JsonLineParser.hpp"

#include <cstddef>
#include <limits>
#include <string>
#include <utility>

namespace hftrec::replay {

namespace {

class JsonParser {
  public:
    explicit JsonParser(std::string_view json) noexcept : json_(json) {}

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

bool parsePricePair(JsonParser& parser, PricePair& out) noexcept {
    if (!parser.parseObjectStart()) return false;
    bool sawPrice = false;
    bool sawQty = false;
    std::string key;

    if (parser.peek('}')) return false;
    do {
        if (!parser.parseKey(key)) return false;
        if (key == "price_i64") {
            if (!parser.parseInt64(out.priceE8)) return false;
            sawPrice = true;
        } else if (key == "qty_i64") {
            if (!parser.parseInt64(out.qtyE8)) return false;
            sawQty = true;
        } else if (!parser.skipValue()) {
            return false;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());

    return parser.parseObjectEnd() && sawPrice && sawQty;
}

bool parsePairArray(JsonParser& parser, std::vector<PricePair>& out) noexcept {
    out.clear();
    if (!parser.parseArrayStart()) return false;
    if (parser.peek(']')) return parser.parseArrayEnd();
    do {
        PricePair pair{};
        if (!parsePricePair(parser, pair)) return false;
        out.push_back(std::move(pair));
        if (parser.peek(']')) break;
    } while (parser.parseComma());
    return parser.parseArrayEnd();
}

}  // namespace

Status parseTradeLine(std::string_view line, TradeRow& out) noexcept {
    out = TradeRow{};
    JsonParser parser{line};
    if (!parser.parseObjectStart()) return Status::CorruptData;

    bool sawTs = false;
    bool sawId = false;
    bool sawPrice = false;
    bool sawQty = false;
    bool sawEventIndex = false;
    bool sawSide = false;
    std::string key;
    std::string side;

    if (parser.peek('}')) return Status::CorruptData;
    do {
        if (!parser.parseKey(key)) return Status::CorruptData;
        if (key == "event_time_ns") {
            if (!parser.parseInt64(out.tsNs)) return Status::CorruptData;
            sawTs = true;
        } else if (key == "trade_id") {
            if (!parser.parseInt64(out.id)) return Status::CorruptData;
            sawId = true;
        } else if (key == "price_i64") {
            if (!parser.parseInt64(out.priceE8)) return Status::CorruptData;
            sawPrice = true;
        } else if (key == "qty_i64") {
            if (!parser.parseInt64(out.qtyE8)) return Status::CorruptData;
            sawQty = true;
        } else if (key == "event_index") {
            if (!parser.parseInt64(out.eventIndex)) return Status::CorruptData;
            sawEventIndex = true;
        } else if (key == "side") {
            if (!parser.parseString(side)) return Status::CorruptData;
            sawSide = true;
        } else if (!parser.skipValue()) {
            return Status::CorruptData;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());

    if (!parser.parseObjectEnd() || !parser.finish()) return Status::CorruptData;
    if (!sawTs || !sawId || !sawPrice || !sawQty || !sawEventIndex || !sawSide) return Status::CorruptData;

    if (side == "buy") out.sideBuy = 1u;
    else if (side == "sell") out.sideBuy = 0u;
    else return Status::CorruptData;

    return Status::Ok;
}

Status parseBookTickerLine(std::string_view line, BookTickerRow& out) noexcept {
    out = BookTickerRow{};
    JsonParser parser{line};
    if (!parser.parseObjectStart()) return Status::CorruptData;

    bool sawTs = false;
    bool sawBidPrice = false;
    bool sawBidQty = false;
    bool sawAskPrice = false;
    bool sawAskQty = false;
    bool sawEventIndex = false;
    std::string key;

    if (parser.peek('}')) return Status::CorruptData;
    do {
        if (!parser.parseKey(key)) return Status::CorruptData;
        if (key == "event_time_ns") {
            if (!parser.parseInt64(out.tsNs)) return Status::CorruptData;
            sawTs = true;
        } else if (key == "best_bid_price_i64") {
            if (!parser.parseInt64(out.bidPriceE8)) return Status::CorruptData;
            sawBidPrice = true;
        } else if (key == "best_bid_qty_i64") {
            if (!parser.parseInt64(out.bidQtyE8)) return Status::CorruptData;
            sawBidQty = true;
        } else if (key == "best_ask_price_i64") {
            if (!parser.parseInt64(out.askPriceE8)) return Status::CorruptData;
            sawAskPrice = true;
        } else if (key == "best_ask_qty_i64") {
            if (!parser.parseInt64(out.askQtyE8)) return Status::CorruptData;
            sawAskQty = true;
        } else if (key == "event_index") {
            if (!parser.parseInt64(out.eventIndex)) return Status::CorruptData;
            sawEventIndex = true;
        } else if (!parser.skipValue()) {
            return Status::CorruptData;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());

    if (!parser.parseObjectEnd() || !parser.finish()) return Status::CorruptData;
    if (!sawTs || !sawBidPrice || !sawBidQty || !sawAskPrice || !sawAskQty || !sawEventIndex) {
        return Status::CorruptData;
    }
    return Status::Ok;
}

Status parseDepthLine(std::string_view line, DepthRow& out) noexcept {
    out = DepthRow{};
    JsonParser parser{line};
    if (!parser.parseObjectStart()) return Status::CorruptData;

    bool sawTs = false;
    bool sawFirst = false;
    bool sawFinal = false;
    bool sawEventIndex = false;
    bool sawBids = false;
    bool sawAsks = false;
    std::string key;

    if (parser.peek('}')) return Status::CorruptData;
    do {
        if (!parser.parseKey(key)) return Status::CorruptData;
        if (key == "event_time_ns") {
            if (!parser.parseInt64(out.tsNs)) return Status::CorruptData;
            sawTs = true;
        } else if (key == "first_update_id") {
            if (!parser.parseInt64(out.firstUpdateId)) return Status::CorruptData;
            sawFirst = true;
        } else if (key == "final_update_id") {
            if (!parser.parseInt64(out.finalUpdateId)) return Status::CorruptData;
            sawFinal = true;
        } else if (key == "event_index") {
            if (!parser.parseInt64(out.eventIndex)) return Status::CorruptData;
            sawEventIndex = true;
        } else if (key == "bids") {
            if (!parsePairArray(parser, out.bids)) return Status::CorruptData;
            sawBids = true;
        } else if (key == "asks") {
            if (!parsePairArray(parser, out.asks)) return Status::CorruptData;
            sawAsks = true;
        } else if (!parser.skipValue()) {
            return Status::CorruptData;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());

    if (!parser.parseObjectEnd() || !parser.finish()) return Status::CorruptData;
    if (!sawTs || !sawFirst || !sawFinal || !sawEventIndex || !sawBids || !sawAsks) {
        return Status::CorruptData;
    }
    return Status::Ok;
}

Status parseSnapshotDocument(std::string_view doc, SnapshotDocument& out) noexcept {
    out = SnapshotDocument{};
    JsonParser parser{doc};
    if (!parser.parseObjectStart()) return Status::CorruptData;

    bool sawTs = false;
    bool sawIndex = false;
    bool sawBids = false;
    bool sawAsks = false;
    std::string key;

    if (parser.peek('}')) return Status::CorruptData;
    do {
        if (!parser.parseKey(key)) return Status::CorruptData;
        if (key == "snapshot_time_ns") {
            if (!parser.parseInt64(out.tsNs)) return Status::CorruptData;
            sawTs = true;
        } else if (key == "snapshot_index") {
            if (!parser.parseInt64(out.snapshotIndex)) return Status::CorruptData;
            sawIndex = true;
        } else if (key == "bids") {
            if (!parsePairArray(parser, out.bids)) return Status::CorruptData;
            sawBids = true;
        } else if (key == "asks") {
            if (!parsePairArray(parser, out.asks)) return Status::CorruptData;
            sawAsks = true;
        } else if (!parser.skipValue()) {
            return Status::CorruptData;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());

    if (!parser.parseObjectEnd() || !parser.finish()) return Status::CorruptData;
    if (!sawTs || !sawIndex || !sawBids || !sawAsks) return Status::CorruptData;
    return Status::Ok;
}

}  // namespace hftrec::replay
