#include "gui/viewmodels/CaptureViewModel.hpp"

#include <QStringList>

#include "gui/viewmodels/CaptureViewModelInternal.hpp"

namespace hftrec::gui::detail {

CaptureBatchSnapshot collectBatchSnapshot(const CaptureViewModel& viewModel) {
    CaptureBatchSnapshot snapshot{};
    QStringList sessionIds;
    QStringList sessionPaths;
    QStringList errors;

    for (const auto& entry : viewModel.coordinators_) {
        const auto& coordinator = entry.coordinator;
        if (!coordinator) continue;

        const auto sessionDir = coordinator->sessionDirCopy();
        const auto manifest = coordinator->manifestCopy();
        if (!sessionDir.empty()) {
            if (!manifest.sessionId.empty()) sessionIds.push_back(QString::fromStdString(manifest.sessionId));
            sessionPaths.push_back(QString::fromStdString(sessionDir.string()));
        }

        snapshot.tradesRunning = snapshot.tradesRunning || coordinator->tradesRunning();
        snapshot.liquidationsRunning = snapshot.liquidationsRunning || coordinator->liquidationsRunning();
        snapshot.bookTickerRunning = snapshot.bookTickerRunning || coordinator->bookTickerRunning();
        snapshot.orderbookRunning = snapshot.orderbookRunning || coordinator->orderbookRunning();
        snapshot.tradesCount += static_cast<qulonglong>(coordinator->tradesCount());
        snapshot.liquidationsCount += static_cast<qulonglong>(coordinator->liquidationsCount());
        snapshot.bookTickerCount += static_cast<qulonglong>(coordinator->bookTickerCount());
        snapshot.depthCount += static_cast<qulonglong>(coordinator->depthCount());

        const auto error = QString::fromStdString(coordinator->lastError()).trimmed();
        if (!error.isEmpty() && !errors.contains(error)) errors.push_back(error);
    }

    if (sessionIds.size() == 1) {
        snapshot.sessionId = sessionIds.front();
        snapshot.sessionPath = sessionPaths.isEmpty() ? QString{} : sessionPaths.front();
    } else if (!sessionIds.isEmpty()) {
        snapshot.sessionId = QStringLiteral("%1 sessions").arg(sessionIds.size());
        snapshot.sessionPath = viewModel.outputDirectory_;
    }

    snapshot.errorText = errors.join(QStringLiteral(" | "));
    return snapshot;
}

}  // namespace hftrec::gui::detail

namespace hftrec::gui {

void CaptureViewModel::refreshState() {
    for (auto& entry : coordinators_) {
        if (entry.coordinator) entry.coordinator->reapStoppedThreads();
    }

    const auto snapshot = detail::collectBatchSnapshot(*this);

    bool sessionChanged = false;
    bool channelChanged = false;
    bool countersChangedLocal = false;

    if (snapshot.sessionId != lastSessionId_ || snapshot.sessionPath != lastSessionPath_) {
        lastSessionId_ = snapshot.sessionId;
        lastSessionPath_ = snapshot.sessionPath;
        sessionChanged = true;
    }

    if (snapshot.tradesRunning != lastTradesRunning_ ||
        snapshot.liquidationsRunning != lastLiquidationsRunning_ ||
        snapshot.bookTickerRunning != lastBookTickerRunning_ ||
        snapshot.orderbookRunning != lastOrderbookRunning_) {
        lastTradesRunning_ = snapshot.tradesRunning;
        lastLiquidationsRunning_ = snapshot.liquidationsRunning;
        lastBookTickerRunning_ = snapshot.bookTickerRunning;
        lastOrderbookRunning_ = snapshot.orderbookRunning;
        channelChanged = true;
    }

    if (snapshot.tradesCount != lastTradesCount_ ||
        snapshot.liquidationsCount != lastLiquidationsCount_ ||
        snapshot.bookTickerCount != lastBookTickerCount_ ||
        snapshot.depthCount != lastDepthCount_) {
        lastTradesCount_ = snapshot.tradesCount;
        lastLiquidationsCount_ = snapshot.liquidationsCount;
        lastBookTickerCount_ = snapshot.bookTickerCount;
        lastDepthCount_ = snapshot.depthCount;
        countersChangedLocal = true;
    }

    if (!snapshot.errorText.isEmpty() && snapshot.errorText != statusText_) {
        setStatusText(snapshot.errorText);
    }

    if (sessionChanged) emit sessionStateChanged();
    if (channelChanged) {
        reconcileActiveChannels_();
        registerLiveSources_();
        emit channelStateChanged();
    }
    if (countersChangedLocal) emit countersChanged();
}

}  // namespace hftrec::gui
