#include "gui/viewmodels/CaptureViewModel.hpp"

#include <algorithm>
#include <filesystem>
#include <QStringList>
#include <QRegularExpression>

namespace hftrec::gui {

namespace {

QString normalizeToken(QString token) {
    token = token.trimmed().toUpper();
    if (token.isEmpty()) return token;
    if (!token.endsWith(QStringLiteral("USDT"))) token += QStringLiteral("USDT");
    return token;
}

}  // namespace

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

QString CaptureViewModel::symbolsText() const {
    return symbolsText_;
}

QString CaptureViewModel::normalizedSymbolsText() const {
    const auto symbols = normalizedSymbols_();
    QStringList parts;
    parts.reserve(static_cast<qsizetype>(symbols.size()));
    for (const auto& symbol : symbols) parts.push_back(QString::fromStdString(symbol));
    return parts.join(' ');
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

bool CaptureViewModel::sessionOpen() const {
    return !coordinators_.empty();
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

void CaptureViewModel::setSymbolsText(const QString& symbolsText) {
    const auto normalized = symbolsText.simplified();
    if (normalized == symbolsText_) return;
    symbolsText_ = normalized;
    emit symbolsTextChanged();
}

bool CaptureViewModel::startTrades() {
    if (!ensureCoordinatorBatch_()) {
        refreshState();
        return false;
    }

    const auto configs = makeConfigs();
    for (std::size_t i = 0; i < coordinators_.size() && i < configs.size(); ++i) {
        const auto status = coordinators_[i]->startTrades(configs[i]);
        if (!isOk(status)) {
            for (auto& coordinator : coordinators_) {
                if (coordinator) coordinator->stopTrades();
            }
            setStatusText(joinCoordinatorErrors_());
            refreshState();
            return false;
        }
    }

    setStatusText(QStringLiteral("Trades capture started for %1 symbol(s)").arg(coordinators_.size()));
    refreshState();
    return true;
}

void CaptureViewModel::stopTrades() {
    for (auto& coordinator : coordinators_) {
        if (coordinator) coordinator->stopTrades();
    }
    setStatusText(QStringLiteral("Trades capture stopped"));
    refreshState();
}

bool CaptureViewModel::startBookTicker() {
    if (!ensureCoordinatorBatch_()) {
        refreshState();
        return false;
    }

    const auto configs = makeConfigs();
    for (std::size_t i = 0; i < coordinators_.size() && i < configs.size(); ++i) {
        const auto status = coordinators_[i]->startBookTicker(configs[i]);
        if (!isOk(status)) {
            for (auto& coordinator : coordinators_) {
                if (coordinator) coordinator->stopBookTicker();
            }
            setStatusText(joinCoordinatorErrors_());
            refreshState();
            return false;
        }
    }

    setStatusText(QStringLiteral("BookTicker capture started for %1 symbol(s)").arg(coordinators_.size()));
    refreshState();
    return true;
}

void CaptureViewModel::stopBookTicker() {
    for (auto& coordinator : coordinators_) {
        if (coordinator) coordinator->stopBookTicker();
    }
    setStatusText(QStringLiteral("BookTicker capture stopped"));
    refreshState();
}

bool CaptureViewModel::startOrderbook() {
    if (!ensureCoordinatorBatch_()) {
        refreshState();
        return false;
    }

    const auto configs = makeConfigs();
    for (std::size_t i = 0; i < coordinators_.size() && i < configs.size(); ++i) {
        const auto status = coordinators_[i]->startOrderbook(configs[i]);
        if (!isOk(status)) {
            for (auto& coordinator : coordinators_) {
                if (coordinator) coordinator->stopOrderbook();
            }
            setStatusText(joinCoordinatorErrors_());
            refreshState();
            return false;
        }
    }

    setStatusText(QStringLiteral("Orderbook capture started for %1 symbol(s)").arg(coordinators_.size()));
    refreshState();
    return true;
}

void CaptureViewModel::stopOrderbook() {
    for (auto& coordinator : coordinators_) {
        if (coordinator) coordinator->stopOrderbook();
    }
    setStatusText(QStringLiteral("Orderbook capture stopped"));
    refreshState();
}

void CaptureViewModel::finalizeSession() {
    bool ok = true;
    for (auto& coordinator : coordinators_) {
        if (!coordinator) continue;
        const auto status = coordinator->finalizeSession();
        if (!isOk(status)) ok = false;
    }
    setStatusText(ok
        ? QStringLiteral("Session batch finalized")
        : joinCoordinatorErrors_());
    clearCoordinatorBatch_();
    refreshState();
}

std::vector<capture::CaptureConfig> CaptureViewModel::makeConfigs() const {
    std::vector<capture::CaptureConfig> configs;
    const auto symbols = normalizedSymbols_();
    configs.reserve(symbols.size());
    for (const auto& symbol : symbols) {
        capture::CaptureConfig config{};
        config.exchange = "binance";
        config.market = "futures_usd";
        config.symbols = {symbol};
        config.outputDir = std::filesystem::path{outputDirectory_.toStdString()};
        config.durationSec = 1800;
        config.snapshotIntervalSec = 60;
        configs.push_back(std::move(config));
    }
    return configs;
}

std::vector<std::string> CaptureViewModel::normalizedSymbols_() const {
    std::vector<std::string> symbols;
    const auto rawTokens = symbolsText_.split(QRegularExpression(QStringLiteral("\\s+")),
                                              Qt::SkipEmptyParts);
    symbols.reserve(rawTokens.size());
    for (const auto& rawToken : rawTokens) {
        const auto normalized = normalizeToken(rawToken);
        if (normalized.isEmpty()) continue;
        const auto asStd = normalized.toStdString();
        if (std::find(symbols.begin(), symbols.end(), asStd) == symbols.end()) {
            symbols.push_back(asStd);
        }
    }
    return symbols;
}

bool CaptureViewModel::ensureCoordinatorBatch_() {
    if (!coordinators_.empty()) return true;

    const auto configs = makeConfigs();
    if (configs.empty()) {
        setStatusText(QStringLiteral("Enter at least one symbol, e.g. ETH or BTC ETH"));
        return false;
    }

    coordinators_.clear();
    coordinators_.reserve(configs.size());
    for (std::size_t i = 0; i < configs.size(); ++i) {
        coordinators_.push_back(std::make_unique<capture::CaptureCoordinator>());
    }
    return true;
}

void CaptureViewModel::clearCoordinatorBatch_() {
    coordinators_.clear();
}

QString CaptureViewModel::joinCoordinatorErrors_() const {
    QStringList errors;
    for (const auto& coordinator : coordinators_) {
        if (!coordinator) continue;
        const auto error = QString::fromStdString(coordinator->lastError()).trimmed();
        if (!error.isEmpty() && !errors.contains(error)) errors.push_back(error);
    }
    if (errors.isEmpty()) return QStringLiteral("Operation failed");
    return errors.join(QStringLiteral(" | "));
}

void CaptureViewModel::refreshState() {
    QStringList sessionIds;
    QStringList sessionPaths;
    bool newTradesRunning = false;
    bool newBookTickerRunning = false;
    bool newOrderbookRunning = false;
    qulonglong newTradesCount = 0;
    qulonglong newBookTickerCount = 0;
    qulonglong newDepthCount = 0;
    QStringList errors;

    for (const auto& coordinator : coordinators_) {
        if (!coordinator) continue;
        const auto manifest = coordinator->manifestCopy();
        if (!manifest.sessionId.empty()) sessionIds.push_back(QString::fromStdString(manifest.sessionId));
        const auto sessionDir = coordinator->sessionDirCopy();
        if (!sessionDir.empty()) sessionPaths.push_back(QString::fromStdString(sessionDir.string()));
        newTradesRunning = newTradesRunning || coordinator->tradesRunning();
        newBookTickerRunning = newBookTickerRunning || coordinator->bookTickerRunning();
        newOrderbookRunning = newOrderbookRunning || coordinator->orderbookRunning();
        newTradesCount += static_cast<qulonglong>(coordinator->tradesCount());
        newBookTickerCount += static_cast<qulonglong>(coordinator->bookTickerCount());
        newDepthCount += static_cast<qulonglong>(coordinator->depthCount());
        const auto error = QString::fromStdString(coordinator->lastError()).trimmed();
        if (!error.isEmpty() && !errors.contains(error)) errors.push_back(error);
    }

    QString newSessionId;
    QString newSessionPath;
    if (sessionIds.size() == 1) {
        newSessionId = sessionIds.front();
        newSessionPath = sessionPaths.isEmpty() ? QString{} : sessionPaths.front();
    } else if (!sessionIds.isEmpty()) {
        newSessionId = QStringLiteral("%1 sessions").arg(sessionIds.size());
        newSessionPath = outputDirectory_;
    }
    const auto newLastError = errors.join(QStringLiteral(" | "));

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

    const auto errorText = joinCoordinatorErrors_();
    if (!errorText.isEmpty()) {
        setStatusText(errorText);
        return;
    }

    setStatusText(QString::fromUtf8(statusToString(status).data(), static_cast<int>(statusToString(status).size())));
}

}  // namespace hftrec::gui
