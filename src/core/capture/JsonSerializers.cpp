#include "core/capture/JsonSerializers.hpp"

#include <charconv>
#include <system_error>

#include "core/common/JsonString.hpp"
#include "core/replay/EventRows.hpp"

namespace hftrec::capture {

namespace {

constexpr char price[] = {'p','r','i','c','e','\0'};
constexpr char amount[] = {'a','m','o','u','n','t','\0'};
constexpr char side[] = {'s','i','d','e','\0'};
constexpr char timestamp[] = {'t','i','m','e','s','t','a','m','p','\0'};
constexpr char id[] = {'i','d','\0'};
constexpr char isBuyerMaker[] = {'i','s','B','u','y','e','r','M','a','k','e','r','\0'};
constexpr char firstTradeId[] = {'f','i','r','s','t','T','r','a','d','e','I','d','\0'};
constexpr char lastTradeId[] = {'l','a','s','t','T','r','a','d','e','I','d','\0'};
constexpr char quoteQty[] = {'q','u','o','t','e','Q','t','y','\0'};
constexpr char symbol[] = {'s','y','m','b','o','l','\0'};
constexpr char exchange[] = {'e','x','c','h','a','n','g','e','\0'};
constexpr char market[] = {'m','a','r','k','e','t','\0'};
constexpr char bidPrice[] = {'b','i','d','P','r','i','c','e','\0'};
constexpr char bidQty[] = {'b','i','d','Q','t','y','\0'};
constexpr char askPrice[] = {'a','s','k','P','r','i','c','e','\0'};
constexpr char askQty[] = {'a','s','k','Q','t','y','\0'};
constexpr char updateId[] = {'u','p','d','a','t','e','I','d','\0'};
constexpr char avgPrice[] = {'a','v','g','P','r','i','c','e','\0'};
constexpr char filledQty[] = {'f','i','l','l','e','d','Q','t','y','\0'};
constexpr char orderType[] = {'o','r','d','e','r','T','y','p','e','\0'};
constexpr char timeInForce[] = {'t','i','m','e','I','n','F','o','r','c','e','\0'};
constexpr char status[] = {'s','t','a','t','u','s','\0'};
constexpr char sourceMode[] = {'s','o','u','r','c','e','M','o','d','e','\0'};
constexpr char captureSeq[] = {'c','a','p','t','u','r','e','S','e','q','\0'};
constexpr char ingestSeq[] = {'i','n','g','e','s','t','S','e','q','\0'};

template <typename AppendFn>
void appendValue(std::string& out, bool& first, AppendFn&& appendFn) {
    if (!first) out.push_back(',');
    appendFn();
    first = false;
}

template <typename Int>
void appendInt(std::string& out, Int value) {
    char buf[32];
    const auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), value);
    if (ec == std::errc{}) out.append(buf, ptr);
}

void appendString(std::string& out, std::string_view value) {
    out.push_back(static_cast<char>(34));
    json::appendEscaped(out, value);
    out.push_back(static_cast<char>(34));
}

void appendFlatOrderbook(std::string& out, const std::vector<replay::PricePair>& levels, std::int64_t tsNs) {
    out.push_back('[');
    for (std::size_t i = 0; i < levels.size(); ++i) {
        if (i != 0) out.push_back(',');
        out.push_back('[');
        appendInt(out, levels[i].priceE8);
        out.push_back(',');
        appendInt(out, levels[i].qtyE8);
        out.push_back(',');
        appendInt(out, levels[i].side);
        out.push_back(']');
    }
    if (!levels.empty()) out.push_back(',');
    appendInt(out, tsNs);
    out.push_back(']');
}

}  // namespace

std::string renderTradeJsonLine(const replay::TradeRow& trade) {
    std::string out;
    out.reserve(96);
    out.push_back('[');
    appendInt(out, trade.priceE8); out.push_back(',');
    appendInt(out, trade.qtyE8); out.push_back(',');
    appendInt(out, trade.side); out.push_back(',');
    appendInt(out, trade.tsNs);
    out.push_back(']');
    return out;
}

std::string renderTradeJsonLine(const replay::TradeRow& trade,
                                const std::vector<std::string>& aliases) {
    (void)aliases;
    return renderTradeJsonLine(trade);
}

