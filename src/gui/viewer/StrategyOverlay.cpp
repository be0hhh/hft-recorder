#include "gui/viewer/StrategyOverlay.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string_view>

#include "core/common/MiniJsonParser.hpp"

namespace hftrec::gui::viewer {
namespace {

struct OrderRow {
    std::uint64_t id{0};
    std::uint64_t targetId{0};
    std::int64_t activeTsNs{0};
    std::uint8_t action{0};
    std::uint8_t side{0};
    std::uint8_t type{0};
    std::uint8_t status{0};
    std::int64_t priceE8{0};
    std::int64_t qtyE8{0};
};

struct FillRow {
    std::uint64_t orderId{0};
    std::int64_t exitTsNs{0};
    std::uint8_t side{0};
    std::int64_t priceE8{0};
    std::int64_t qtyE8{0};
    bool reduceOnly{false};
};

struct ParsedResult {
    std::string runId{};
    std::string strategy{};
    std::string sessionPath{};
    std::vector<OrderRow> orders{};
    std::vector<FillRow> fills{};
};

bool readText(const std::filesystem::path& path, std::string& out) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in.is_open()) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

bool parseManifest(std::string_view text, ParsedResult& out) noexcept {
    hftrec::json::MiniJsonParser parser{text};
    if (!parser.parseObjectStart()) return false;
    if (parser.peek('}')) return parser.parseObjectEnd() && parser.finish();
    std::string key;
    do {
        if (!parser.parseKey(key)) return false;
        if (key == "run_id") {
            if (!parser.parseString(out.runId)) return false;
        } else if (key == "strategy") {
            if (!parser.parseString(out.strategy)) return false;
        } else if (key == "session_path") {
            if (!parser.parseString(out.sessionPath)) return false;
        } else if (!parser.skipValue()) {
            return false;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());
    return parser.parseObjectEnd() && parser.finish();
}

bool parseByte(hftrec::json::MiniJsonParser& parser, std::uint8_t& out) noexcept {
    std::int64_t value = 0;
    if (!parser.parseInt64(value) || value < 0 || value > 255) return false;
    out = static_cast<std::uint8_t>(value);
    return true;
}

bool parseBoolByte(hftrec::json::MiniJsonParser& parser, bool& out) noexcept {
    std::int64_t value = 0;
    if (!parser.parseInt64(value) || (value != 0 && value != 1)) return false;
    out = value != 0;
    return true;
}

bool parseOrderLine(std::string_view line, OrderRow& out) noexcept {
    out = OrderRow{};
    hftrec::json::MiniJsonParser parser{line};
    if (!parser.parseArrayStart()) return false;
    if (!parser.parseUInt64(out.id) || !parser.parseComma()) return false;
    if (!parser.parseUInt64(out.targetId) || !parser.parseComma()) return false;
    std::int64_t ignored = 0;
    if (!parser.parseInt64(ignored) || !parser.parseComma()) return false;
    if (!parser.parseInt64(out.activeTsNs) || !parser.parseComma()) return false;
    if (!parser.parseInt64(ignored) || !parser.parseComma()) return false;
    if (!parseByte(parser, out.action) || !parser.parseComma()) return false;
    if (!parseByte(parser, out.side) || !parser.parseComma()) return false;
    if (!parseByte(parser, out.type) || !parser.parseComma()) return false;
    if (!parseByte(parser, out.status) || !parser.parseComma()) return false;
    if (!parser.parseInt64(out.priceE8) || !parser.parseComma()) return false;
    if (!parser.parseInt64(out.qtyE8) || !parser.parseComma()) return false;
    bool ignoredBool = false;
    if (!parseBoolByte(parser, ignoredBool)) return false;
    return parser.parseArrayEnd() && parser.finish();
}

bool parseFillLine(std::string_view line, FillRow& out) noexcept {
    out = FillRow{};
    hftrec::json::MiniJsonParser parser{line};
    if (!parser.parseArrayStart()) return false;
    std::int64_t ignored = 0;
    if (!parser.parseUInt64(out.orderId) || !parser.parseComma()) return false;
    if (!parser.parseInt64(ignored) || !parser.parseComma()) return false;
    if (!parser.parseInt64(out.exitTsNs) || !parser.parseComma()) return false;
    if (!parseByte(parser, out.side) || !parser.parseComma()) return false;
    if (!parser.parseInt64(out.priceE8) || !parser.parseComma()) return false;
    if (!parser.parseInt64(out.qtyE8) || !parser.parseComma()) return false;
    if (!parser.parseInt64(ignored) || !parser.parseComma()) return false;
    if (!parseBoolByte(parser, out.reduceOnly)) return false;
    return parser.parseArrayEnd() && parser.finish();
}

template <typename Row, typename ParseFn>
bool loadJsonl(const std::filesystem::path& path, std::vector<Row>& out, ParseFn parse) {
    out.clear();
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in.is_open()) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        Row row{};
        if (!parse(line, row)) return false;
        out.push_back(row);
    }
    return in.eof();
}

bool sideBuy(std::uint8_t side) noexcept {
    return side == 1u;
}

bool isLiveCommand(std::uint8_t action) noexcept {
    return action == 1u || action == 2u;
}

bool isMarketType(std::uint8_t type) noexcept {
    return type == 2u;
}

