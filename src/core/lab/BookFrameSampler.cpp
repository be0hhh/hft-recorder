#include "core/lab/BookFrameSampler.hpp"

#include <algorithm>

#include "core/replay/BookState.hpp"
#include "core/replay/EventRows.hpp"
#include "core/replay/JsonLineParser.hpp"

namespace hftrec::lab {

namespace {

enum class TimedKind : std::uint8_t {
    Depth = 0,
    BookTicker = 1,
};

struct TimedEvent {
    std::int64_t tsNs{0};
    std::size_t index{0};
    TimedKind kind{TimedKind::Depth};
};

template <typename MapT>
void copyTopLevels(const MapT& src,
                   std::size_t topLevelsPerSide,
                   std::vector<BookLevelView>& out) {
    out.clear();
    out.reserve(std::min(topLevelsPerSide, src.size()));
    std::size_t count = 0;
    for (const auto& [price, qty] : src) {
        if (count >= topLevelsPerSide) break;
        out.push_back(BookLevelView{price, qty});
        ++count;
    }
}

BookFrame makeFrame(const replay::BookState& book,
                    const replay::BookTickerRow* ticker,
                    std::int64_t tsNs,
                    std::size_t topLevelsPerSide) {
    BookFrame frame{};
    frame.tsNs = tsNs;
    frame.bestBidPriceE8 = book.bestBidPrice();
    frame.bestBidQtyE8 = book.bestBidQty();
    frame.bestAskPriceE8 = book.bestAskPrice();
    frame.bestAskQtyE8 = book.bestAskQty();
    frame.totalBidLevels = book.bids().size();
    frame.totalAskLevels = book.asks().size();
    copyTopLevels(book.bids(), topLevelsPerSide, frame.topBids);
    copyTopLevels(book.asks(), topLevelsPerSide, frame.topAsks);

    if (ticker != nullptr) {
        frame.hasBookTicker = true;
        frame.tickerBidPriceE8 = ticker->bidPriceE8;
        frame.tickerBidQtyE8 = ticker->bidQtyE8;
        frame.tickerAskPriceE8 = ticker->askPriceE8;
        frame.tickerAskQtyE8 = ticker->askQtyE8;
    }

    return frame;
}

}  // namespace

Status sampleGroundTruthBookFrames(const corpus::SessionCorpus& corpus,
                                   std::size_t topLevelsPerSide,
                                   std::vector<BookFrame>& out) noexcept {
    out.clear();

    replay::BookState book{};
    replay::SnapshotDocument chosenSnapshot{};
    bool hasSnapshot = false;

    for (const auto& document : corpus.snapshotDocuments) {
        replay::SnapshotDocument parsed{};
        const auto st = replay::parseSnapshotDocument(document, parsed);
        if (!isOk(st)) return st;
        if (!hasSnapshot ||
            parsed.snapshotIndex < chosenSnapshot.snapshotIndex ||
            (parsed.snapshotIndex == chosenSnapshot.snapshotIndex && parsed.tsNs < chosenSnapshot.tsNs)) {
            chosenSnapshot = std::move(parsed);
            hasSnapshot = true;
        }
    }

    std::vector<replay::DepthRow> depths;
    depths.reserve(corpus.depthLines.size());
    for (const auto& line : corpus.depthLines) {
        if (line.empty()) continue;
        replay::DepthRow row{};
        const auto st = replay::parseDepthLine(line, row);
        if (!isOk(st)) return st;
        depths.push_back(std::move(row));
    }

    std::vector<replay::BookTickerRow> tickers;
    tickers.reserve(corpus.bookTickerLines.size());
    for (const auto& line : corpus.bookTickerLines) {
        if (line.empty()) continue;
        replay::BookTickerRow row{};
        const auto st = replay::parseBookTickerLine(line, row);
        if (!isOk(st)) return st;
        tickers.push_back(std::move(row));
    }

    if (hasSnapshot) {
        book.applySnapshot(chosenSnapshot);
        out.push_back(makeFrame(book, nullptr, chosenSnapshot.tsNs, topLevelsPerSide));
    }

    std::vector<TimedEvent> events;
    events.reserve(depths.size() + tickers.size());
    for (std::size_t i = 0; i < depths.size(); ++i) {
        events.push_back(TimedEvent{depths[i].tsNs, i, TimedKind::Depth});
    }
    for (std::size_t i = 0; i < tickers.size(); ++i) {
        events.push_back(TimedEvent{tickers[i].tsNs, i, TimedKind::BookTicker});
    }

    std::stable_sort(events.begin(), events.end(), [](const TimedEvent& lhs, const TimedEvent& rhs) noexcept {
        if (lhs.tsNs != rhs.tsNs) return lhs.tsNs < rhs.tsNs;
        return static_cast<std::uint8_t>(lhs.kind) < static_cast<std::uint8_t>(rhs.kind);
    });

    const replay::BookTickerRow* currentTicker = nullptr;
    for (const auto& event : events) {
        if (event.kind == TimedKind::Depth) {
            if (event.index < depths.size()) book.applyDelta(depths[event.index]);
        } else {
            if (event.index < tickers.size()) currentTicker = &tickers[event.index];
        }
        out.push_back(makeFrame(book, currentTicker, event.tsNs, topLevelsPerSide));
    }

    return Status::Ok;
}

}  // namespace hftrec::lab
