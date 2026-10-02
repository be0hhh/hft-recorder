#include "RecorderReplayRows.hpp"
#include "RecorderReplaySelection.hpp"

namespace hftrec::detail {

RecorderPriceLevel convert(const replay::PricePair& row) {
    return RecorderPriceLevel{row.priceE8, row.qtyE8, row.side};
}

std::vector<RecorderPriceLevel> convertLevels(const std::vector<replay::PricePair>& rows) {
    std::vector<RecorderPriceLevel> out;
    out.reserve(rows.size());
    for (const auto& row : rows) out.push_back(convert(row));
    return out;
}

RecorderTradeRow convert(const replay::TradeRow& row) {
    RecorderTradeRow out{};
    out.tradeId = row.tradeId;
    out.firstTradeId = row.firstTradeId;
    out.lastTradeId = row.lastTradeId;
    out.symbol = row.symbol;
    out.exchange = row.exchange;
    out.market = row.market;
    out.tsNs = row.tsNs;
    out.captureSeq = row.captureSeq;
    out.ingestSeq = row.ingestSeq;
    out.priceE8 = row.priceE8;
    out.qtyE8 = row.qtyE8;
    out.quoteQtyE8 = row.quoteQtyE8;
    out.side = row.side;
    out.isBuyerMaker = row.isBuyerMaker;
    out.sideBuy = row.sideBuy;
    return out;
}

RecorderBookTickerRow convert(const replay::BookTickerRow& row) {
    RecorderBookTickerRow out{};
    out.eventId = row.eventId;
    out.symbol = row.symbol;
    out.exchange = row.exchange;
    out.market = row.market;
    out.tsNs = row.tsNs;
    out.captureSeq = row.captureSeq;
    out.ingestSeq = row.ingestSeq;
    out.bidPriceE8 = row.bidPriceE8;
    out.bidQtyE8 = row.bidQtyE8;
    out.askPriceE8 = row.askPriceE8;
    out.askQtyE8 = row.askQtyE8;
    return out;
}

RecorderCandleRow convert(const replay::CandleRow& row) {
    RecorderCandleRow out{};
    out.tier = row.tier;
    out.tsNs = row.tsNs;
    out.exchange = row.exchange;
    out.market = row.market;
    out.symbol = row.symbol;
    out.timeframe = row.timeframe;
    out.durationNs = row.durationNs;
    out.openE8 = row.openE8;
    out.highE8 = row.highE8;
    out.lowE8 = row.lowE8;
    out.closeE8 = row.closeE8;
    out.volumeE8 = row.volumeE8;
    out.quoteAmountE8 = row.quoteAmountE8;
    out.hasOhlc = row.hasOhlc;
    return out;
}

RecorderDepthRow convert(const replay::DepthRow& row) {
    RecorderDepthRow out{};
    out.eventId = row.eventId;
    out.tsNs = row.tsNs;
    out.levels = convertLevels(row.levels);
    return out;
}

RecorderSnapshotDocument convert(const replay::SnapshotDocument& row) {
    RecorderSnapshotDocument out{};
    out.tsNs = row.tsNs;
    out.levels = convertLevels(row.levels);
    return out;
}

RecorderEventKind convert(replay::SessionReplay::EventKind kind) noexcept {
    switch (kind) {
        case replay::SessionReplay::EventKind::Depth: return RecorderEventKind::Depth;
        case replay::SessionReplay::EventKind::Trade: return RecorderEventKind::Trade;
        case replay::SessionReplay::EventKind::BookTicker: return RecorderEventKind::BookTicker;
    }
    return RecorderEventKind::Depth;
}

bool eventWanted(replay::SessionReplay::EventKind kind, RecorderChannelMask channels) noexcept {
    switch (kind) {
        case replay::SessionReplay::EventKind::Depth: return wants(channels, RecorderChannel_Depth);
        case replay::SessionReplay::EventKind::Trade: return wants(channels, RecorderChannel_Trades);
        case replay::SessionReplay::EventKind::BookTicker: return wants(channels, RecorderChannel_BookTicker);
    }
    return false;
}

}  // namespace hftrec::detail