bool isLimitType(std::uint8_t type) noexcept {
    return type == 1u;
}

bool isEffectiveCommandStatus(std::uint8_t status) noexcept {
    return status == 2u || status == 3u || status == 4u;
}

const FillRow* findFill(const std::vector<FillRow>& fills, std::uint64_t orderId) noexcept {
    const FillRow* best = nullptr;
    for (const FillRow& fill : fills) {
        if (fill.orderId != orderId || fill.exitTsNs <= 0 || fill.priceE8 <= 0) continue;
        if (best == nullptr || fill.exitTsNs < best->exitTsNs) best = &fill;
    }
    return best;
}

std::int64_t findCommandEndTs(const std::vector<OrderRow>& orders,
                              std::uint64_t orderId,
                              std::int64_t startTsNs) noexcept {
    std::int64_t best = 0;
    for (const OrderRow& order : orders) {
        if (order.targetId != orderId || order.activeTsNs <= startTsNs) continue;
        if (order.action != 2u && order.action != 3u) continue;
        if (!isEffectiveCommandStatus(order.status)) continue;
        if (best == 0 || order.activeTsNs < best) best = order.activeTsNs;
    }
    return best;
}

bool hasSameTimestampReplaceEnd(const std::vector<OrderRow>& orders,
                                  std::uint64_t orderId,
                                  std::int64_t startTsNs) noexcept {
    for (const OrderRow& order : orders) {
        if (order.targetId != orderId || order.activeTsNs != startTsNs) continue;
        if ((order.action == 2u || order.action == 3u) && isEffectiveCommandStatus(order.status)) return true;
    }
    return false;
}

void materialize(const ParsedResult& parsed, std::int64_t fallbackRunEndNs, StrategyOverlayData& out) {
    out = StrategyOverlayData{};
    out.runId = QString::fromStdString(parsed.runId);
    out.strategy = QString::fromStdString(parsed.strategy);
    out.sourceSessionPath = QString::fromStdString(parsed.sessionPath);

    for (const OrderRow& order : parsed.orders) {
        if (!isLiveCommand(order.action) || !isEffectiveCommandStatus(order.status)) continue;
        const FillRow* fill = findFill(parsed.fills, order.id);
        const bool replacedAtStart = order.action == 1u && hasSameTimestampReplaceEnd(parsed.orders, order.id, order.activeTsNs);
        if (!replacedAtStart && isLimitType(order.type) && order.activeTsNs > 0 && order.priceE8 > 0) {
            std::int64_t endTs = findCommandEndTs(parsed.orders, order.id, order.activeTsNs);
            if (fill != nullptr && (endTs == 0 || fill->exitTsNs < endTs)) endTs = fill->exitTsNs;
            bool openEnded = false;
            if (endTs <= order.activeTsNs) {
                if (fill != nullptr) {
                    endTs = order.activeTsNs;
                } else {
                    endTs = fallbackRunEndNs;
                    openEnded = true;
                }
            }
            if (endTs > order.activeTsNs) {
                out.orderSegments.push_back(StrategyOrderSegment{
                    order.activeTsNs,
                    endTs,
                    order.priceE8,
                    order.qtyE8,
                    sideBuy(order.side),
                    openEnded,
                });
            }
        }
        if (fill != nullptr) {
            const bool buy = sideBuy(fill->side);
            out.fillMarkers.push_back(StrategyFillMarker{
                fill->exitTsNs,
                fill->priceE8,
                fill->qtyE8,
                buy,
                isMarketType(order.type),
                fill->reduceOnly,
                buy ? StrategyFillShape::BuyUp : StrategyFillShape::SellDown,
            });
        }
    }

    std::stable_sort(out.orderSegments.begin(), out.orderSegments.end(), [](const auto& lhs, const auto& rhs) noexcept {
        if (lhs.tsStartNs != rhs.tsStartNs) return lhs.tsStartNs < rhs.tsStartNs;
        return lhs.priceE8 < rhs.priceE8;
    });
    std::stable_sort(out.fillMarkers.begin(), out.fillMarkers.end(), [](const auto& lhs, const auto& rhs) noexcept {
        if (lhs.tsNs != rhs.tsNs) return lhs.tsNs < rhs.tsNs;
        return lhs.priceE8 < rhs.priceE8;
    });
}

}  // namespace

bool loadStrategyOverlayFromResult(const std::filesystem::path& path,
                                   std::int64_t fallbackRunEndNs,
                                   StrategyOverlayData& out,
                                   std::string& error) noexcept {
    out = StrategyOverlayData{};
    error.clear();
    ParsedResult parsed{};
    std::string manifest;
    if (!readText(path / "manifest.json", manifest)) {
        error = "failed to read backtest manifest";
        return false;
    }
    if (!parseManifest(manifest, parsed)) {
        error = "failed to parse backtest manifest";
        return false;
    }
    if (!loadJsonl(path / "orders.jsonl", parsed.orders, parseOrderLine)) {
        error = "failed to parse backtest orders";
        return false;
    }
    if (!loadJsonl(path / "fills.jsonl", parsed.fills, parseFillLine)) {
        error = "failed to parse backtest fills";
        return false;
    }
    materialize(parsed, fallbackRunEndNs, out);
    return true;
}

}  // namespace hftrec::gui::viewer
