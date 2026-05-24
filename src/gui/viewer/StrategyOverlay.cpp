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
    std::string action{};
    std::string side{};
    std::string type{};
    std::string status{};
    std::int64_t priceE8{0};
    std::int64_t qtyE8{0};
};

struct FillRow {
    std::uint64_t orderId{0};
    std::int64_t exitTsNs{0};
    std::string side{};
    std::int64_t priceE8{0};
    std::int64_t qtyE8{0};
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

bool parseOrder(hftrec::json::MiniJsonParser& parser, OrderRow& out) noexcept {
    out = OrderRow{};
    if (!parser.parseObjectStart()) return false;
    if (parser.peek('}')) return parser.parseObjectEnd();
    std::string key;
    do {
        if (!parser.parseKey(key)) return false;
        if (key == "id") {
            if (!parser.parseUInt64(out.id)) return false;
        } else if (key == "target_id") {
            if (!parser.parseUInt64(out.targetId)) return false;
        } else if (key == "active_ts_ns") {
            if (!parser.parseInt64(out.activeTsNs)) return false;
        } else if (key == "action") {
            if (!parser.parseString(out.action)) return false;
        } else if (key == "side") {
            if (!parser.parseString(out.side)) return false;
        } else if (key == "type") {
            if (!parser.parseString(out.type)) return false;
        } else if (key == "status") {
            if (!parser.parseString(out.status)) return false;
        } else if (key == "price_e8") {
            if (!parser.parseInt64(out.priceE8)) return false;
        } else if (key == "qty_e8") {
            if (!parser.parseInt64(out.qtyE8)) return false;
        } else if (!parser.skipValue()) {
            return false;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());
    return parser.parseObjectEnd();
}

bool parseFill(hftrec::json::MiniJsonParser& parser, FillRow& out) noexcept {
    out = FillRow{};
    if (!parser.parseObjectStart()) return false;
    if (parser.peek('}')) return parser.parseObjectEnd();
    std::string key;
    do {
        if (!parser.parseKey(key)) return false;
        if (key == "order_id") {
            if (!parser.parseUInt64(out.orderId)) return false;
        } else if (key == "exit_ts_ns") {
            if (!parser.parseInt64(out.exitTsNs)) return false;
        } else if (key == "side") {
            if (!parser.parseString(out.side)) return false;
        } else if (key == "price_e8") {
            if (!parser.parseInt64(out.priceE8)) return false;
        } else if (key == "qty_e8") {
            if (!parser.parseInt64(out.qtyE8)) return false;
        } else if (!parser.skipValue()) {
            return false;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());
    return parser.parseObjectEnd();
}

bool parseOrders(hftrec::json::MiniJsonParser& parser, std::vector<OrderRow>& out) noexcept {
    out.clear();
    if (!parser.parseArrayStart()) return false;
    if (parser.peek(']')) return parser.parseArrayEnd();
    do {
        OrderRow row{};
        if (!parseOrder(parser, row)) return false;
        out.push_back(std::move(row));
        if (parser.peek(']')) break;
    } while (parser.parseComma());
    return parser.parseArrayEnd();
}

bool parseFills(hftrec::json::MiniJsonParser& parser, std::vector<FillRow>& out) noexcept {
    out.clear();
    if (!parser.parseArrayStart()) return false;
    if (parser.peek(']')) return parser.parseArrayEnd();
    do {
        FillRow row{};
        if (!parseFill(parser, row)) return false;
        out.push_back(std::move(row));
        if (parser.peek(']')) break;
    } while (parser.parseComma());
    return parser.parseArrayEnd();
}

bool parseResult(std::string_view text, ParsedResult& out) noexcept {
    out = ParsedResult{};
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
        } else if (key == "orders") {
            if (!parseOrders(parser, out.orders)) return false;
        } else if (key == "fills") {
            if (!parseFills(parser, out.fills)) return false;
        } else if (!parser.skipValue()) {
            return false;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());
    return parser.parseObjectEnd() && parser.finish();
}

bool sideBuy(std::string_view side) noexcept {
    return side == "buy";
}

bool isEffectiveCommandStatus(std::string_view status) noexcept {
    return status != "missing" && status != "rejected" && status != "pending" &&
           status != "failed" && status != "error";
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
        if (order.action != "cancel" && order.action != "amend") continue;
        if (!isEffectiveCommandStatus(order.status)) continue;
        if (best == 0 || order.activeTsNs < best) best = order.activeTsNs;
    }
    return best;
}

void materialize(const ParsedResult& parsed, std::int64_t fallbackRunEndNs, StrategyOverlayData& out) {
    out = StrategyOverlayData{};
    out.runId = QString::fromStdString(parsed.runId);
    out.strategy = QString::fromStdString(parsed.strategy);
    out.sourceSessionPath = QString::fromStdString(parsed.sessionPath);

    for (const OrderRow& order : parsed.orders) {
        const bool liveCommand = order.action == "order" || order.action == "amend";
        if (!liveCommand || !isEffectiveCommandStatus(order.status)) continue;
        const FillRow* fill = findFill(parsed.fills, order.id);
        if (order.type == "limit" && order.activeTsNs > 0 && order.priceE8 > 0) {
            std::int64_t endTs = findCommandEndTs(parsed.orders, order.id, order.activeTsNs);
            if (fill != nullptr && (endTs == 0 || fill->exitTsNs < endTs)) endTs = fill->exitTsNs;
            bool openEnded = false;
            if (endTs <= order.activeTsNs) {
                endTs = fallbackRunEndNs;
                openEnded = true;
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
                order.type == "market",
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
    std::string text;
    if (!readText(path, text)) {
        error = "failed to read backtest result";
        return false;
    }
    ParsedResult parsed{};
    if (!parseResult(text, parsed)) {
        error = "failed to parse backtest result";
        return false;
    }
    materialize(parsed, fallbackRunEndNs, out);
    return true;
}

}  // namespace hftrec::gui::viewer
