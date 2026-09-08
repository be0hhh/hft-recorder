#include "hftrec/RecorderApi.hpp"

#include <string>
#include <utility>

#include "RecorderReplayRows.hpp"
#include "RecorderReplaySelection.hpp"
#include "SessionReplay.hpp"

namespace hftrec {
namespace {

using detail::convert;
using detail::eventWanted;
using detail::openSelectedReplay;
using detail::wants;

void fillSummary(RecorderSession& out, const replay::SessionReplay& replay, Status status) {
    out.info.status = status;
    out.info.trades = out.tradeRows.size();
    out.info.liquidations = out.liquidationRows.size();
    out.info.bookTickers = out.bookTickerRows.size();
    out.info.depths = out.depthRows.size();
    out.info.candles = out.candleRows.size();
    out.info.snapshots = out.hasSnapshot ? 1u : 0u;
    out.info.timelineEvents = out.timelineRows.size();
    out.info.buckets = replay.buckets().size();
    out.info.firstTsNs = replay.firstTsNs();
    out.info.lastTsNs = replay.lastTsNs();
    out.info.error = std::string{replay.errorDetail()};
}

}  // namespace

void RecorderSession::clear() noexcept {
    info = RecorderSessionSummary{};
    tradeRows.clear();
    liquidationRows.clear();
    bookTickerRows.clear();
    depthRows.clear();
    candleRows.clear();
    snapshot = RecorderSnapshotDocument{};
    hasSnapshot = false;
    timelineRows.clear();
}

void RecorderSessionSet::clear() noexcept {
    primarySession.clear();
    secondarySession.clear();
    secondaryPresent = false;
}

Status loadRecorderSession(const std::filesystem::path& sessionPath,
                           RecorderChannelMask channels,
                           bool buildTimeline,
                           RecorderSession& out) noexcept {
    out.clear();
    out.info.sessionPath = sessionPath;
    if (sessionPath.empty()) {
        out.info.status = Status::InvalidArgument;
        out.info.error = "session path is empty";
        return out.info.status;
    }

    replay::SessionReplay replay;
    std::string loadError;
    const Status status = openSelectedReplay(sessionPath, channels, replay, loadError);
    if (!isOk(status)) {
        fillSummary(out, replay, status);
        if (!loadError.empty()) out.info.error = std::move(loadError);
        if (out.info.error.empty()) out.info.error = "failed to load recorder session";
        return status;
    }

    if (wants(channels, RecorderChannel_Trades)) {
        out.tradeRows.reserve(replay.trades().size());
        for (const auto& row : replay.trades()) out.tradeRows.push_back(convert(row));
    }
    if (wants(channels, RecorderChannel_Liquidations)) {
        out.liquidationRows.reserve(replay.liquidations().size());
        for (const auto& row : replay.liquidations()) out.liquidationRows.push_back(convert(row));
    }
    if (wants(channels, RecorderChannel_BookTicker)) {
        out.bookTickerRows.reserve(replay.bookTickers().size());
        for (const auto& row : replay.bookTickers()) out.bookTickerRows.push_back(convert(row));
    }
    if (wants(channels, RecorderChannel_Depth)) {
        out.depthRows.reserve(replay.depths().size());
        for (const auto& row : replay.depths()) out.depthRows.push_back(convert(row));
    }
    if (wants(channels, RecorderChannel_Candles)) {
        out.candleRows.reserve(replay.candles().size() + replay.candles2().size());
        for (const auto& row : replay.candles()) out.candleRows.push_back(convert(row));
        for (const auto& row : replay.candles2()) out.candleRows.push_back(convert(row));
    }
    if (wants(channels, RecorderChannel_Snapshot) && replay.hasSnapshot()) {
        out.snapshot = convert(replay.snapshot());
        out.hasSnapshot = true;
    }
    if (buildTimeline) {
        out.timelineRows.reserve(replay.events().size());
        for (const auto& event : replay.events()) {
            if (!eventWanted(event.kind, channels)) continue;
            out.timelineRows.push_back(RecorderTimelineEvent{
                event.tsNs,
                event.ingestSeq,
                event.rowIndex,
                convert(event.kind),
            });
        }
    }

    fillSummary(out, replay, Status::Ok);
    return Status::Ok;
}

Status loadRecorderSessions(const RecorderLoadRequest& request, RecorderSessionSet& out) noexcept {
    out.clear();
    const Status primaryStatus = loadRecorderSession(request.primarySessionPath,
                                                     request.channels,
                                                     request.buildTimeline,
                                                     out.primarySession);
    if (!isOk(primaryStatus)) return primaryStatus;

    if (!request.secondarySessionPath.empty()) {
        out.secondaryPresent = true;
        const Status secondaryStatus = loadRecorderSession(request.secondarySessionPath,
                                                           request.channels,
                                                           request.buildTimeline,
                                                           out.secondarySession);
        if (!isOk(secondaryStatus)) return secondaryStatus;
    }
    return Status::Ok;
}

}  // namespace hftrec
