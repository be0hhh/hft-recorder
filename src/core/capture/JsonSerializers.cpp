#include "core/capture/JsonSerializers.hpp"

#include <charconv>
#include <system_error>

#include "core/common/JsonString.hpp"
#include "core/cxet_bridge/CxetCaptureBridge.hpp"
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

template <typename LevelT, typename PriceFn, typename QtyFn>
void appendLevels(std::string& out, const std::vector<LevelT>& levels, PriceFn&& price, QtyFn&& qty) {
    out.push_back('[');
    for (std::size_t i = 0; i < levels.size(); ++i) {
        if (i != 0) out.push_back(',');
        out.push_back('[');
        appendInt(out, price(levels[i]));
        out.push_back(',');
        appendInt(out, qty(levels[i]));
        out.push_back(',');
        appendInt(out, levels[i].side);
        out.push_back(',');
        appendInt(out, levels[i].levelId);
        out.push_back(']');
    }
    out.push_back(']');
}

void appendReplayLevels(std::string& out, const std::vector<replay::PricePair>& levels) {
    appendLevels(out, levels,
                 [](const auto& level) noexcept { return level.priceE8; },
                 [](const auto& level) noexcept { return level.qtyE8; });
}

}  // namespace

std::string renderTradeJsonLine(const replay::TradeRow& trade) {
    std::string out;
    out.reserve(256);
    out.push_back('[');
    appendInt(out, trade.priceE8); out.push_back(',');
    appendInt(out, trade.qtyE8); out.push_back(',');
    appendInt(out, trade.side); out.push_back(',');
    appendInt(out, trade.tsNs); out.push_back(',');
    appendInt(out, trade.tradeId); out.push_back(',');
    appendInt(out, static_cast<int>(trade.isBuyerMaker)); out.push_back(',');
    appendInt(out, trade.firstTradeId); out.push_back(',');
    appendInt(out, trade.lastTradeId); out.push_back(',');
    appendInt(out, trade.quoteQtyE8); out.push_back(',');
    appendString(out, trade.symbol); out.push_back(',');
    appendString(out, trade.exchange); out.push_back(',');
    appendString(out, trade.market); out.push_back(',');
    appendInt(out, trade.captureSeq); out.push_back(',');
    appendInt(out, trade.ingestSeq);
    out.push_back(']');
    return out;
}

std::string renderTradeJsonLine(const replay::TradeRow& trade,
                                const std::vector<std::string>& aliases) {
    std::string out;
    out.reserve(256);
    out.push_back('[');
    bool first = true;
    for (const auto& alias : aliases) {
        if (alias == price) appendValue(out, first, [&] { appendInt(out, trade.priceE8); });
        else if (alias == amount) appendValue(out, first, [&] { appendInt(out, trade.qtyE8); });
        else if (alias == side) appendValue(out, first, [&] { appendInt(out, trade.side); });
        else if (alias == timestamp) appendValue(out, first, [&] { appendInt(out, trade.tsNs); });
        else if (alias == id) appendValue(out, first, [&] { appendInt(out, trade.tradeId); });
        else if (alias == isBuyerMaker) appendValue(out, first, [&] { appendInt(out, static_cast<int>(trade.isBuyerMaker)); });
        else if (alias == firstTradeId) appendValue(out, first, [&] { appendInt(out, trade.firstTradeId); });
        else if (alias == lastTradeId) appendValue(out, first, [&] { appendInt(out, trade.lastTradeId); });
        else if (alias == quoteQty) appendValue(out, first, [&] { appendInt(out, trade.quoteQtyE8); });
        else if (alias == symbol) appendValue(out, first, [&] { appendString(out, trade.symbol); });
        else if (alias == exchange) appendValue(out, first, [&] { appendString(out, trade.exchange); });
        else if (alias == market) appendValue(out, first, [&] { appendString(out, trade.market); });
    }
    out.push_back(']');
    return out;
}

std::string renderBookTickerJsonLine(const replay::BookTickerRow& bookTicker) {
    std::string out;
    out.reserve(208);
    out.push_back('[');
    appendInt(out, bookTicker.bidPriceE8); out.push_back(',');
    appendInt(out, bookTicker.bidQtyE8); out.push_back(',');
    appendInt(out, bookTicker.askPriceE8); out.push_back(',');
    appendInt(out, bookTicker.askQtyE8); out.push_back(',');
    appendInt(out, bookTicker.tsNs); out.push_back(',');
    appendString(out, bookTicker.symbol); out.push_back(',');
    appendString(out, bookTicker.exchange); out.push_back(',');
    appendString(out, bookTicker.market); out.push_back(',');
    appendInt(out, bookTicker.captureSeq); out.push_back(',');
    appendInt(out, bookTicker.ingestSeq);
    out.push_back(']');
    return out;
}

