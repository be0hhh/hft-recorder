#include "CaptureViewModel.hpp"

#include <QStringList>

#include "CaptureViewModelInternal.hpp"

namespace hftrec::gui::detail {

CaptureBatchSnapshot collectBatchSnapshot(const CaptureViewModel& viewModel, CaptureRefreshMode mode) {
    CaptureBatchSnapshot snapshot{};
    QStringList sessionIds;
    QStringList sessionPaths;
    QStringList errors;
    const bool fullRefresh = mode == CaptureRefreshMode::Full;

    for (const auto& entry : viewModel.coordinators_) {
        const auto& coordinator = entry.coordinator;
        if (!coordinator) continue;

        if (fullRefresh) {
            const auto sessionDir = coordinator->sessionDirCopy();
            const auto manifest = coordinator->manifestCopy();
            if (!sessionDir.empty()) {
                if (!manifest.sessionId.empty()) sessionIds.push_back(QString::fromStdString(manifest.sessionId));
                sessionPaths.push_back(QString::fromStdString(sessionDir.string()));
            }
        }

        snapshot.tradesRunning = snapshot.tradesRunning || coordinator->tradesRunning();
        snapshot.bookTickerRunning = snapshot.bookTickerRunning || coordinator->bookTickerRunning();
        snapshot.orderbookRunning = snapshot.orderbookRunning || coordinator->orderbookRunning();
        if (fullRefresh) {
            snapshot.tradesCount += static_cast<qulonglong>(coordinator->tradesCount());
            snapshot.bookTickerCount += static_cast<qulonglong>(coordinator->bookTickerCount());
            snapshot.candlesCount += static_cast<qulonglong>(coordinator->candlesCount());
            snapshot.candles2Count += static_cast<qulonglong>(coordinator->candles2Count());
            snapshot.depthCount += static_cast<qulonglong>(coordinator->depthCount());
        }

        const auto error = QString::fromStdString(coordinator->lastError()).trimmed();
        if (!error.isEmpty() && !errors.contains(error)) errors.push_back(error);
    }

    if (fullRefresh) {
        if (sessionIds.size() == 1) {
            snapshot.sessionId = sessionIds.front();
            snapshot.sessionPath = sessionPaths.isEmpty() ? QString{} : sessionPaths.front();
        } else if (!sessionIds.isEmpty()) {
            snapshot.sessionId = QStringLiteral("%1 sessions").arg(sessionIds.size());
            snapshot.sessionPath = viewModel.outputDirectory_;
        }
    }

    snapshot.errorText = errors.join(QStringLiteral(" | "));

    return snapshot;
}

}  // namespace hftrec::gui::detail

namespace hftrec::gui {

void CaptureViewModel::refreshState(detail::CaptureRefreshMode mode) {
    for (auto& entry : coordinators_) {
        if (entry.coordinator) entry.coordinator->reapStoppedThreads();
    }

    const auto snapshot = detail::collectBatchSnapshot(*this, mode);
    const bool fullRefresh = mode == detail::CaptureRefreshMode::Full;

    bool sessionChanged = false;
    bool channelChanged = false;
    bool countersChangedLocal = false;

    if (fullRefresh && (snapshot.sessionId != lastSessionId_ || snapshot.sessionPath != lastSessionPath_)) {
        lastSessionId_ = snapshot.sessionId;
        lastSessionPath_ = snapshot.sessionPath;
        sessionChanged = true;
    }

    if (snapshot.tradesRunning != lastTradesRunning_ ||
        snapshot.bookTickerRunning != lastBookTickerRunning_ ||
        snapshot.orderbookRunning != lastOrderbookRunning_) {
        lastTradesRunning_ = snapshot.tradesRunning;
        lastBookTickerRunning_ = snapshot.bookTickerRunning;
        lastOrderbookRunning_ = snapshot.orderbookRunning;
        channelChanged = true;
    }

    if (fullRefresh &&
        (snapshot.tradesCount != lastTradesCount_ ||
        snapshot.bookTickerCount != lastBookTickerCount_ ||
        snapshot.candlesCount != lastCandlesCount_ ||
        snapshot.candles2Count != lastCandles2Count_ ||
        snapshot.depthCount != lastDepthCount_)) {
        lastTradesCount_ = snapshot.tradesCount;
        lastBookTickerCount_ = snapshot.bookTickerCount;
        lastCandlesCount_ = snapshot.candlesCount;
        lastCandles2Count_ = snapshot.candles2Count;
        lastDepthCount_ = snapshot.depthCount;
        countersChangedLocal = true;
    }

    if (!snapshot.errorText.isEmpty() && snapshot.errorText != statusText_) {
        setStatusText(snapshot.errorText);
    }

    if (sessionChanged) emit sessionStateChanged();
    if (channelChanged) {
        if (snapshot.errorText.isEmpty()) reconcileActiveChannels_();
        registerLiveSources_();
        emit channelStateChanged();
    }
    if (countersChangedLocal) {
        emit countersChanged();
        if (!activeLiveSources_.isEmpty()) registerLiveSources_();
    }
    if (!fullRefresh && !activeLiveSources_.isEmpty()) registerLiveSources_();
}

}  // namespace hftrec::gui

