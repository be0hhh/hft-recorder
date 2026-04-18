#include "core/replay/JsonLineParser.hpp"

#include <cstddef>

namespace hftrec::replay {

namespace {

inline bool isWs(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

void skipWs(std::string_view json, std::size_t& pos) noexcept {
    while (pos < json.size() && isWs(json[pos])) ++pos;
}

// Find the first occurrence of "<key>": in `json`; on success `pos` points at
// the first character of the value (past the colon). Scans on `"` only, so
// value strings cannot accidentally match another key.
bool seekKey(std::string_view json, std::string_view key, std::size_t& pos) noexcept {
    std::size_t off = 0;
    while (off < json.size()) {
        auto p = json.find('"', off);
        if (p == std::string_view::npos) return false;
        // Ignore escaped quote inside an earlier string value.
        if (p > 0 && json[p - 1] == '\\') { off = p + 1; continue; }
        const std::size_t keyEnd = p + 1 + key.size();
        if (keyEnd < json.size()
            && json.compare(p + 1, key.size(), key) == 0
            && json[keyEnd] == '"') {
            std::size_t q = keyEnd + 1;
            while (q < json.size() && (json[q] == ' ' || json[q] == '\t')) ++q;
            if (q < json.size() && json[q] == ':') {
                pos = q + 1;
                return true;
            }
        }
        // advance past the closing quote of whatever string this was to avoid
        // rescanning inside the string body.
        off = p + 1;
        // skip until next unescaped quote
        while (off < json.size() && json[off] != '"') {
            if (json[off] == '\\' && off + 1 < json.size()) off += 2;
            else ++off;
        }
        if (off < json.size()) ++off;
    }
    return false;
}

bool parseInt64(std::string_view json, std::size_t& pos, std::int64_t& out) noexcept {
    skipWs(json, pos);
    if (pos >= json.size()) return false;
    bool neg = false;
    if (json[pos] == '-') { neg = true; ++pos; }
    else if (json[pos] == '+') { ++pos; }
    if (pos >= json.size() || json[pos] < '0' || json[pos] > '9') return false;
    std::int64_t v = 0;
    while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
        v = v * 10 + (json[pos] - '0');
        ++pos;
    }
    out = neg ? -v : v;
    return true;
}

bool parseString(std::string_view json, std::size_t& pos, std::string_view& out) noexcept {
    skipWs(json, pos);
    if (pos >= json.size() || json[pos] != '"') return false;
    ++pos;
    std::size_t start = pos;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) { pos += 2; continue; }
        ++pos;
    }
    if (pos >= json.size()) return false;
    out = json.substr(start, pos - start);
    ++pos;
    return true;
}

bool readInt64Field(std::string_view json, std::string_view key, std::int64_t& out) noexcept {
    std::size_t pos = 0;
    if (!seekKey(json, key, pos)) return false;
    return parseInt64(json, pos, out);
}

bool readStringField(std::string_view json, std::string_view key, std::string_view& out) noexcept {
    std::size_t pos = 0;
    if (!seekKey(json, key, pos)) return false;
    return parseString(json, pos, out);
}

// Parse an array of pair-objects: [{"price_i64":N,"qty_i64":N}, ...].
// Pos must point right after the key's colon. Populates `out`.
bool parsePairObjectArray(std::string_view json, std::size_t pos, std::vector<PricePair>& out) noexcept {
    skipWs(json, pos);
    if (pos >= json.size() || json[pos] != '[') return false;
    ++pos;
    skipWs(json, pos);
    if (pos < json.size() && json[pos] == ']') return true;
    while (pos < json.size()) {
        skipWs(json, pos);
        if (pos >= json.size() || json[pos] != '{') return false;
        // find matching closing brace — no nested objects in our schema
        const std::size_t braceStart = pos;
        int depth = 0;
        std::size_t i = pos;
        for (; i < json.size(); ++i) {
            if (json[i] == '{') ++depth;
            else if (json[i] == '}') {
                --depth;
                if (depth == 0) break;
            }
        }
        if (i >= json.size()) return false;
        const std::string_view obj = json.substr(braceStart, i - braceStart + 1);
        PricePair pp{};
        if (!readInt64Field(obj, "price_i64", pp.priceE8)) return false;
        if (!readInt64Field(obj, "qty_i64",   pp.qtyE8))   return false;
        out.push_back(pp);
        pos = i + 1;
        skipWs(json, pos);
        if (pos >= json.size()) return false;
        if (json[pos] == ',') { ++pos; continue; }
        if (json[pos] == ']') return true;
        return false;
    }
    return false;
}

