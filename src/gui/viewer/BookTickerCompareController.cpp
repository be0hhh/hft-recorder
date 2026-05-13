#include "gui/viewer/BookTickerCompareController.hpp"

#include <algorithm>

#include "core/replay/SessionReplay.hpp"

namespace hftrec::gui::viewer {

namespace {

bool isLive(QString value) noexcept {
    return value.trimmed().toLower() == QStringLiteral("live");
}

bool rowsLessTs(const hftrec::replay::BookTickerRow& lhs,
                const hftrec::replay::BookTickerRow& rhs) noexcept {
    if (lhs.tsNs != rhs.tsNs) return lhs.tsNs < rhs.tsNs;
    if (lhs.ingestSeq != rhs.ingestSeq) return lhs.ingestSeq < rhs.ingestSeq;
    return lhs.captureSeq < rhs.captureSeq;
}

}  // namespace

BookTickerCompareController::BookTickerCompareController(QObject* parent)
    : QObject(parent) {
    liveTimer_.setInterval(100);
    connect(&liveTimer_, &QTimer::timeout, this, &BookTickerCompareController::pollLive_);
}

bool BookTickerCompareController::setPrimarySource(const QString& sourceId,
                                                   const QString& sourceKind,
                                                   const QString& sessionPath) {
    if (sourceId.trimmed().isEmpty()) {
        primary_ = SourceState{};
        primarySourceId_.clear();
        rebuild_();
        emit sourcesChanged();
        return true;
    }
    if (primary_.sourceId == sourceId && primary_.sourceKind == sourceKind
        && primary_.sessionPath == std::filesystem::path{sessionPath.toStdString()}) {
        return true;
    }
    if (!setSource_(primary_, sourceId, sourceKind, sessionPath)) return false;
    primarySourceId_ = sourceId;
    emit sourcesChanged();
    rebuild_();
    return true;
}

bool BookTickerCompareController::setSecondarySource(const QString& sourceId,
                                                     const QString& sourceKind,
                                                     const QString& sessionPath) {
    if (sourceId.trimmed().isEmpty()) {
        secondary_ = SourceState{};
        secondarySourceId_.clear();
        rebuild_();
        emit sourcesChanged();
        return true;
    }
    if (secondary_.sourceId == sourceId && secondary_.sourceKind == sourceKind
        && secondary_.sessionPath == std::filesystem::path{sessionPath.toStdString()}) {
        return true;
    }
    if (!setSource_(secondary_, sourceId, sourceKind, sessionPath)) return false;
    secondarySourceId_ = sourceId;
    emit sourcesChanged();
    rebuild_();
    return true;
}

void BookTickerCompareController::clear() {
    primary_ = SourceState{};
    secondary_ = SourceState{};
    primarySourceId_.clear();
    secondarySourceId_.clear();
    primaryRows_.clear();
    secondaryRows_.clear();
    spreadPoints_.clear();
    fullTsMin_ = 0;
    fullTsMax_ = 1;
    tsMin_ = 0;
    tsMax_ = 1;
    viewportInitialized_ = false;
    userViewportControl_ = false;
    liveTimer_.stop();
    setStatus_(QStringLiteral("Select two bookTicker sessions"));
    emit sourcesChanged();
    emit dataChanged();
}

void BookTickerCompareController::autoFit() {
    updateFullRange_();
    tsMin_ = fullTsMin_;
    tsMax_ = fullTsMax_;
    viewportInitialized_ = true;
    userViewportControl_ = false;
    emit viewportChanged();
    emit dataChanged();
}

void BookTickerCompareController::panTime(double fraction) {
    const qint64 span = tsMax_ - tsMin_;
    if (span <= 0) return;
    const qint64 fullSpan = std::max<qint64>(1, fullTsMax_ - fullTsMin_);
    const qint64 minBound = fullTsMin_ - fullSpan;
    const qint64 maxBound = fullTsMax_ + fullSpan;
    const qint64 delta = static_cast<qint64>(static_cast<double>(span) * fraction);
    qint64 nextMin = tsMin_ + delta;
    qint64 nextMax = tsMax_ + delta;
    if (nextMin < minBound) {
        nextMax += minBound - nextMin;
        nextMin = minBound;
    }
    if (nextMax > maxBound) {
        nextMin -= nextMax - maxBound;
        nextMax = maxBound;
    }
    if (nextMax <= nextMin) return;
    tsMin_ = nextMin;
    tsMax_ = nextMax;
    viewportInitialized_ = true;
    userViewportControl_ = true;
    emit viewportChanged();
    emit dataChanged();
}

void BookTickerCompareController::zoomTime(double factor) {
    zoomTimeAt(factor, 0.5);
}

void BookTickerCompareController::zoomTimeAt(double factor, double anchorFraction) {
    if (factor <= 0.0) return;
    anchorFraction = std::clamp(anchorFraction, 0.0, 1.0);
    const qint64 span = tsMax_ - tsMin_;
    if (span <= 0) return;
    const qint64 anchorTs = tsMin_ + static_cast<qint64>(static_cast<double>(span) * anchorFraction);
    qint64 nextSpan = static_cast<qint64>(static_cast<double>(span) / factor);
    if (nextSpan < 1000000) nextSpan = 1000000;
    const qint64 fullSpan = std::max<qint64>(1, fullTsMax_ - fullTsMin_);
    const qint64 maxSpan = fullSpan * 3;
    if (nextSpan > maxSpan) nextSpan = maxSpan;
    const qint64 minBound = fullTsMin_ - fullSpan;
    const qint64 maxBound = fullTsMax_ + fullSpan;
    qint64 nextMin = anchorTs - static_cast<qint64>(static_cast<double>(nextSpan) * anchorFraction);
    qint64 nextMax = nextMin + nextSpan;
    if (nextMin < minBound) {
        nextMax += minBound - nextMin;
        nextMin = minBound;
    }
    if (nextMax > maxBound) {
        nextMin -= nextMax - maxBound;
        nextMax = maxBound;
    }
    if (nextMin < minBound) nextMin = minBound;
    if (nextMax > maxBound) nextMax = maxBound;
    if (nextMax <= nextMin) return;
    tsMin_ = nextMin;
    tsMax_ = nextMax;
    viewportInitialized_ = true;
    userViewportControl_ = true;
    emit viewportChanged();
    emit dataChanged();
}

void BookTickerCompareController::setLiveUpdateIntervalMs(int intervalMs) {
    if (intervalMs < 16) intervalMs = 16;
    liveTimer_.setInterval(intervalMs);
}

bool BookTickerCompareController::setSource_(SourceState& state,
                                             const QString& sourceId,
                                             const QString& sourceKind,
                                             const QString& sessionPath) {
    state = SourceState{};
    state.sourceId = sourceId;
    state.sourceKind = sourceKind;
    state.sessionPath = std::filesystem::path{sessionPath.toStdString()};
    state.nextBatchId = 1;
    viewportInitialized_ = false;
    userViewportControl_ = false;

    if (sourceId.trimmed().isEmpty()) {
        state.rows.clear();
        setStatus_(QStringLiteral("Select two bookTicker sessions"));
        return false;
    }

    if (isLive(sourceKind)) {
        state.liveProvider = LiveDataRegistry::instance().makeProvider(sourceId.toStdString());
        if (!state.liveProvider) {
            setStatus_(QStringLiteral("Live source is no longer available"));
            return false;
        }
        state.liveProvider->start(LiveDataProviderConfig{state.sessionPath, {}, sourceId.toStdString()});
        liveTimer_.start();
        return true;
    }

    if (sessionPath.trimmed().isEmpty()) {
        state.rows.clear();
        setStatus_(QStringLiteral("Recorded session path is empty"));
        return false;
    }
    reloadRecorded_(state);
    return true;
}

void BookTickerCompareController::reloadRecorded_(SourceState& state) {
    state.rows.clear();
    hftrec::replay::SessionReplay replay{};
    const auto status = replay.open(state.sessionPath);
    if (!isOk(status)) {
        setStatus_(QStringLiteral("Failed to load recorded session"));
        return;
    }
    state.rows = replay.bookTickers();
    std::sort(state.rows.begin(), state.rows.end(), rowsLessTs);
}

void BookTickerCompareController::pollLive_() {
    bool changed = false;
    auto pollOne = [&](SourceState& state) {
        if (!state.liveProvider) return;
        const auto result = state.liveProvider->pollHot(state.nextBatchId++);
        if (!isOk(result.failureStatus)) {
            setStatus_(QString::fromStdString(result.failureDetail));
            return;
        }
        if (!result.batch.bookTickers.empty()) {
            state.rows.insert(state.rows.end(), result.batch.bookTickers.begin(), result.batch.bookTickers.end());
            std::sort(state.rows.begin(), state.rows.end(), rowsLessTs);
            changed = true;
        }
    };

    pollOne(primary_);
    pollOne(secondary_);
    if (changed) rebuild_();
    if (!primary_.liveProvider && !secondary_.liveProvider) liveTimer_.stop();
}

void BookTickerCompareController::rebuild_() {
    primaryRows_ = primary_.rows;
    secondaryRows_ = secondary_.rows;
    spreadPoints_ = hftrec::arbitrage::buildBestSideBookTickerSpread(primaryRows_, secondaryRows_);
    updateFullRange_();
    initializeViewportIfNeeded_();

    if (primarySourceId_.isEmpty() || secondarySourceId_.isEmpty()) {
        setStatus_(QStringLiteral("Select two bookTicker sessions"));
    } else if (primaryRows_.empty() || secondaryRows_.empty()) {
        setStatus_(QStringLiteral("Waiting for bookTicker rows from both sessions"));
    } else if (spreadPoints_.empty()) {
        setStatus_(QStringLiteral("Not enough valid bid/ask quotes to build spread"));
    } else {
        setStatus_(QStringLiteral("BookTicker comparison ready"));
    }
    emit dataChanged();
}

void BookTickerCompareController::updateFullRange_() noexcept {
    bool hasTs = false;
    auto absorb = [&](qint64 ts) noexcept {
        if (!hasTs) {
            fullTsMin_ = ts;
            fullTsMax_ = ts;
            hasTs = true;
            return;
        }
        if (ts < fullTsMin_) fullTsMin_ = ts;
        if (ts > fullTsMax_) fullTsMax_ = ts;
    };

    for (const auto& point : spreadPoints_) absorb(point.tsNs);
    if (!hasTs) {
        for (const auto& row : primaryRows_) absorb(row.tsNs);
        for (const auto& row : secondaryRows_) absorb(row.tsNs);
    }

    if (!hasTs) {
        fullTsMin_ = 0;
        fullTsMax_ = 1;
    } else if (fullTsMax_ <= fullTsMin_) {
        fullTsMax_ = fullTsMin_ + 1000000;
    }
    const qint64 fullSpan = std::max<qint64>(1, fullTsMax_ - fullTsMin_);
    const qint64 minBound = fullTsMin_ - fullSpan;
    const qint64 maxBound = fullTsMax_ + fullSpan;
    if (tsMin_ < minBound) tsMin_ = minBound;
    if (tsMax_ > maxBound) tsMax_ = maxBound;
    if (tsMax_ <= tsMin_) {
        tsMin_ = fullTsMin_;
        tsMax_ = fullTsMax_;
    }
}

void BookTickerCompareController::initializeViewportIfNeeded_() noexcept {
    if (spreadPoints_.empty()) return;
    if (viewportInitialized_ && userViewportControl_) return;
    tsMin_ = fullTsMin_;
    tsMax_ = fullTsMax_;
    viewportInitialized_ = true;
    emit viewportChanged();
}

void BookTickerCompareController::setStatus_(const QString& statusText) {
    if (statusText_ == statusText) return;
    statusText_ = statusText;
    emit statusChanged();
}

}  // namespace hftrec::gui::viewer








