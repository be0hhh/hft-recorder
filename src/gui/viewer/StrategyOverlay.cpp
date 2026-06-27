#include "gui/viewer/StrategyOverlay.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string_view>
#include <unordered_map>

#include "core/common/MiniJsonParser.hpp"

namespace hftrec::gui::viewer {
namespace {

struct OrderLifetimeRow {
    std::int64_t tsStartNs{0};
    std::int64_t tsEndNs{0};
    std::int64_t priceE8{0};
    std::int64_t qtyE8{0};
    std::uint8_t side{0};
    bool openEnded{false};
    std::uint32_t legIndex{0};
    std::uint8_t orderType{kStrategyOrderTypeLimit};
};

struct FillRow {
    std::uint64_t orderId{0};
    std::int64_t exitTsNs{0};
    std::uint8_t side{0};
    std::int64_t priceE8{0};
    std::int64_t qtyE8{0};
    bool reduceOnly{false};
    std::uint32_t legIndex{0};
    std::uint8_t fillReason{0};
    std::uint8_t liquidity{0};
    std::int64_t orderQtyE8{0};
    std::int64_t cumulativeFilledQtyE8{0};
    std::int64_t remainingQtyE8{0};
    std::int64_t avgPriceE8{0};
    std::int64_t bookLevelQtyE8{0};
    std::int64_t bookVisibleExecutableQtyE8{0};
    std::int64_t bookConsumedPctE8{0};
    std::int64_t queueAheadBeforeE8{0};
    std::int64_t queueAheadAfterE8{0};
    std::uint32_t chunkIndex{0};
    std::uint32_t chunkCount{0};
    std::int64_t executionQtyE8{0};
    std::int64_t executionAvgPriceE8{0};
    std::int64_t referencePriceE8{0};
    std::int64_t slippageE8{0};
    std::int64_t slippageBpsE8{0};
    std::int64_t executionBookConsumedPctE8{0};
};

struct LegacyOrderRow {
    std::uint64_t id{0};
    std::uint64_t targetId{0};
    std::int64_t activeTsNs{0};
    std::uint8_t action{0};
    std::uint8_t side{0};
    std::uint8_t type{0};
    std::uint8_t status{0};
    std::int64_t priceE8{0};
    std::int64_t qtyE8{0};
    std::uint32_t legIndex{0};
};

struct RangeRow {
    std::int64_t tsNs{0};
    std::int64_t lowE8{0};
    std::int64_t midE8{0};
    std::int64_t highE8{0};
};

struct SpreadRow {
    std::int64_t tsNs{0};
    std::int64_t spreadBpsE8{0};
    std::int64_t emaBpsE8{0};
    std::int64_t deviationBpsE8{0};
    std::int64_t costBandBpsE8{0};
    std::int64_t edgeAfterCostBpsE8{0};
    std::uint8_t direction{0};
    std::uint8_t decisionKind{0};
    std::uint8_t reasonRaw{0};
};

struct ParsedResult {
    std::string runId{};
    std::string strategy{};
    std::string sessionPath{};
    std::vector<OrderLifetimeRow> lifetimes{};
    std::vector<LegacyOrderRow> legacyOrders{};
    std::vector<FillRow> fills{};
    std::vector<RangeRow> ranges{};
    std::vector<SpreadRow> spreads{};
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

bool skipOptionalTrailingFields(hftrec::json::MiniJsonParser& parser) noexcept {
    while (parser.parseComma()) {
        if (!parser.skipValue()) return false;
    }
    return parser.parseArrayEnd() && parser.finish();
}

bool parseOptionalLegIndexAndTrailingFields(hftrec::json::MiniJsonParser& parser,
                                            std::uint32_t& out) noexcept {
    if (!parser.parseComma()) return parser.parseArrayEnd() && parser.finish();
    std::int64_t value = 0;
    if (!parser.parseInt64(value) || value < 0) return false;
    out = static_cast<std::uint32_t>(value);
    return skipOptionalTrailingFields(parser);
}

bool parseOptionalOrderLifetimeTrailingFields(hftrec::json::MiniJsonParser& parser,
                                              OrderLifetimeRow& out) noexcept {
    if (!parser.parseComma()) return parser.parseArrayEnd() && parser.finish();
    std::int64_t legIndex = 0;
    if (!parser.parseInt64(legIndex) || legIndex < 0) return false;
    out.legIndex = static_cast<std::uint32_t>(legIndex);
    if (!parser.parseComma()) return parser.parseArrayEnd() && parser.finish();
    if (!parseByte(parser, out.orderType)) return false;
    return skipOptionalTrailingFields(parser);
}

bool parseOrderLifetimeLine(std::string_view line, OrderLifetimeRow& out) noexcept {
    out = OrderLifetimeRow{};
    hftrec::json::MiniJsonParser parser{line};
    if (!parser.parseArrayStart()) return false;
    if (!parser.parseInt64(out.tsStartNs) || !parser.parseComma()) return false;
    if (!parser.parseInt64(out.tsEndNs) || !parser.parseComma()) return false;
    if (!parser.parseInt64(out.priceE8) || !parser.parseComma()) return false;
    if (!parser.parseInt64(out.qtyE8) || !parser.parseComma()) return false;
    if (!parseByte(parser, out.side) || !parser.parseComma()) return false;
    if (!parseBoolByte(parser, out.openEnded)) return false;
    return parseOptionalOrderLifetimeTrailingFields(parser, out);
}

bool parseLegacyOrderLine(std::string_view line, LegacyOrderRow& out) noexcept {
    out = LegacyOrderRow{};
    hftrec::json::MiniJsonParser parser{line};
    if (!parser.parseArrayStart()) return false;
    std::int64_t ignored = 0;
    if (!parser.parseUInt64(out.id) || !parser.parseComma()) return false;
    if (!parser.parseUInt64(out.targetId) || !parser.parseComma()) return false;
    if (!parser.parseInt64(ignored) || !parser.parseComma()) return false;
    if (!parser.parseInt64(out.activeTsNs) || !parser.parseComma()) return false;
    if (!parser.parseInt64(ignored) || !parser.parseComma()) return false;
    if (!parseByte(parser, out.action) || !parser.parseComma()) return false;
    if (!parseByte(parser, out.side) || !parser.parseComma()) return false;
    if (!parseByte(parser, out.type) || !parser.parseComma()) return false;
    if (!parseByte(parser, out.status) || !parser.parseComma()) return false;
    if (!parser.parseInt64(out.priceE8) || !parser.parseComma()) return false;
    if (!parser.parseInt64(out.qtyE8) || !parser.parseComma()) return false;
    if (!parser.parseInt64(ignored)) return false;
    return parseOptionalLegIndexAndTrailingFields(parser, out.legIndex);
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
    if (!parser.parseComma()) return parser.parseArrayEnd() && parser.finish();
    std::int64_t legIndex = 0;
    if (!parser.parseInt64(legIndex) || legIndex < 0) return false;
    out.legIndex = static_cast<std::uint32_t>(legIndex);
    if (!parser.parseComma()) return parser.parseArrayEnd() && parser.finish();
    if (!parseByte(parser, out.fillReason)) return false;
    if (!parser.parseComma()) return parser.parseArrayEnd() && parser.finish();
    if (!parseByte(parser, out.liquidity)) return false;
    if (!parser.parseComma()) return parser.parseArrayEnd() && parser.finish();
    if (!parser.parseInt64(out.orderQtyE8)) return false;
    if (!parser.parseComma()) return parser.parseArrayEnd() && parser.finish();
    if (!parser.parseInt64(out.cumulativeFilledQtyE8)) return false;
    if (!parser.parseComma()) return parser.parseArrayEnd() && parser.finish();
    if (!parser.parseInt64(out.remainingQtyE8)) return false;
    if (!parser.parseComma()) return parser.parseArrayEnd() && parser.finish();
    if (!parser.parseInt64(out.avgPriceE8)) return false;
    if (!parser.parseComma()) return parser.parseArrayEnd() && parser.finish();
    if (!parser.parseInt64(out.bookLevelQtyE8)) return false;
    if (!parser.parseComma()) return parser.parseArrayEnd() && parser.finish();
    if (!parser.parseInt64(out.bookVisibleExecutableQtyE8)) return false;
    if (!parser.parseComma()) return parser.parseArrayEnd() && parser.finish();
    if (!parser.parseInt64(out.bookConsumedPctE8)) return false;
    if (!parser.parseComma()) return parser.parseArrayEnd() && parser.finish();
    if (!parser.parseInt64(out.queueAheadBeforeE8)) return false;
    if (!parser.parseComma()) return parser.parseArrayEnd() && parser.finish();
    if (!parser.parseInt64(out.queueAheadAfterE8)) return false;
    if (!parser.parseComma()) return parser.parseArrayEnd() && parser.finish();
    std::int64_t chunkIndex = 0;
    if (!parser.parseInt64(chunkIndex) || chunkIndex < 0) return false;
    out.chunkIndex = static_cast<std::uint32_t>(chunkIndex);
    if (!parser.parseComma()) return parser.parseArrayEnd() && parser.finish();
    std::int64_t chunkCount = 0;
    if (!parser.parseInt64(chunkCount) || chunkCount < 0) return false;
    out.chunkCount = static_cast<std::uint32_t>(chunkCount);
    if (!parser.parseComma()) return parser.parseArrayEnd() && parser.finish();
    if (!parser.parseInt64(out.executionQtyE8)) return false;
    if (!parser.parseComma()) return parser.parseArrayEnd() && parser.finish();
    if (!parser.parseInt64(out.executionAvgPriceE8)) return false;
    if (!parser.parseComma()) return parser.parseArrayEnd() && parser.finish();
    if (!parser.parseInt64(out.referencePriceE8)) return false;
    if (!parser.parseComma()) return parser.parseArrayEnd() && parser.finish();
    if (!parser.parseInt64(out.slippageE8)) return false;
    if (!parser.parseComma()) return parser.parseArrayEnd() && parser.finish();
    if (!parser.parseInt64(out.slippageBpsE8)) return false;
    if (!parser.parseComma()) return parser.parseArrayEnd() && parser.finish();
    if (!parser.parseInt64(out.executionBookConsumedPctE8)) return false;
    return skipOptionalTrailingFields(parser);
}

bool parseRangeLine(std::string_view line, RangeRow& out) noexcept {
    out = RangeRow{};
    hftrec::json::MiniJsonParser parser{line};
    if (!parser.parseArrayStart()) return false;
    if (!parser.parseInt64(out.tsNs) || !parser.parseComma()) return false;
    if (!parser.parseInt64(out.lowE8) || !parser.parseComma()) return false;
    if (!parser.parseInt64(out.midE8) || !parser.parseComma()) return false;
    if (!parser.parseInt64(out.highE8)) return false;
    return skipOptionalTrailingFields(parser);
}

bool parseSpreadLine(std::string_view line, SpreadRow& out) noexcept {
    out = SpreadRow{};
    hftrec::json::MiniJsonParser parser{line};
    if (!parser.parseArrayStart()) return false;
    if (!parser.parseInt64(out.tsNs) || !parser.parseComma()) return false;
    if (!parser.parseInt64(out.spreadBpsE8) || !parser.parseComma()) return false;
    if (!parser.parseInt64(out.emaBpsE8) || !parser.parseComma()) return false;
    if (!parser.parseInt64(out.deviationBpsE8) || !parser.parseComma()) return false;
    if (!parser.parseInt64(out.costBandBpsE8) || !parser.parseComma()) return false;
    if (!parser.parseInt64(out.edgeAfterCostBpsE8) || !parser.parseComma()) return false;
    if (!parseByte(parser, out.direction) || !parser.parseComma()) return false;
    if (!parseByte(parser, out.decisionKind) || !parser.parseComma()) return false;
    if (!parseByte(parser, out.reasonRaw)) return false;
    return skipOptionalTrailingFields(parser);
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

template <typename Row, typename ParseFn>
bool loadOptionalJsonl(const std::filesystem::path& path, std::vector<Row>& out, ParseFn parse) {
    out.clear();
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) return true;
    return loadJsonl(path, out, parse);
}

bool sideBuy(std::uint8_t side) noexcept {
    return side == 1u;
}

bool legacyStatusEffective(std::uint8_t status) noexcept {
    return status == 2u || status == 3u || status == 4u || status == 7u;
}

bool legacyStatusOpen(std::uint8_t status) noexcept {
    return status == 2u;
}

std::vector<OrderLifetimeRow> materializeLegacyLifetimes(const std::vector<LegacyOrderRow>& orders,
                                                         const std::vector<FillRow>& fills,
                                                         std::int64_t fallbackRunEndNs) {
    std::unordered_map<std::uint64_t, const FillRow*> fillByOrderId;
    fillByOrderId.reserve(fills.size());
    for (const FillRow& fill : fills) {
        if (fill.exitTsNs <= 0 || fill.priceE8 <= 0) continue;
        auto [it, inserted] = fillByOrderId.emplace(fill.orderId, &fill);
        if (!inserted && fill.exitTsNs < it->second->exitTsNs) it->second = &fill;
    }

    std::unordered_map<std::uint64_t, std::vector<const LegacyOrderRow*>> commandsByTargetId;
    commandsByTargetId.reserve(orders.size() / 2u + 1u);
    for (const LegacyOrderRow& order : orders) {
        if (order.targetId == 0 || order.activeTsNs <= 0) continue;
        if (order.action != 2u && order.action != 3u) continue;
        if (!legacyStatusEffective(order.status)) continue;
        commandsByTargetId[order.targetId].push_back(&order);
    }
    for (auto& row : commandsByTargetId) {
        std::stable_sort(row.second.begin(), row.second.end(), [](const LegacyOrderRow* lhs, const LegacyOrderRow* rhs) noexcept {
            if (lhs->activeTsNs != rhs->activeTsNs) return lhs->activeTsNs < rhs->activeTsNs;
            return lhs->id < rhs->id;
        });
    }

    std::vector<OrderLifetimeRow> out;
    out.reserve(fills.size() + 16u);
    for (const LegacyOrderRow& order : orders) {
        if (order.action != 1u || order.type != 1u) continue;
        if (!legacyStatusEffective(order.status)) continue;
        if (order.activeTsNs <= 0 || order.priceE8 <= 0 || order.qtyE8 <= 0) continue;

        const auto commandIt = commandsByTargetId.find(order.id);
        const auto* commands = commandIt == commandsByTargetId.end() ? nullptr : &commandIt->second;
        if (commands != nullptr) {
            const auto sameTimestamp = std::lower_bound(commands->begin(), commands->end(), order.activeTsNs,
                                                        [](const LegacyOrderRow* row, std::int64_t tsNs) noexcept {
                                                            return row->activeTsNs < tsNs;
                                                        });
            if (sameTimestamp != commands->end() && (*sameTimestamp)->activeTsNs == order.activeTsNs) continue;
        }

        std::int64_t endTs = 0;
        if (commands != nullptr) {
            const auto command = std::upper_bound(commands->begin(), commands->end(), order.activeTsNs,
                                                  [](std::int64_t tsNs, const LegacyOrderRow* row) noexcept {
                                                      return tsNs < row->activeTsNs;
                                                  });
            if (command != commands->end()) endTs = (*command)->activeTsNs;
        }

        const auto fillIt = fillByOrderId.find(order.id);
        const FillRow* fill = fillIt == fillByOrderId.end() ? nullptr : fillIt->second;
        if (fill != nullptr && (endTs == 0 || fill->exitTsNs < endTs)) endTs = fill->exitTsNs;

        bool openEnded = false;
        if (endTs <= order.activeTsNs) {
            if (fill != nullptr) {
                endTs = order.activeTsNs;
            } else if (legacyStatusOpen(order.status)) {
                endTs = fallbackRunEndNs > order.activeTsNs ? fallbackRunEndNs : order.activeTsNs;
                openEnded = endTs > order.activeTsNs;
            }
        }
        if (endTs <= order.activeTsNs) continue;
        out.push_back(OrderLifetimeRow{
            order.activeTsNs,
            endTs,
            order.priceE8,
            order.qtyE8,
            order.side,
            openEnded,
            order.legIndex,
        });
    }

    std::stable_sort(out.begin(), out.end(), [](const OrderLifetimeRow& lhs, const OrderLifetimeRow& rhs) noexcept {
        if (lhs.tsStartNs != rhs.tsStartNs) return lhs.tsStartNs < rhs.tsStartNs;
        return lhs.priceE8 < rhs.priceE8;
    });
    return out;
}

void materialize(const ParsedResult& parsed, std::int64_t fallbackRunEndNs, StrategyOverlayData& out) {
    out = StrategyOverlayData{};
    out.runId = QString::fromStdString(parsed.runId);
    out.strategy = QString::fromStdString(parsed.strategy);
    out.sourceSessionPath = QString::fromStdString(parsed.sessionPath);
    const std::vector<OrderLifetimeRow> legacyLifetimes = parsed.lifetimes.empty() && !parsed.legacyOrders.empty()
        ? materializeLegacyLifetimes(parsed.legacyOrders, parsed.fills, fallbackRunEndNs)
        : std::vector<OrderLifetimeRow>{};
    const std::vector<OrderLifetimeRow>& lifetimes = parsed.lifetimes.empty() ? legacyLifetimes : parsed.lifetimes;
    out.orderSegments.reserve(lifetimes.size());
    out.fillMarkers.reserve(parsed.fills.size());

    for (const OrderLifetimeRow& row : lifetimes) {
        if (row.tsEndNs <= row.tsStartNs || row.priceE8 <= 0 || row.qtyE8 <= 0) continue;
        out.orderSegments.push_back(StrategyOrderSegment{
            row.tsStartNs,
            row.tsEndNs,
            row.priceE8,
            row.qtyE8,
            row.legIndex,
            sideBuy(row.side),
            row.openEnded,
            row.orderType,
        });
    }

    for (const FillRow& fill : parsed.fills) {
        if (fill.exitTsNs <= 0 || fill.priceE8 <= 0 || fill.qtyE8 <= 0) continue;
        const bool buy = sideBuy(fill.side);
        StrategyFillMarker marker{
            fill.exitTsNs,
            fill.priceE8,
            fill.qtyE8,
            fill.legIndex,
            buy,
            false,
            fill.reduceOnly,
            buy ? StrategyFillShape::BuyUp : StrategyFillShape::SellDown,
        };
        marker.fillReason = fill.fillReason;
        marker.liquidity = fill.liquidity;
        marker.orderQtyE8 = fill.orderQtyE8;
        marker.cumulativeFilledQtyE8 = fill.cumulativeFilledQtyE8;
        marker.remainingQtyE8 = fill.remainingQtyE8;
        marker.avgPriceE8 = fill.avgPriceE8;
        marker.bookLevelQtyE8 = fill.bookLevelQtyE8;
        marker.bookVisibleExecutableQtyE8 = fill.bookVisibleExecutableQtyE8;
        marker.bookConsumedPctE8 = fill.bookConsumedPctE8;
        marker.queueAheadBeforeE8 = fill.queueAheadBeforeE8;
        marker.queueAheadAfterE8 = fill.queueAheadAfterE8;
        marker.chunkIndex = fill.chunkIndex;
        marker.chunkCount = fill.chunkCount;
        marker.executionQtyE8 = fill.executionQtyE8;
        marker.executionAvgPriceE8 = fill.executionAvgPriceE8;
        marker.referencePriceE8 = fill.referencePriceE8;
        marker.slippageE8 = fill.slippageE8;
        marker.slippageBpsE8 = fill.slippageBpsE8;
        marker.executionBookConsumedPctE8 = fill.executionBookConsumedPctE8;
        marker.orderId = fill.orderId;
        out.fillMarkers.push_back(marker);
    }

    for (const RangeRow& row : parsed.ranges) {
        if (row.tsNs <= 0 || row.lowE8 <= 0 || row.highE8 <= row.lowE8) continue;
        out.rangePoints.push_back(StrategyRangePoint{
            row.tsNs,
            row.lowE8,
            row.midE8,
            row.highE8,
        });
    }

    for (const SpreadRow& row : parsed.spreads) {
        if (row.tsNs <= 0) continue;
        out.spreadPoints.push_back(StrategySpreadPoint{
            row.tsNs,
            row.spreadBpsE8,
            row.emaBpsE8,
            row.deviationBpsE8,
            row.costBandBpsE8,
            row.edgeAfterCostBpsE8,
            row.direction,
            row.decisionKind,
            row.reasonRaw,
        });
    }

    std::stable_sort(out.orderSegments.begin(), out.orderSegments.end(), [](const auto& lhs, const auto& rhs) noexcept {
        if (lhs.tsStartNs != rhs.tsStartNs) return lhs.tsStartNs < rhs.tsStartNs;
        return lhs.priceE8 < rhs.priceE8;
    });
    std::stable_sort(out.fillMarkers.begin(), out.fillMarkers.end(), [](const auto& lhs, const auto& rhs) noexcept {
        if (lhs.tsNs != rhs.tsNs) return lhs.tsNs < rhs.tsNs;
        return lhs.priceE8 < rhs.priceE8;
    });
    std::stable_sort(out.rangePoints.begin(), out.rangePoints.end(), [](const auto& lhs, const auto& rhs) noexcept {
        if (lhs.tsNs != rhs.tsNs) return lhs.tsNs < rhs.tsNs;
        return lhs.midE8 < rhs.midE8;
    });
    std::stable_sort(out.spreadPoints.begin(), out.spreadPoints.end(), [](const auto& lhs, const auto& rhs) noexcept {
        return lhs.tsNs < rhs.tsNs;
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
    if (!loadOptionalJsonl(path / "order_lifetimes.jsonl", parsed.lifetimes, parseOrderLifetimeLine)) {
        error = "failed to parse order lifetimes";
        return false;
    }
    if (!loadJsonl(path / "fills.jsonl", parsed.fills, parseFillLine)) {
        error = "failed to parse backtest fills";
        return false;
    }
    std::error_code ordersEc;
    const std::filesystem::path legacyOrdersPath = path / "orders.jsonl";
    if (parsed.lifetimes.empty() && std::filesystem::is_regular_file(legacyOrdersPath, ordersEc) && !ordersEc) {
        if (!loadJsonl(legacyOrdersPath, parsed.legacyOrders, parseLegacyOrderLine)) {
            error = "failed to parse legacy backtest orders";
            return false;
        }
    }
    if (!loadOptionalJsonl(path / "strategy_range.jsonl", parsed.ranges, parseRangeLine)) {
        error = "failed to parse strategy range";
        return false;
    }
    if (!loadOptionalJsonl(path / "strategy_spread.jsonl", parsed.spreads, parseSpreadLine)) {
        error = "failed to parse strategy spread";
        return false;
    }
    materialize(parsed, fallbackRunEndNs, out);
    return true;
}

}  // namespace hftrec::gui::viewer