bool readPairArrayField(std::string_view json, std::string_view key, std::vector<PricePair>& out) noexcept {
    std::size_t pos = 0;
    if (!seekKey(json, key, pos)) return false;
    return parsePairObjectArray(json, pos, out);
}

}  // namespace

Status parseTradeLine(std::string_view line, TradeRow& out) noexcept {
    out = TradeRow{};
    if (!readInt64Field(line, "event_time_ns", out.tsNs))      return Status::CorruptData;
    if (!readInt64Field(line, "trade_id",     out.id))         return Status::CorruptData;
    if (!readInt64Field(line, "price_i64",    out.priceE8))    return Status::CorruptData;
    if (!readInt64Field(line, "qty_i64",      out.qtyE8))      return Status::CorruptData;
    if (!readInt64Field(line, "event_index",  out.eventIndex)) return Status::CorruptData;
    std::string_view side{};
    if (!readStringField(line, "side", side)) return Status::CorruptData;
    out.sideBuy = (side == "buy") ? 1u : 0u;
    return Status::Ok;
}

Status parseBookTickerLine(std::string_view line, BookTickerRow& out) noexcept {
    out = BookTickerRow{};
    if (!readInt64Field(line, "event_time_ns",        out.tsNs))        return Status::CorruptData;
    if (!readInt64Field(line, "best_bid_price_i64",  out.bidPriceE8))  return Status::CorruptData;
    if (!readInt64Field(line, "best_bid_qty_i64",    out.bidQtyE8))    return Status::CorruptData;
    if (!readInt64Field(line, "best_ask_price_i64",  out.askPriceE8))  return Status::CorruptData;
    if (!readInt64Field(line, "best_ask_qty_i64",    out.askQtyE8))    return Status::CorruptData;
    if (!readInt64Field(line, "event_index",          out.eventIndex))  return Status::CorruptData;
    return Status::Ok;
}

Status parseDepthLine(std::string_view line, DepthRow& out) noexcept {
    out = DepthRow{};
    if (!readInt64Field(line, "event_time_ns",   out.tsNs))           return Status::CorruptData;
    if (!readInt64Field(line, "first_update_id", out.firstUpdateId))  return Status::CorruptData;
    if (!readInt64Field(line, "final_update_id", out.finalUpdateId))  return Status::CorruptData;
    if (!readInt64Field(line, "event_index",     out.eventIndex))     return Status::CorruptData;
    if (!readPairArrayField(line, "bids", out.bids)) return Status::CorruptData;
    if (!readPairArrayField(line, "asks", out.asks)) return Status::CorruptData;
    return Status::Ok;
}

Status parseSnapshotDocument(std::string_view doc, SnapshotDocument& out) noexcept {
    out = SnapshotDocument{};
    if (!readInt64Field(doc, "snapshot_time_ns", out.tsNs))           return Status::CorruptData;
    if (!readInt64Field(doc, "snapshot_index",   out.snapshotIndex))  return Status::CorruptData;
    if (!readPairArrayField(doc, "bids", out.bids)) return Status::CorruptData;
    if (!readPairArrayField(doc, "asks", out.asks)) return Status::CorruptData;
    return Status::Ok;
}

}  // namespace hftrec::replay
