#include "core/replay/CxetReplaySessionLoader.hpp"

#if HFTREC_WITH_CXET_REPLAY

#include "cxet/replay/ReplaySession.hpp"

#include <string>
#include <utility>

namespace hftrec::replay {

namespace {

Status toStatus(cxet::replay::ReplayLoadStatus status) noexcept {
    switch (status) {
        case cxet::replay::ReplayLoadStatus::Ok: return Status::Ok;
        case cxet::replay::ReplayLoadStatus::InvalidArgument: return Status::InvalidArgument;
        case cxet::replay::ReplayLoadStatus::IoError: return Status::IoError;
        case cxet::replay::ReplayLoadStatus::CorruptData: return Status::CorruptData;
        case cxet::replay::ReplayLoadStatus::CallbackStopped: return Status::IoError;
        case cxet::replay::ReplayLoadStatus::DependencyUnavailable: return Status::Unimplemented;
    }
    return Status::CorruptData;
}

std::string formatError(const cxet::replay::ReplayLoadError& error) {
    std::string out;
    if (!error.channel.empty()) out += error.channel;
    if (!error.file.empty()) {
        if (!out.empty()) out += ": ";
        out += error.file;
    }
    if (error.line != 0u) {
        out += " line ";
        out += std::to_string(error.line);
    }
    if (!error.detail.empty()) {
        if (!out.empty()) out += ": ";
        out += error.detail;
    }
    if (out.empty()) out = cxet::replay::replayLoadStatusName(error.status);
    return out;
}

PricePair convert(const cxet::replay::PricePair& row) noexcept {
    return PricePair{row.priceE8, row.qtyE8, row.side, row.levelId};
}

std::vector<PricePair> convertLevels(const std::vector<cxet::replay::PricePair>& rows) {
    std::vector<PricePair> out;
    out.reserve(rows.size());
    for (const auto& row : rows) out.push_back(convert(row));
    return out;
}

TradeRow convert(const cxet::replay::TradeRow& row) {
    TradeRow out{};
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

BookTickerRow convert(const cxet::replay::BookTickerRow& row) {
    BookTickerRow out{};
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

DepthRow convert(const cxet::replay::DepthRow& row) {
    DepthRow out{};
    out.symbol = row.symbol;
    out.exchange = row.exchange;
    out.market = row.market;
    out.tsNs = row.tsNs;
    out.captureSeq = row.captureSeq;
    out.ingestSeq = row.ingestSeq;
    out.hasUpdateId = row.hasUpdateId;
    out.hasFirstUpdateId = row.hasFirstUpdateId;
    out.updateId = row.updateId;
    out.firstUpdateId = row.firstUpdateId;
    out.bids = convertLevels(row.bids);
    out.asks = convertLevels(row.asks);
    return out;
}

SnapshotDocument convert(const cxet::replay::SnapshotDocument& row) {
    SnapshotDocument out{};
    out.tsNs = row.tsNs;
    out.captureSeq = row.captureSeq;
    out.ingestSeq = row.ingestSeq;
    out.hasUpdateId = row.hasUpdateId;
    out.hasFirstUpdateId = row.hasFirstUpdateId;
    out.updateId = row.updateId;
    out.firstUpdateId = row.firstUpdateId;
    out.snapshotKind = row.snapshotKind;
    out.source = row.source;
    out.exchange = row.exchange;
    out.market = row.market;
    out.symbol = row.symbol;
    out.sourceTsNs = row.sourceTsNs;
    out.ingestTsNs = row.ingestTsNs;
    out.hasAnchorUpdateId = row.hasAnchorUpdateId;
    out.hasAnchorFirstUpdateId = row.hasAnchorFirstUpdateId;
    out.anchorUpdateId = row.anchorUpdateId;
    out.anchorFirstUpdateId = row.anchorFirstUpdateId;
    out.trustedReplayAnchor = row.trustedReplayAnchor;
    out.bids = convertLevels(row.bids);
    out.asks = convertLevels(row.asks);
    return out;
}

struct StreamContext {
    SessionReplay* out{nullptr};
};

bool appendTrade(const cxet::replay::TradeRow& row, void* userData) noexcept {
    auto* context = static_cast<StreamContext*>(userData);
    if (context == nullptr || context->out == nullptr) return false;
    context->out->appendTradeRow(convert(row));
    return true;
}

bool appendBookTicker(const cxet::replay::BookTickerRow& row, void* userData) noexcept {
    auto* context = static_cast<StreamContext*>(userData);
    if (context == nullptr || context->out == nullptr) return false;
    context->out->appendBookTickerRow(convert(row));
    return true;
}

bool appendDepth(const cxet::replay::DepthRow& row, void* userData) noexcept {
    auto* context = static_cast<StreamContext*>(userData);
    if (context == nullptr || context->out == nullptr) return false;
    context->out->appendDepthRow(convert(row));
    return true;
}

bool appendSnapshot(const cxet::replay::SnapshotDocument& row, void* userData) noexcept {
    auto* context = static_cast<StreamContext*>(userData);
    if (context == nullptr || context->out == nullptr) return false;
    context->out->appendSnapshotDocument(convert(row));
    return true;
}

}  // namespace

Status CxetReplaySessionLoader::loadRenderOnce(const std::filesystem::path& sessionDir,
                                               SessionReplay& out,
                                               std::string& errorDetail) const noexcept {
    return loadRenderOnce(sessionDir, {}, true, out, errorDetail);
}

Status CxetReplaySessionLoader::loadRenderOnce(const std::filesystem::path& sessionDir,
                                               const std::filesystem::path& compressedRoot,
                                               bool preferCompressed,
                                               SessionReplay& out,
                                               std::string& errorDetail) const noexcept {
    out.reset();
    StreamContext context{&out};
    cxet::replay::ReplayLoadError error{};
    const cxet::replay::ReplayStreamCallbacks callbacks{
        &context,
        appendTrade,
        appendBookTicker,
        appendDepth,
        appendSnapshot,
    };
    const auto status = cxet::replay::streamSession(
        cxet::replay::ReplayLoadConfig{sessionDir, true, compressedRoot, preferCompressed},
        callbacks,
        error);
    if (status != cxet::replay::ReplayLoadStatus::Ok) {
        errorDetail = formatError(error);
        return toStatus(status);
    }

    out.finalize();
    if (!isOk(out.status())) {
        errorDetail = out.errorDetail().empty()
            ? std::string{statusToString(out.status())}
            : std::string{out.errorDetail()};
        return out.status();
    }
    errorDetail.clear();
    return Status::Ok;
}

}  // namespace hftrec::replay

#else

namespace hftrec::replay {

Status CxetReplaySessionLoader::loadRenderOnce(const std::filesystem::path&,
                                               SessionReplay&,
                                               std::string& errorDetail) const noexcept {
    errorDetail = "CXET replay-core integration is disabled";
    return Status::InvalidArgument;
}

Status CxetReplaySessionLoader::loadRenderOnce(const std::filesystem::path&,
                                               const std::filesystem::path&,
                                               bool,
                                               SessionReplay&,
                                               std::string& errorDetail) const noexcept {
    errorDetail = "CXET replay-core integration is disabled";
    return Status::InvalidArgument;
}

}  // namespace hftrec::replay

#endif