std::string renderLiquidationJsonLine(const replay::LiquidationRow& liquidation) {
    std::string out;
    out.reserve(256);
    out.push_back('[');
    appendInt(out, liquidation.priceE8); out.push_back(',');
    appendInt(out, liquidation.qtyE8); out.push_back(',');
    appendInt(out, liquidation.side); out.push_back(',');
    appendInt(out, liquidation.tsNs); out.push_back(',');
    appendInt(out, liquidation.avgPriceE8); out.push_back(',');
    appendInt(out, liquidation.filledQtyE8); out.push_back(',');
    appendString(out, liquidation.symbol); out.push_back(',');
    appendString(out, liquidation.exchange); out.push_back(',');
    appendString(out, liquidation.market); out.push_back(',');
    appendInt(out, liquidation.orderType); out.push_back(',');
    appendInt(out, liquidation.timeInForce); out.push_back(',');
    appendInt(out, liquidation.status); out.push_back(',');
    appendInt(out, liquidation.sourceMode); out.push_back(',');
    appendInt(out, liquidation.captureSeq); out.push_back(',');
    appendInt(out, liquidation.ingestSeq);
    out.push_back(']');
    return out;
}

std::string renderLiquidationJsonLine(const replay::LiquidationRow& liquidation,
                                      const std::vector<std::string>& aliases) {
    std::string out;
    out.reserve(256);
    out.push_back('[');
    bool first = true;
    for (const auto& alias : aliases) {
        if (alias == price) appendValue(out, first, [&] { appendInt(out, liquidation.priceE8); });
        else if (alias == amount) appendValue(out, first, [&] { appendInt(out, liquidation.qtyE8); });
        else if (alias == side) appendValue(out, first, [&] { appendInt(out, liquidation.side); });
        else if (alias == timestamp) appendValue(out, first, [&] { appendInt(out, liquidation.tsNs); });
        else if (alias == avgPrice) appendValue(out, first, [&] { appendInt(out, liquidation.avgPriceE8); });
        else if (alias == filledQty) appendValue(out, first, [&] { appendInt(out, liquidation.filledQtyE8); });
        else if (alias == symbol) appendValue(out, first, [&] { appendString(out, liquidation.symbol); });
        else if (alias == exchange) appendValue(out, first, [&] { appendString(out, liquidation.exchange); });
        else if (alias == market) appendValue(out, first, [&] { appendString(out, liquidation.market); });
        else if (alias == orderType) appendValue(out, first, [&] { appendInt(out, liquidation.orderType); });
        else if (alias == timeInForce) appendValue(out, first, [&] { appendInt(out, liquidation.timeInForce); });
        else if (alias == status) appendValue(out, first, [&] { appendInt(out, liquidation.status); });
        else if (alias == sourceMode) appendValue(out, first, [&] { appendInt(out, liquidation.sourceMode); });
        else if (alias == captureSeq) appendValue(out, first, [&] { appendInt(out, liquidation.captureSeq); });
        else if (alias == ingestSeq) appendValue(out, first, [&] { appendInt(out, liquidation.ingestSeq); });
    }
    out.push_back(']');
    return out;
}

std::string renderBookTickerJsonLine(const replay::BookTickerRow& bookTicker) {
    std::string out;
    out.reserve(120);
    out.push_back('[');
    appendInt(out, bookTicker.bidPriceE8); out.push_back(',');
    appendInt(out, bookTicker.bidQtyE8); out.push_back(',');
    appendInt(out, bookTicker.askPriceE8); out.push_back(',');
    appendInt(out, bookTicker.askQtyE8); out.push_back(',');
    appendInt(out, bookTicker.tsNs);
    out.push_back(']');
    return out;
}

std::string renderBookTickerJsonLine(const replay::BookTickerRow& bookTicker,
                                     const std::vector<std::string>& aliases) {
    (void)aliases;
    return renderBookTickerJsonLine(bookTicker);
}

std::string renderDepthJsonLine(const replay::DepthRow& delta) {
    std::string out;
    out.reserve(64 + delta.levels.size() * 48);
    appendFlatOrderbook(out, delta.levels, delta.tsNs);
    return out;
}

std::string renderDepthJsonLine(const replay::DepthRow& delta,
                                const std::vector<std::string>& aliases) {
    (void)aliases;
    return renderDepthJsonLine(delta);
}

std::string renderSnapshotJson(const replay::SnapshotDocument& snapshot) {
    std::string out;
    out.reserve(64 + snapshot.levels.size() * 48);
    appendFlatOrderbook(out, snapshot.levels, snapshot.tsNs);
    out.push_back('\n');
    return out;
}

}  // namespace hftrec::capture
