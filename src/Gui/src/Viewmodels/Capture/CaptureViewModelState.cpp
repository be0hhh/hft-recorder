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

    if(viewModel.parserCapture_) {
        const auto state=viewModel.parserCapture_->snapshot();
        snapshot.sessionId=QString::fromStdString(state.sessionPath.filename().string());
        snapshot.sessionPath=QString::fromStdString(state.sessionPath.string());
        snapshot.tradesRunning=state.active && (state.channelMask&2u)!=0u;
        snapshot.bookTickerRunning=state.active && (state.channelMask&1u)!=0u;
        snapshot.orderbookRunning=state.active && (state.channelMask&4u)!=0u;
        snapshot.tradesCount=state.channelRecords[1];snapshot.bookTickerCount=state.channelRecords[0];snapshot.depthCount=state.channelRecords[2];
        if(!state.error.empty()) {
            auto message=QString::fromStdString(state.error);
            if(state.producerRetained) message+=QStringLiteral(" · Parser continues (PID %1); Finalize Session to stop it").arg(static_cast<qlonglong>(state.producerPid));
            errors.push_back(message);
        }
    }
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

    if (fullRefresh && !viewModel.parserCapture_) {
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
    const bool fullRefresh = mode == detail::CaptureRefreshMode::Full || parserCapture_!=nullptr;

    bool sessionChanged = false;
    bool channelChanged = false;
    bool countersChangedLocal = false;
    const bool open=sessionOpen();
    if(open!=lastSessionOpen_) {lastSessionOpen_=open;sessionChanged=true;}

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
        registerLiveSources_();
        emit channelStateChanged();
    }
    if (countersChangedLocal) {
        emit countersChanged();
    }
    if(parserCapture_) {
        const auto capture=parserCapture_->snapshot();
        const bool metadataChanged=capture.sourceCount!=lastCapturedSourceCount_ ||
            capture.sourceMetadataRevision!=lastSourceMetadataRevision_;
        const bool selectionChanged=capture.appliedSelectionRevision!=lastAppliedSelectionRevision_ ||
            capture.selectionPending!=lastSelectionPending_;
        if(metadataChanged || selectionChanged || (capture.connected && activeLiveSources_.isEmpty())) registerLiveSources_();
        if(selectionChanged && snapshot.errorText.isEmpty() && capture.active) {
            if(capture.selectionPending) setStatusText(QStringLiteral("Selection requested; waiting for producer confirmation"));
            else if(capture.appliedSelectionRevision!=0u)
                setStatusText(QStringLiteral("Selection applied by Parser; capture continues"));
        }
        if(capture.complete!=lastCaptureComplete_) emit channelStateChanged();
        if(capture.sourceCount!=lastCapturedSourceCount_) emit activeLiveSourcesChanged();
        lastCaptureComplete_=capture.complete;lastSelectionPending_=capture.selectionPending;
        lastAppliedSelectionRevision_=capture.appliedSelectionRevision;
        lastSourceMetadataRevision_=capture.sourceMetadataRevision;lastCapturedSourceCount_=capture.sourceCount;
        if(!capture.active && capture.complete && snapshot.errorText.isEmpty())
            setStatusText(QStringLiteral("Compressed corpus complete: %1 records, %2 sources, %3 stored/reserved bytes")
                .arg(capture.writer.recordCount).arg(capture.sourceCount).arg(capture.writer.projectedBytes)+
                (capture.producerRetained?QStringLiteral(" · Parser continues (PID %1); Finalize Session before a new capture").arg(static_cast<qlonglong>(capture.producerPid)):QString{}));
    }
}

}  // namespace hftrec::gui