std::string renderBookTickerJsonLine(const replay::BookTickerRow& bookTicker,
                                     const std::vector<std::string>& aliases) {
    std::string out;
    out.reserve(208);
    out.push_back('[');
    bool first = true;
    for (const auto& alias : aliases) {
        if (alias == bidPrice) appendValue(out, first, [&] { appendInt(out, bookTicker.bidPriceE8); });
        else if (alias == bidQty) appendValue(out, first, [&] { appendInt(out, bookTicker.bidQtyE8); });
        else if (alias == askPrice) appendValue(out, first, [&] { appendInt(out, bookTicker.askPriceE8); });
        else if (alias == askQty) appendValue(out, first, [&] { appendInt(out, bookTicker.askQtyE8); });
        else if (alias == timestamp) appendValue(out, first, [&] { appendInt(out, bookTicker.tsNs); });
        else if (alias == symbol) appendValue(out, first, [&] { appendString(out, bookTicker.symbol); });
        else if (alias == exchange) appendValue(out, first, [&] { appendString(out, bookTicker.exchange); });
        else if (alias == market) appendValue(out, first, [&] { appendString(out, bookTicker.market); });
    }
    out.push_back(']');
    return out;
}

std::string renderDepthJsonLine(const replay::DepthRow& delta) {
    std::string out;
    out.reserve(224 + (delta.bids.size() + delta.asks.size()) * 64);
    out.push_back('[');
    appendReplayLevels(out, delta.bids);
    out.push_back(',');
    appendReplayLevels(out, delta.asks);
    out.push_back(',');
    appendInt(out, delta.tsNs); out.push_back(',');
    appendString(out, delta.symbol); out.push_back(',');
    appendString(out, delta.exchange); out.push_back(',');
    appendString(out, delta.market); out.push_back(',');
    appendInt(out, delta.updateId); out.push_back(',');
    appendInt(out, delta.firstUpdateId); out.push_back(',');
    appendInt(out, delta.captureSeq); out.push_back(',');
    appendInt(out, delta.ingestSeq);
    out.push_back(']');
    return out;
}

std::string renderDepthJsonLine(const replay::DepthRow& delta,
                                const std::vector<std::string>& aliases) {
    std::string out;
    out.reserve(224 + (delta.bids.size() + delta.asks.size()) * 64);
    out.push_back('[');
    bool first = true;
    bool wroteBids = false;
    bool wroteAsks = false;
    for (const auto& alias : aliases) {
        if ((alias == bidPrice || alias == bidQty) && !wroteBids) {
            appendValue(out, first, [&] { appendReplayLevels(out, delta.bids); });
            wroteBids = true;
        } else if ((alias == askPrice || alias == askQty) && !wroteAsks) {
            appendValue(out, first, [&] { appendReplayLevels(out, delta.asks); });
            wroteAsks = true;
        } else if (alias == timestamp) appendValue(out, first, [&] { appendInt(out, delta.tsNs); });
        else if (alias == symbol) appendValue(out, first, [&] { appendString(out, delta.symbol); });
        else if (alias == exchange) appendValue(out, first, [&] { appendString(out, delta.exchange); });
        else if (alias == market) appendValue(out, first, [&] { appendString(out, delta.market); });
        else if (alias == updateId) appendValue(out, first, [&] { appendInt(out, delta.updateId); });
    }
    out.push_back(']');
    return out;
}

std::string renderSnapshotJson(const replay::SnapshotDocument& snapshot) {
    std::string out;
    out.reserve(320 + (snapshot.bids.size() + snapshot.asks.size()) * 64);
    out.push_back('[');
    appendReplayLevels(out, snapshot.bids);
    out.push_back(',');
    appendReplayLevels(out, snapshot.asks);
    out.push_back(',');
    appendInt(out, snapshot.tsNs); out.push_back(',');
    appendString(out, snapshot.symbol); out.push_back(',');
    appendString(out, snapshot.exchange); out.push_back(',');
    appendString(out, snapshot.market); out.push_back(',');
    appendInt(out, snapshot.updateId); out.push_back(',');
    appendInt(out, snapshot.firstUpdateId); out.push_back(',');
    appendInt(out, snapshot.sourceTsNs); out.push_back(',');
    appendInt(out, snapshot.ingestTsNs); out.push_back(',');
    appendInt(out, static_cast<int>(snapshot.trustedReplayAnchor)); out.push_back(',');
    appendInt(out, snapshot.captureSeq); out.push_back(',');
    appendInt(out, snapshot.ingestSeq); out.push_back(',');
    appendInt(out, snapshot.anchorUpdateId); out.push_back(',');
    appendInt(out, snapshot.anchorFirstUpdateId); out.push_back(',');
    appendString(out, snapshot.snapshotKind); out.push_back(',');
    appendString(out, snapshot.source);
    out.push_back(']');
    out.push_back('\n');
    return out;
}

}  // namespace hftrec::capture
