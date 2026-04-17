#include "gui/viewmodels/CaptureViewModel.hpp"

#include <filesystem>

namespace hftrec::gui {

CaptureViewModel::CaptureViewModel(QObject* parent)
    : QObject(parent) {
    refreshTimer_.setInterval(250);
    connect(&refreshTimer_, &QTimer::timeout, this, &CaptureViewModel::refreshState);
    refreshTimer_.start();
    refreshState();
}

QString CaptureViewModel::outputDirectory() const {
    return outputDirectory_;
}

QString CaptureViewModel::sessionId() const {
    return lastSessionId_;
}

QString CaptureViewModel::sessionPath() const {
    return lastSessionPath_;
}

QString CaptureViewModel::statusText() const {
    return statusText_;
}

bool CaptureViewModel::tradesRunning() const {
    return lastTradesRunning_;
}

bool CaptureViewModel::bookTickerRunning() const {
    return lastBookTickerRunning_;
}

bool CaptureViewModel::orderbookRunning() const {
    return lastOrderbookRunning_;
}

qulonglong CaptureViewModel::tradesCount() const {
    return lastTradesCount_;
}

qulonglong CaptureViewModel::bookTickerCount() const {
    return lastBookTickerCount_;
}

qulonglong CaptureViewModel::depthCount() const {
    return lastDepthCount_;
}

void CaptureViewModel::setOutputDirectory(const QString& outputDirectory) {
    const auto normalized = outputDirectory.trimmed();
    if (normalized.isEmpty() || normalized == outputDirectory_) {
        return;
    }
    outputDirectory_ = normalized;
    emit outputDirectoryChanged();
}

bool CaptureViewModel::startTrades() {
    const auto status = coordinator_.startTrades(makeConfig());
    setStatusFromStatus(status, "Trades capture started");
    refreshState();
    return isOk(status);
}

void CaptureViewModel::stopTrades() {
    const auto status = coordinator_.stopTrades();
    setStatusFromStatus(status, "Trades capture stopped");
    refreshState();
}

bool CaptureViewModel::startBookTicker() {
    const auto status = coordinator_.startBookTicker(makeConfig());
    setStatusFromStatus(status, "BookTicker capture started");
    refreshState();
    return isOk(status);
}

void CaptureViewModel::stopBookTicker() {
    const auto status = coordinator_.stopBookTicker();
    setStatusFromStatus(status, "BookTicker capture stopped");
    refreshState();
}

bool CaptureViewModel::startOrderbook() {
    const auto status = coordinator_.startOrderbook(makeConfig());
    setStatusFromStatus(status, "Orderbook capture started");
    refreshState();
    return isOk(status);
}

void CaptureViewModel::stopOrderbook() {
    const auto status = coordinator_.stopOrderbook();
    setStatusFromStatus(status, "Orderbook capture stopped");
    refreshState();
}

void CaptureViewModel::finalizeSession() {
    const auto status = coordinator_.finalizeSession();
    setStatusFromStatus(status, "Session finalized");
    refreshState();
}

capture::CaptureConfig CaptureViewModel::makeConfig() const {
    capture::CaptureConfig config{};
    config.exchange = "binance";
    config.market = "futures_usd";
    config.symbols = {"ETHUSDT"};
    config.outputDir = std::filesystem::path{outputDirectory_.toStdString()};
    config.durationSec = 1800;
    config.snapshotIntervalSec = 60;
    return config;
}

void CaptureViewModel::refreshState() {
    const auto manifest = coordinator_.manifestCopy();
    const auto newSessionId = QString::fromStdString(manifest.sessionId);
    const auto sessionDir = coordinator_.sessionDirCopy();
    const auto newSessionPath = sessionDir.empty() ? QString{} : QString::fromStdString(sessionDir.string());
    const auto newTradesRunning = coordinator_.tradesRunning();
    const auto newBookTickerRunning = coordinator_.bookTickerRunning();
    const auto newOrderbookRunning = coordinator_.orderbookRunning();
    const auto newTradesCount = static_cast<qulonglong>(coordinator_.tradesCount());
    const auto newBookTickerCount = static_cast<qulonglong>(coordinator_.bookTickerCount());
    const auto newDepthCount = static_cast<qulonglong>(coordinator_.depthCount());
    const auto newLastError = QString::fromStdString(coordinator_.lastError());

    bool sessionChanged = false;
    bool channelChanged = false;
    bool countersChangedLocal = false;

    if (newSessionId != lastSessionId_ || newSessionPath != lastSessionPath_) {
        lastSessionId_ = newSessionId;
        lastSessionPath_ = newSessionPath;
        sessionChanged = true;
    }

    if (newTradesRunning != lastTradesRunning_ ||
        newBookTickerRunning != lastBookTickerRunning_ ||
        newOrderbookRunning != lastOrderbookRunning_) {
        lastTradesRunning_ = newTradesRunning;
        lastBookTickerRunning_ = newBookTickerRunning;
        lastOrderbookRunning_ = newOrderbookRunning;
        channelChanged = true;
    }

    if (newTradesCount != lastTradesCount_ ||
        newBookTickerCount != lastBookTickerCount_ ||
        newDepthCount != lastDepthCount_) {
        lastTradesCount_ = newTradesCount;
        lastBookTickerCount_ = newBookTickerCount;
        lastDepthCount_ = newDepthCount;
        countersChangedLocal = true;
    }

    if (!newLastError.isEmpty() && newLastError != statusText_) {
        setStatusText(newLastError);
    }

    if (sessionChanged) {
        emit sessionStateChanged();
    }
    if (channelChanged) {
        emit channelStateChanged();
    }
    if (countersChangedLocal) {
        emit countersChanged();
    }
}

void CaptureViewModel::setStatusText(const QString& statusText) {
    if (statusText == statusText_) {
        return;
    }
    statusText_ = statusText;
    emit statusTextChanged();
}

void CaptureViewModel::setStatusFromStatus(hftrec::Status status, const QString& okText) {
    if (isOk(status)) {
        setStatusText(okText);
        return;
    }

    const auto errorText = QString::fromStdString(coordinator_.lastError());
    if (!errorText.isEmpty()) {
        setStatusText(errorText);
        return;
    }

    setStatusText(QString::fromUtf8(statusToString(status).data(), static_cast<int>(statusToString(status).size())));
}

}  // namespace hftrec::gui
