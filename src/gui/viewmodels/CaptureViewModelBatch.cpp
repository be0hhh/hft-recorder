#include "gui/viewmodels/CaptureViewModel.hpp"

#include <QDateTime>
#include <QDir>
#include <QStringList>
#include <QVariantMap>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/capture/CaptureChannelSupport.hpp"
#include "core/corpus/InstrumentMetadata.hpp"
#include "core/recordings/BasisChainManifest.hpp"
#include "core/recordings/BasisChainSeries.hpp"
#include "core/recordings/RecordingDiscovery.hpp"
#include "core/recordings/RecordingRoot.hpp"
#include "core/replay/SessionReplay.hpp"
#include "gui/viewer/LiveDataProvider.hpp"
#include "gui/viewmodels/CaptureViewModelInternal.hpp"

namespace hftrec::gui {

namespace {

constexpr qint64 kNsPerMs = 1'000'000ll;

QString buildViewerSourceId(const QString& exchange, const QString& market, const QString& symbol) {
    return QStringLiteral("live:%1:%2:%3")
        .arg(exchange.trimmed().toLower(), market.trimmed().toLower(), symbol.trimmed().toUpper());
}

QString buildLiveLabel(const QString& exchange, const QString& market, const QString& symbol) {
    const auto normalizedSymbol = symbol.trimmed().toUpper();
    return QStringLiteral("LIVE | %1 | %2 | %3")
        .arg(exchange.trimmed().isEmpty() ? QStringLiteral("Unknown Exchange") : exchange.trimmed(),
             market.trimmed().isEmpty() ? QStringLiteral("Unknown Market") : market.trimmed(),
             normalizedSymbol.isEmpty() ? QStringLiteral("Unknown Symbol") : normalizedSymbol);
}

qint64 currentUtcNs() {
    return QDateTime::currentMSecsSinceEpoch() * kNsPerMs;
}

bool retryableDetailedCandlesWindowError(const QString& error) {
    return error.contains(QStringLiteral("fetch returned no valid OHLCV rows")) ||
           error.contains(QStringLiteral("parsed_rows=0"));
}

QString endLabelForStatus(std::int64_t endNs) {
    if (endNs <= 0) return QStringLiteral("now");
    return QDateTime::fromMSecsSinceEpoch(endNs / kNsPerMs, Qt::UTC).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss'Z'"));
}

QString configKey(const capture::CaptureConfig& config) {
    const QString symbol = QString::fromStdString(config.symbols.empty() ? std::string{} : config.symbols.front());
    const int apiSlot = config.apiSlot == 0u ? 1 : static_cast<int>(config.apiSlot);
    return QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
        .arg(QString::fromStdString(config.exchange).toLower(),
             QString::fromStdString(config.market).toLower(),
             symbol,
             QString::fromStdString(config.envPath.string()),
             QString::number(apiSlot),
             QString::fromStdString(config.outputDir.string()),
             QString::fromStdString(config.detailedCandlesTimeframe));
}

bool configsMatch(const capture::CaptureConfig& lhs, const capture::CaptureConfig& rhs) {
    return configKey(lhs) == configKey(rhs);
}

bool textEqualsAscii(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size()) return false;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        char a = lhs[i];
        char b = rhs[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + ('a' - 'A'));
        if (a != b) return false;
    }
    return true;
}

bool useBulkDetailedCandles(const capture::CaptureConfig& config) {
    return textEqualsAscii(config.exchange, "finam") || textEqualsAscii(config.exchange, "finam_arena");
}

QString safePathComponent(QString value) {
    value = value.trimmed();
    if (value.isEmpty()) return QStringLiteral("UNKNOWN");
    QString out;
    out.reserve(value.size());
    for (const QChar ch : value) {
        out.push_back(ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char('-') || ch == QLatin1Char('.')
            ? ch
            : QLatin1Char('_'));
    }
    while (out.startsWith(QLatin1Char('_'))) out.remove(0, 1);
    while (out.endsWith(QLatin1Char('_'))) out.chop(1);
    return out.isEmpty() ? QStringLiteral("UNKNOWN") : out;
}

std::filesystem::path uniqueBasisGroupPath(const QString& outputDirectory,
                                           const QString& underlying,
                                           std::int64_t nowNs) {
    const std::filesystem::path root = recordings::normalizeExplicitRecordingsPath(std::filesystem::path{outputDirectory.toStdString()});
    const QString stamp = QString::fromStdString(recordings::recordingFolderTimestamp(nowNs));
    const QString base = QStringLiteral("%1_%2_basis_chain")
        .arg(stamp.isEmpty() ? QStringLiteral("undated") : stamp, safePathComponent(underlying));
    std::filesystem::path candidate = root / base.toStdString();
    std::error_code ec;
    if (!std::filesystem::exists(candidate, ec)) return candidate;
    for (int i = 2; i < 1000; ++i) {
        candidate = root / QStringLiteral("%1_%2").arg(base).arg(i, 2, 10, QLatin1Char('0')).toStdString();
        if (!std::filesystem::exists(candidate, ec)) return candidate;
    }
    return root / (base + QStringLiteral("_overflow")).toStdString();
}

QVariantMap candidateBySymbol(const QVariantList& rows, const QString& symbol) {
    for (const QVariant& value : rows) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("symbol")).toString() == symbol) return row;
    }
    return {};
}

std::uint64_t manifestTotalRows(const capture::SessionManifest& manifest) {
    return manifest.tradesCount + manifest.liquidationsCount + manifest.bookTickerCount +
           manifest.depthCount + manifest.candlesCount + manifest.candles2Count +
           manifest.markPriceCount + manifest.indexPriceCount + manifest.fundingCount +
           manifest.priceLimitCount;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in) return {};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::filesystem::path existingSessionFile(const std::filesystem::path& sessionPath,
                                          const std::string& manifestPath,
                                          std::string_view fallback) {
    if (!manifestPath.empty()) {
        std::filesystem::path path{manifestPath};
        if (path.is_relative()) path = sessionPath / path;
        std::error_code ec;
        if (std::filesystem::exists(path, ec) && !ec) return path;
    }
    const std::filesystem::path fallbackPath = sessionPath / std::string{fallback};
    std::error_code ec;
    return std::filesystem::exists(fallbackPath, ec) && !ec ? fallbackPath : std::filesystem::path{};
}

std::filesystem::path candlePathFor(const std::filesystem::path& sessionPath,
                                    const capture::SessionManifest& manifest,
                                    bool& detailed) {
    detailed = true;
    auto path = existingSessionFile(sessionPath, manifest.candles2Path, "jsonl/candles2.jsonl");
    if (!path.empty()) return path;
    path = existingSessionFile(sessionPath, {}, "jsonl/candlesv2.jsonl");
    if (!path.empty()) return path;
    detailed = false;
    return existingSessionFile(sessionPath, manifest.candlesPath, "jsonl/candles.jsonl");
}

bool loadMetadataForSeries(const std::filesystem::path& sessionPath,
                           recordings::BasisChainSeriesLegInput& out) {
    corpus::InstrumentMetadata metadata{};
    const std::string text = readFile(sessionPath / "instrument_metadata.json");
    if (text.empty() || !isOk(corpus::parseInstrumentMetadataJson(text, metadata))) return false;
    if (metadata.expiryUtcNs.has_value()) out.expiryUtcNs = *metadata.expiryUtcNs;
    if (metadata.priceBasisQtyE8.has_value()) out.priceBasisQtyE8 = *metadata.priceBasisQtyE8;
    return true;
}

bool loadSeriesLegInput(const std::filesystem::path& sessionPath,
                        const capture::SessionManifest& manifest,
                        const QString& role,
                        const QString& underlying,
                        const QString& expiration,
                        recordings::BasisChainSeriesLegInput& out,
                        QString* errorText) {
    if (errorText != nullptr) errorText->clear();
    if (sessionPath.empty()) {
        if (errorText != nullptr) *errorText = QStringLiteral("empty session path");
        return false;
    }

    out = {};
    out.role = role.toStdString();
    out.sessionId = manifest.sessionId;
    out.path = sessionPath.filename().string();
    out.exchange = manifest.exchange;
    out.market = manifest.market;
    out.symbol = manifest.symbols.empty() ? std::string{} : manifest.symbols.front();
    out.underlying = underlying.toStdString();
    out.expiration = expiration.toStdString();
    out.priceBasisQtyE8 = 100000000LL;
    (void)loadMetadataForSeries(sessionPath, out);

    bool detailed = true;
    const std::filesystem::path candlePath = candlePathFor(sessionPath, manifest, detailed);
    if (candlePath.empty()) {
        if (errorText != nullptr) *errorText = QStringLiteral("missing candles file");
        return false;
    }

    replay::SessionReplay replay{};
    const auto status = detailed ? replay.addCandles2File(candlePath) : replay.addCandlesFile(candlePath);
    if (!isOk(status)) {
        if (errorText != nullptr) {
            *errorText = QStringLiteral("failed to load candles: %1")
                .arg(QString::fromUtf8(hftrec::statusToString(status).data()));
        }
        return false;
    }
    out.candles = detailed ? replay.candles2() : replay.candles();
    if (out.candles.empty()) {
        if (errorText != nullptr) *errorText = QStringLiteral("no candles rows");
        return false;
    }
    return true;
}

capture::SessionManifest finalizedSessionManifest(const std::filesystem::path& sessionPath,
                                                  const capture::SessionManifest& fallback) {
    if (!sessionPath.empty()) {
        capture::SessionManifest parsed{};
        const std::string manifestText = readFile(sessionPath / "manifest.json");
        if (!manifestText.empty() && isOk(capture::parseManifestJson(manifestText, parsed))) return parsed;
    }
    return fallback;
}

recordings::RecordedSessionInfo recordedSessionFromManifest(const std::filesystem::path& groupPath,
                                                            const std::filesystem::path& sessionPath,
                                                            const capture::SessionManifest& manifest) {
    recordings::RecordedSessionInfo out;
    out.path = sessionPath;
    out.manifestPath = sessionPath / "manifest.json";
    out.groupPath = groupPath;
    out.sessionId = manifest.sessionId;
    out.groupId = groupPath.filename().string();
    out.exchange = manifest.exchange;
    out.market = manifest.market;
    out.symbols = manifest.symbols;
    out.normalizedSymbol = recordings::normalizeRecordingSymbol(manifest.symbols.empty() ? std::string{} : manifest.symbols.front());
    out.sessionHealth = std::string{toString(manifest.sessionHealth)};
    out.warningSummary = manifest.warningSummary;
    out.displayTime = recordings::recordingDisplayTimestamp(manifest.startedAtNs);
    out.startedAtNs = manifest.startedAtNs;
    out.endedAtNs = manifest.endedAtNs;
    out.bookTickerCount = manifest.bookTickerCount;
    out.candleCount = manifest.candles2Count > 0 ? manifest.candles2Count : manifest.candlesCount;
    out.totalRows = manifestTotalRows(manifest);
    out.grouped = true;
    out.complete = manifest.endedAtNs > 0;
    return out;
}

recordings::RecordingGroupInfo makeBasisRecordingGroup(const std::filesystem::path& groupPath,
                                                       const QString& underlying,
                                                       const std::vector<recordings::RecordedSessionInfo>& sessions) {
    recordings::RecordingGroupInfo group;
    group.path = groupPath;
    group.id = groupPath.filename().string();
    group.title = QStringLiteral("%1 %2 basis chain")
        .arg(QString::fromStdString(recordings::recordingDisplayTimestamp(currentUtcNs())),
             underlying.isEmpty() ? QStringLiteral("UNKNOWN") : underlying)
        .toStdString();
    group.normalizedSymbol = underlying.toStdString();
    group.physical = true;
    group.sessions = sessions;
    for (const auto& session : group.sessions) {
        if (group.startedAtNs == 0 || (session.startedAtNs > 0 && session.startedAtNs < group.startedAtNs)) {
            group.startedAtNs = session.startedAtNs;
        }
        if (session.endedAtNs > group.endedAtNs) group.endedAtNs = session.endedAtNs;
        group.totalRows += session.totalRows;
    }
    group.displayTime = recordings::recordingDisplayTimestamp(group.startedAtNs);
    return group;
}

bool hasRunningChannel(const capture::CaptureCoordinator& coordinator) noexcept {
    return coordinator.tradesRunning()
        || coordinator.liquidationsRunning()
        || coordinator.bookTickerRunning()
        || coordinator.orderbookRunning()
        || coordinator.markPriceRunning()
        || coordinator.indexPriceRunning()
        || coordinator.fundingRunning()
        || coordinator.priceLimitRunning();
}

bool startDesiredChannels(capture::CaptureCoordinator& coordinator,
                          const capture::CaptureConfig& config,
                          bool trades,
                          bool liquidations,
                          bool bookTicker,
                          bool orderbook,
                          bool markPrice,
                          bool indexPrice,
                          bool funding,
                          bool priceLimit,
                          std::string* skippedSummary) {
    const bool requested = trades || liquidations || bookTicker || orderbook || markPrice || indexPrice || funding || priceLimit;
    std::vector<capture::CaptureChannel> requestedChannels;
    requestedChannels.reserve(8u);
    if (trades) requestedChannels.push_back(capture::CaptureChannel::Trades);
    if (liquidations) requestedChannels.push_back(capture::CaptureChannel::Liquidations);
    if (bookTicker) requestedChannels.push_back(capture::CaptureChannel::BookTicker);
    if (orderbook) requestedChannels.push_back(capture::CaptureChannel::Orderbook);
    if (markPrice) requestedChannels.push_back(capture::CaptureChannel::MarkPrice);
    if (indexPrice) requestedChannels.push_back(capture::CaptureChannel::IndexPrice);
    if (funding) requestedChannels.push_back(capture::CaptureChannel::Funding);
    if (priceLimit) requestedChannels.push_back(capture::CaptureChannel::PriceLimit);

    const auto launchPlan = capture::buildCaptureLaunchPlan(config, requestedChannels);
    const std::string skipped = launchPlan.skippedSummary();
    if (!skipped.empty() && skippedSummary != nullptr) {
        if (!skippedSummary->empty()) *skippedSummary += " | ";
        *skippedSummary += config.exchange + "/" + config.market + " ";
        *skippedSummary += config.symbols.empty() ? std::string{} : config.symbols.front();
        *skippedSummary += ": ";
        *skippedSummary += skipped;
    }
    trades = launchPlan.channelEnabled(capture::CaptureChannel::Trades);
    liquidations = launchPlan.channelEnabled(capture::CaptureChannel::Liquidations);
    bookTicker = launchPlan.channelEnabled(capture::CaptureChannel::BookTicker);
    orderbook = launchPlan.channelEnabled(capture::CaptureChannel::Orderbook);
    markPrice = launchPlan.channelEnabled(capture::CaptureChannel::MarkPrice);
    indexPrice = launchPlan.channelEnabled(capture::CaptureChannel::IndexPrice);
    funding = launchPlan.channelEnabled(capture::CaptureChannel::Funding);
    priceLimit = launchPlan.channelEnabled(capture::CaptureChannel::PriceLimit);
    if (requested && !launchPlan.anyEnabled()) return false;
    bool running = hasRunningChannel(coordinator);

    if (trades && !coordinator.tradesRunning()) {
        if (isOk(coordinator.startTrades(config))) running = true;
    }
    if (liquidations && !coordinator.liquidationsRunning()) {
        if (isOk(coordinator.startLiquidations(config))) running = true;
    }
    if (bookTicker && !coordinator.bookTickerRunning()) {
        if (isOk(coordinator.startBookTicker(config))) running = true;
    }
    if (orderbook && !coordinator.orderbookRunning()) {
        if (isOk(coordinator.startOrderbook(config))) running = true;
    }
    if (markPrice && !coordinator.markPriceRunning()) {
        if (isOk(coordinator.startMarkPrice(config))) running = true;
    }
    if (indexPrice && !coordinator.indexPriceRunning()) {
        if (isOk(coordinator.startIndexPrice(config))) running = true;
    }
    if (funding && !coordinator.fundingRunning()) {
        if (isOk(coordinator.startFunding(config))) running = true;
    }
    if (priceLimit && !coordinator.priceLimitRunning()) {
        if (isOk(coordinator.startPriceLimit(config))) running = true;
    }
    return !requested || running;
}

}  // namespace

bool CaptureViewModel::startTrades() {
    desiredTradesRunning_ = true;
    if (!reconcileCoordinatorBatch_()) return false;
    setStatusText(QStringLiteral("Trades capture desired for %1 stream(s)").arg(coordinators_.size()));
    registerLiveSources_();
    refreshState(detail::CaptureRefreshMode::Full);
    return true;
}

void CaptureViewModel::stopTrades() {
    desiredTradesRunning_ = false;
    for (auto& entry : coordinators_) {
        if (entry.coordinator) entry.coordinator->requestStopTrades();
    }
    setStatusText(QStringLiteral("Trades stop requested"));
    refreshState(detail::CaptureRefreshMode::Full);
}

bool CaptureViewModel::startTradesHistory() {
    if (!ensureCoordinatorBatch_()) return false;

    bool ok = true;
    for (auto& entry : coordinators_) {
        if (!entry.coordinator) continue;
        const auto historyStatus = entry.coordinator->captureTradesHistoryOnce(entry.config);
        if (!isOk(historyStatus)) ok = false;
    }

    setStatusText(ok
        ? QStringLiteral("Trades history fetched for %1 stream(s)").arg(coordinators_.size())
        : joinCoordinatorErrors_());
    registerLiveSources_();
    refreshState(detail::CaptureRefreshMode::Full);
    return ok;
}

bool CaptureViewModel::startLiquidations() {
    desiredLiquidationsRunning_ = true;
    if (!reconcileCoordinatorBatch_()) return false;
    setStatusText(QStringLiteral("Liquidations capture desired for %1 stream(s)").arg(coordinators_.size()));
    registerLiveSources_();
    refreshState(detail::CaptureRefreshMode::Full);
    return true;
}

void CaptureViewModel::stopLiquidations() {
    desiredLiquidationsRunning_ = false;
    for (auto& entry : coordinators_) {
        if (entry.coordinator) entry.coordinator->requestStopLiquidations();
    }
    setStatusText(QStringLiteral("Liquidations stop requested"));
    refreshState(detail::CaptureRefreshMode::Full);
}
bool CaptureViewModel::startBookTicker() {
    desiredBookTickerRunning_ = true;
    if (!reconcileCoordinatorBatch_()) return false;
    setStatusText(QStringLiteral("BookTicker capture desired for %1 stream(s)").arg(coordinators_.size()));
    registerLiveSources_();
    refreshState(detail::CaptureRefreshMode::Full);
    return true;
}

void CaptureViewModel::stopBookTicker() {
    desiredBookTickerRunning_ = false;
    for (auto& entry : coordinators_) {
        if (entry.coordinator) entry.coordinator->requestStopBookTicker();
    }
    setStatusText(QStringLiteral("BookTicker stop requested"));
    refreshState(detail::CaptureRefreshMode::Full);
}


bool CaptureViewModel::startCandles() {
    if (!ensureCoordinatorBatch_()) return false;

    bool ok = true;
    for (auto& entry : coordinators_) {
        if (!entry.coordinator) continue;
        const auto sessionStatus = entry.coordinator->ensureSession(entry.config);
        if (!isOk(sessionStatus)) {
            ok = false;
            continue;
        }
        const auto candleStatus = entry.coordinator->captureCandlesOnce(entry.config);
        if (!isOk(candleStatus)) ok = false;
    }

    setStatusText(ok
        ? QStringLiteral("Candles history requested for %1 stream(s)").arg(coordinators_.size())
        : joinCoordinatorErrors_());
    registerLiveSources_();
    refreshState(detail::CaptureRefreshMode::Full);
    return ok;
}

bool CaptureViewModel::startDetailedCandles() {
    if (detailedCandlesMode_ == QStringLiteral("basis_chain")) {
        return startDetailedCandlesBasisChain();
    }

    QString errorText;
    const QString effectiveLeg2Symbols = detailedCandlesMode_ == QStringLiteral("pair") ? detailedCandlesLeg2SymbolsText_ : QString{};
    const auto endCandidates = detail::detailedCandlesEndCandidatesNs(detailedCandlesEndMode_,
                                                                      detailedCandlesEndUtcText_,
                                                                      detailedCandlesVenueKey_,
                                                                      effectiveLeg2Symbols.trimmed().isEmpty()
                                                                          ? QString{}
                                                                          : detailedCandlesLeg2VenueKey_,
                                                                      currentUtcNs(),
                                                                      nullptr,
                                                                      &errorText);
    if (endCandidates.empty()) {
        setStatusText(errorText.isEmpty() ? QStringLiteral("Enter detailed candles end time") : errorText);
        return false;
    }

    std::vector<capture::CaptureConfig> configs;
    std::int64_t selectedEndNs = 0;
    QStringList probeFailures;
    const bool directNowPath = endCandidates.size() == 1u && endCandidates.front() == 0;
    if (directNowPath) {
        errorText.clear();
        configs = detail::makeDetailedCandlesConfigs(outputDirectory_,
                                                     envPath_,
                                                     apiSlot_,
                                                     detailedCandlesVenueKey_,
                                                     detailedCandlesSymbolsText_,
                                                     detailedCandlesLeg2VenueKey_,
                                                     effectiveLeg2Symbols,
                                                     detailedCandlesTimeframe_,
                                                     detailedCandlesLimit_,
                                                     &errorText,
                                                     0);
        if (configs.empty()) {
            setStatusText(errorText.isEmpty() ? QStringLiteral("Enter detailed candles symbol") : errorText);
            return false;
        }
    } else {
        for (const auto endNs : endCandidates) {
            errorText.clear();
            auto candidateConfigs = detail::makeDetailedCandlesConfigs(outputDirectory_,
                                                                       envPath_,
                                                                       apiSlot_,
                                                                       detailedCandlesVenueKey_,
                                                                       detailedCandlesSymbolsText_,
                                                                       detailedCandlesLeg2VenueKey_,
                                                                       effectiveLeg2Symbols,
                                                                       detailedCandlesTimeframe_,
                                                                       detailedCandlesLimit_,
                                                                       &errorText,
                                                                       endNs);
            if (candidateConfigs.empty()) {
                setStatusText(errorText.isEmpty() ? QStringLiteral("Enter detailed candles symbol") : errorText);
                return false;
            }

            bool candidateOk = true;
            QStringList candidateFailures;
            for (const auto& config : candidateConfigs) {
                capture::CaptureCoordinator probe{};
                const auto probeStatus = probe.probeDetailedCandlesOnce(config);
                if (isOk(probeStatus)) continue;

                candidateOk = false;
                const auto symbol = config.symbols.empty() ? QString{} : QString::fromStdString(config.symbols.front());
                const auto failure = QString::fromStdString(probe.lastError()).trimmed();
                candidateFailures.push_back(QStringLiteral("%1/%2/%3/%4 end=%5: %6")
                    .arg(QString::fromStdString(config.exchange),
                         QString::fromStdString(config.market),
                         symbol,
                         QString::fromStdString(config.detailedCandlesTimeframe),
                         endLabelForStatus(endNs),
                         failure.isEmpty() ? QString::fromUtf8(hftrec::statusToString(probeStatus).data()) : failure));
            }

            if (candidateOk) {
                configs = std::move(candidateConfigs);
                selectedEndNs = endNs;
                break;
            }

            probeFailures = candidateFailures;
            bool retryable = true;
            for (const auto& failure : candidateFailures) {
                if (!retryableDetailedCandlesWindowError(failure)) {
                    retryable = false;
                    break;
                }
            }
            if (!retryable) break;
        }
    }

    if (configs.empty()) {
        setStatusText(probeFailures.isEmpty()
            ? QStringLiteral("Detailed candles2 failed: no valid smart end candidate")
            : QStringLiteral("Detailed candles2 failed: %1").arg(probeFailures.join(QStringLiteral(" | "))));
        return false;
    }

    bool ok = true;
    QStringList failures;
    QStringList successes;
    for (const auto& config : configs) {
        auto existing = std::find_if(coordinators_.begin(), coordinators_.end(), [&](const CoordinatorEntry& entry) {
            return configsMatch(entry.config, config);
        });
        if (existing == coordinators_.end()) {
            CoordinatorEntry entry{};
            entry.config = config;
            entry.coordinator = std::make_unique<capture::CaptureCoordinator>();
            coordinators_.push_back(std::move(entry));
            existing = std::prev(coordinators_.end());
        }
        if (!existing->coordinator) {
            ok = false;
            failures.push_back(QStringLiteral("missing coordinator"));
            continue;
        }
        existing->config = config;
        const auto beforeRows = existing->coordinator->candles2Count();
        const auto status = useBulkDetailedCandles(existing->config)
            ? existing->coordinator->captureDetailedCandlesBulk(existing->config)
            : existing->coordinator->captureDetailedCandlesOnce(existing->config);
        const auto afterRows = existing->coordinator->candles2Count();
        const auto symbol = config.symbols.empty() ? QString{} : QString::fromStdString(config.symbols.front());
        const auto venue = QStringLiteral("%1/%2/%3/%4")
            .arg(QString::fromStdString(config.exchange),
                 QString::fromStdString(config.market),
                 symbol,
                 QString::fromStdString(config.detailedCandlesTimeframe));
        if (!isOk(status)) {
            ok = false;
            const auto error = QString::fromStdString(existing->coordinator->lastError()).trimmed();
            const auto statusText = QString::fromUtf8(hftrec::statusToString(status).data());
            failures.push_back(error.isEmpty()
                ? QStringLiteral("%1: %2").arg(venue, statusText)
                : QStringLiteral("%1: %2").arg(venue, error));
            continue;
        }
        const auto written = afterRows >= beforeRows ? (afterRows - beforeRows) : afterRows;
        successes.push_back(QStringLiteral("%1 rows=%2/%3")
            .arg(venue,
                 QString::number(written),
                 QString::number(config.detailedCandlesLimit)));
    }

    if (ok) {
        setStatusText(QStringLiteral("Detailed candles2 downloaded end=%1: %2")
            .arg(endLabelForStatus(selectedEndNs), successes.join(QStringLiteral(" | "))));
    } else if (!failures.isEmpty()) {
        setStatusText(QStringLiteral("Detailed candles2 failed: %1").arg(failures.join(QStringLiteral(" | "))));
    } else {
        setStatusText(joinCoordinatorErrors_());
    }
    refreshState(detail::CaptureRefreshMode::Full);
    return ok;
}

bool CaptureViewModel::startDetailedCandlesBasisChain() {
    if (detailedCandlesBasisCandidateRows_.isEmpty()) {
        refreshDetailedCandlesBasisCandidates();
    }
    if (detailedCandlesBasisCandidateRows_.isEmpty()) {
        setStatusText(detailedCandlesBasisStatus_.isEmpty()
            ? QStringLiteral("Basis chain failed: build futures candidates first")
            : QStringLiteral("Basis chain failed: %1").arg(detailedCandlesBasisStatus_));
        return false;
    }

    QString errorText;
    const auto endCandidates = detail::detailedCandlesEndCandidatesNs(detailedCandlesEndMode_,
                                                                      detailedCandlesEndUtcText_,
                                                                      detailedCandlesVenueKey_,
                                                                      QStringLiteral("finam_futures"),
                                                                      currentUtcNs(),
                                                                      nullptr,
                                                                      &errorText);
    if (endCandidates.empty()) {
        setStatusText(errorText.isEmpty() ? QStringLiteral("Enter detailed candles end time") : errorText);
        return false;
    }

    std::int64_t selectedEndNs = 0;
    QString probeFailure;
    const bool directNowPath = endCandidates.size() == 1u && endCandidates.front() == 0;
    if (!directNowPath) {
        bool foundSpotWindow = false;
        for (const auto endNs : endCandidates) {
            auto probeConfigs = detail::makeDetailedCandlesBasisChainConfigs(outputDirectory_,
                                                                             envPath_,
                                                                             apiSlot_,
                                                                             detailedCandlesVenueKey_,
                                                                             detailedCandlesSymbolsText_,
                                                                             QStringLiteral("finam_futures"),
                                                                             detailedCandlesBasisCandidateRows_,
                                                                             detailedCandlesTimeframe_,
                                                                             detailedCandlesLimit_,
                                                                             &errorText,
                                                                             endNs);
            if (probeConfigs.empty()) {
                setStatusText(errorText.isEmpty() ? QStringLiteral("Basis chain config is empty") : errorText);
                return false;
            }

            capture::CaptureCoordinator probe{};
            const auto probeStatus = probe.probeDetailedCandlesOnce(probeConfigs.front());
            if (isOk(probeStatus)) {
                selectedEndNs = endNs;
                foundSpotWindow = true;
                break;
            }

            const auto failure = QString::fromStdString(probe.lastError()).trimmed();
            probeFailure = QStringLiteral("%1 end=%2: %3")
                .arg(QString::fromStdString(probeConfigs.front().symbols.empty() ? std::string{} : probeConfigs.front().symbols.front()),
                     endLabelForStatus(endNs),
                     failure.isEmpty() ? QString::fromUtf8(hftrec::statusToString(probeStatus).data()) : failure);
            if (!retryableDetailedCandlesWindowError(probeFailure)) break;
        }

        if (!foundSpotWindow) {
            setStatusText(probeFailure.isEmpty()
                ? QStringLiteral("Basis chain failed: no valid smart end candidate")
                : QStringLiteral("Basis chain failed: %1").arg(probeFailure));
            return false;
        }
    }

    const QVariantMap firstCandidate = detailedCandlesBasisCandidateRows_.isEmpty()
        ? QVariantMap{}
        : detailedCandlesBasisCandidateRows_.front().toMap();
    const QString underlying = firstCandidate.value(QStringLiteral("underlying")).toString().trimmed().isEmpty()
        ? QStringLiteral("UNKNOWN")
        : firstCandidate.value(QStringLiteral("underlying")).toString().trimmed();
    const std::int64_t nowNs = currentUtcNs();
    const std::filesystem::path groupPath = uniqueBasisGroupPath(outputDirectory_, underlying, nowNs);
    const QString groupPathText = QDir::cleanPath(QString::fromStdString(groupPath.string()));

    auto configs = detail::makeDetailedCandlesBasisChainConfigs(groupPathText,
                                                                envPath_,
                                                                apiSlot_,
                                                                detailedCandlesVenueKey_,
                                                                detailedCandlesSymbolsText_,
                                                                QStringLiteral("finam_futures"),
                                                                detailedCandlesBasisCandidateRows_,
                                                                detailedCandlesTimeframe_,
                                                                detailedCandlesLimit_,
                                                                &errorText,
                                                                selectedEndNs);
    if (configs.empty()) {
        setStatusText(errorText.isEmpty() ? QStringLiteral("Basis chain config is empty") : errorText);
        return false;
    }

    setStatusText(QStringLiteral("Basis chain downloading %1 legs to %2")
        .arg(QString::number(configs.size()), groupPathText));

    recordings::BasisChainManifest basisManifest;
    basisManifest.groupId = groupPath.filename().string();
    basisManifest.title = QStringLiteral("%1 basis chain").arg(underlying).toStdString();
    basisManifest.underlying = underlying.toStdString();
    basisManifest.timeframe = detailedCandlesTimeframe_.toStdString();
    basisManifest.requestedEndNs = selectedEndNs;
    basisManifest.requestedLimit = static_cast<std::uint32_t>(std::clamp(detailedCandlesLimit_, 1, 1'000'000));
    basisManifest.createdAtNs = nowNs;

    std::vector<recordings::RecordedSessionInfo> recordedSessions;
    std::vector<recordings::BasisChainSeriesLegInput> seriesLegs;
    QStringList failures;
    bool spotOk = false;
    int futureOkCount = 0;

    for (std::size_t i = 0; i < configs.size(); ++i) {
        const bool spotLeg = i == 0;
        const auto& config = configs[i];
        const QString symbol = QString::fromStdString(config.symbols.empty() ? std::string{} : config.symbols.front());
        const QVariantMap candidate = spotLeg ? QVariantMap{} : candidateBySymbol(detailedCandlesBasisCandidateRows_, symbol);

        capture::CaptureCoordinator coordinator{};
        const auto status = useBulkDetailedCandles(config)
            ? coordinator.captureDetailedCandlesBulk(config)
            : coordinator.captureDetailedCandlesOnce(config);
        const std::filesystem::path sessionPath = coordinator.sessionDirCopy();
        const capture::SessionManifest preFinalizeManifest = coordinator.manifestCopy();
        const auto finalizeStatus = coordinator.finalizeSession();
        const capture::SessionManifest sessionManifest = finalizedSessionManifest(sessionPath, preFinalizeManifest);

        recordings::BasisChainLegInfo leg;
        leg.role = spotLeg ? "spot" : "future";
        leg.sessionId = sessionManifest.sessionId;
        leg.path = sessionPath.empty() ? std::string{} : sessionPath.filename().string();
        leg.exchange = config.exchange;
        leg.market = config.market;
        leg.symbol = symbol.toStdString();
        leg.underlying = underlying.toStdString();
        leg.expiration = candidate.value(QStringLiteral("expiration")).toString().toStdString();
        leg.candles2Count = sessionManifest.candles2Count;

        const bool legOk = isOk(status) && isOk(finalizeStatus) && sessionManifest.candles2Count > 0;
        if (legOk) {
            leg.status = "ok";
            if (spotLeg) spotOk = true;
            else ++futureOkCount;

            recordings::BasisChainSeriesLegInput seriesLeg;
            QString seriesError;
            if (loadSeriesLegInput(sessionPath,
                                   sessionManifest,
                                   spotLeg ? QStringLiteral("spot") : QStringLiteral("future"),
                                   underlying,
                                   candidate.value(QStringLiteral("expiration")).toString(),
                                   seriesLeg,
                                   &seriesError)) {
                seriesLegs.push_back(std::move(seriesLeg));
            } else {
                failures.push_back(QStringLiteral("%1 %2 series: %3")
                    .arg(spotLeg ? QStringLiteral("spot") : QStringLiteral("future"),
                         symbol,
                         seriesError.isEmpty() ? QStringLiteral("failed to load candles") : seriesError));
            }
        } else {
            leg.status = "failed";
            QString error = QString::fromStdString(coordinator.lastError()).trimmed();
            if (error.isEmpty() && !isOk(status)) error = QString::fromUtf8(hftrec::statusToString(status).data());
            if (error.isEmpty() && !isOk(finalizeStatus)) error = QString::fromUtf8(hftrec::statusToString(finalizeStatus).data());
            if (error.isEmpty()) error = QStringLiteral("no candles2 rows");
            leg.error = error.toStdString();
            failures.push_back(QStringLiteral("%1 %2: %3")
                .arg(spotLeg ? QStringLiteral("spot") : QStringLiteral("future"), symbol, error));
        }
        basisManifest.legs.push_back(std::move(leg));

        if (!sessionPath.empty() && !sessionManifest.sessionId.empty()) {
            recordedSessions.push_back(recordedSessionFromManifest(groupPath, sessionPath, sessionManifest));
        }
    }

    QStringList manifestFailures;
    bool seriesHasSpot = false;
    bool seriesHasFuture = false;
    for (const auto& leg : seriesLegs) {
        if (leg.role == "spot") seriesHasSpot = true;
        if (leg.role == "future") seriesHasFuture = true;
    }
    if (seriesHasSpot && seriesHasFuture) {
        recordings::BasisChainSeriesStats seriesStats;
        std::string seriesError;
        if (recordings::writeBasisChainSeries(groupPath, seriesLegs, &seriesStats, &seriesError)) {
            basisManifest.seriesRows = seriesStats.rows;
            basisManifest.frontRankCount = seriesStats.frontRankCount;
        } else {
            manifestFailures.push_back(QStringLiteral("basis_chain_series: %1").arg(QString::fromStdString(seriesError)));
        }
    } else {
        manifestFailures.push_back(QStringLiteral("basis_chain_series: need loaded spot + at least one future"));
    }

    if (!recordedSessions.empty()) {
        recordings::RecordingGroupInfo group = makeBasisRecordingGroup(groupPath, underlying, recordedSessions);
        std::string groupError;
        if (!recordings::writeGroupManifest(group, &groupError)) {
            manifestFailures.push_back(QStringLiteral("group_manifest: %1").arg(QString::fromStdString(groupError)));
        }
    }

    std::string basisError;
    if (!recordings::writeBasisChainManifest(groupPath, basisManifest, &basisError)) {
        manifestFailures.push_back(QStringLiteral("basis_chain_manifest: %1").arg(QString::fromStdString(basisError)));
    }

    lastSessionId_ = QString::fromStdString(groupPath.filename().string());
    lastSessionPath_ = groupPathText;
    emit sessionStateChanged();

    const bool ok = spotOk && futureOkCount > 0 && manifestFailures.isEmpty();
    QString status = ok
        ? QStringLiteral("Basis chain downloaded: spot + %1 futures -> %2").arg(futureOkCount).arg(groupPathText)
        : QStringLiteral("Basis chain partial/failed: spot=%1 futures_ok=%2 -> %3")
              .arg(spotOk ? QStringLiteral("ok") : QStringLiteral("failed"))
              .arg(futureOkCount)
              .arg(groupPathText);
    if (!failures.isEmpty()) status += QStringLiteral(" | failed: %1").arg(failures.mid(0, 6).join(QStringLiteral(" | ")));
    if (!manifestFailures.isEmpty()) status += QStringLiteral(" | manifest: %1").arg(manifestFailures.join(QStringLiteral(" | ")));
    setStatusText(status);
    refreshState(detail::CaptureRefreshMode::Full);
    return ok;
}

bool CaptureViewModel::startOrderbook() {
    desiredOrderbookRunning_ = true;
    if (!reconcileCoordinatorBatch_()) return false;
    setStatusText(QStringLiteral("Orderbook capture desired for %1 stream(s)").arg(coordinators_.size()));
    registerLiveSources_();
    refreshState(detail::CaptureRefreshMode::Full);
    return true;
}

void CaptureViewModel::stopOrderbook() {
    desiredOrderbookRunning_ = false;
    for (auto& entry : coordinators_) {
        if (entry.coordinator) entry.coordinator->requestStopOrderbook();
    }
    setStatusText(QStringLiteral("Orderbook stop requested"));
    refreshState(detail::CaptureRefreshMode::Full);
}

bool CaptureViewModel::startMarkPrice() {
    desiredMarkPriceRunning_ = true;
    if (!reconcileCoordinatorBatch_()) return false;
    setStatusText(QStringLiteral("MarkPrice capture desired for %1 stream(s)").arg(coordinators_.size()));
    registerLiveSources_();
    refreshState(detail::CaptureRefreshMode::Full);
    return true;
}

void CaptureViewModel::stopMarkPrice() {
    desiredMarkPriceRunning_ = false;
    for (auto& entry : coordinators_) {
        if (entry.coordinator) entry.coordinator->requestStopMarkPrice();
    }
    setStatusText(QStringLiteral("MarkPrice stop requested"));
    refreshState(detail::CaptureRefreshMode::Full);
}

bool CaptureViewModel::startIndexPrice() {
    desiredIndexPriceRunning_ = true;
    if (!reconcileCoordinatorBatch_()) return false;
    setStatusText(QStringLiteral("IndexPrice capture desired for %1 stream(s)").arg(coordinators_.size()));
    registerLiveSources_();
    refreshState(detail::CaptureRefreshMode::Full);
    return true;
}

void CaptureViewModel::stopIndexPrice() {
    desiredIndexPriceRunning_ = false;
    for (auto& entry : coordinators_) {
        if (entry.coordinator) entry.coordinator->requestStopIndexPrice();
    }
    setStatusText(QStringLiteral("IndexPrice stop requested"));
    refreshState(detail::CaptureRefreshMode::Full);
}

bool CaptureViewModel::startFunding() {
    desiredFundingRunning_ = true;
    if (!reconcileCoordinatorBatch_()) return false;
    setStatusText(QStringLiteral("Funding capture desired for %1 stream(s)").arg(coordinators_.size()));
    registerLiveSources_();
    refreshState(detail::CaptureRefreshMode::Full);
    return true;
}

void CaptureViewModel::stopFunding() {
    desiredFundingRunning_ = false;
    for (auto& entry : coordinators_) {
        if (entry.coordinator) entry.coordinator->requestStopFunding();
    }
    setStatusText(QStringLiteral("Funding stop requested"));
    refreshState(detail::CaptureRefreshMode::Full);
}

bool CaptureViewModel::startPriceLimit() {
    desiredPriceLimitRunning_ = true;
    if (!reconcileCoordinatorBatch_()) return false;
    setStatusText(QStringLiteral("PriceLimit capture desired for %1 stream(s)").arg(coordinators_.size()));
    registerLiveSources_();
    refreshState(detail::CaptureRefreshMode::Full);
    return true;
}

void CaptureViewModel::stopPriceLimit() {
    desiredPriceLimitRunning_ = false;
    for (auto& entry : coordinators_) {
        if (entry.coordinator) entry.coordinator->requestStopPriceLimit();
    }
    setStatusText(QStringLiteral("PriceLimit stop requested"));
    refreshState(detail::CaptureRefreshMode::Full);
}

bool CaptureViewModel::startOpenInterest() {
    setStatusText(QStringLiteral("OpenInterest display is disabled; no data file was created"));
    return false;
}

bool CaptureViewModel::startAllChannels() {
    desiredTradesRunning_ = true;
    desiredLiquidationsRunning_ = true;
    desiredBookTickerRunning_ = true;
    desiredOrderbookRunning_ = true;
    desiredMarkPriceRunning_ = true;
    desiredIndexPriceRunning_ = true;
    desiredFundingRunning_ = true;
    desiredPriceLimitRunning_ = true;
    if (!reconcileCoordinatorBatch_()) return false;

    bool candlesOk = true;
    for (auto& entry : coordinators_) {
        if (!entry.coordinator) continue;
        const auto candleStatus = entry.coordinator->captureCandlesOnce(entry.config);
        if (!isOk(candleStatus)) candlesOk = false;
    }

    if (candlesOk && !lastSkippedChannelsSummary_.isEmpty()) {
        setStatusText(QStringLiteral("All supported capture channels desired for %1 stream(s); skipped: %2")
                          .arg(coordinators_.size())
                          .arg(lastSkippedChannelsSummary_));
    } else {
        setStatusText(candlesOk
            ? QStringLiteral("All available capture channels desired for %1 stream(s)").arg(coordinators_.size())
            : joinCoordinatorErrors_());
    }
    registerLiveSources_();
    refreshState(detail::CaptureRefreshMode::Full);
    return candlesOk;
}

void CaptureViewModel::stopAllChannels() {
    desiredTradesRunning_ = false;
    desiredLiquidationsRunning_ = false;
    desiredBookTickerRunning_ = false;
    desiredOrderbookRunning_ = false;
    desiredMarkPriceRunning_ = false;
    desiredIndexPriceRunning_ = false;
    desiredFundingRunning_ = false;
    desiredPriceLimitRunning_ = false;
    for (auto& entry : coordinators_) {
        if (!entry.coordinator) continue;
        entry.coordinator->requestStopTrades();
        entry.coordinator->requestStopLiquidations();
        entry.coordinator->requestStopBookTicker();
        entry.coordinator->requestStopOrderbook();
        entry.coordinator->requestStopMarkPrice();
        entry.coordinator->requestStopIndexPrice();
        entry.coordinator->requestStopFunding();
        entry.coordinator->requestStopPriceLimit();
    }
    setStatusText(QStringLiteral("All capture channels stop requested"));
    refreshState(detail::CaptureRefreshMode::Full);
}

void CaptureViewModel::finalizeSession() {
    bool ok = true;
    for (auto& entry : coordinators_) {
        if (!entry.coordinator) continue;
        const auto status = entry.coordinator->finalizeSession();
        if (!isOk(status)) ok = false;
    }
    setStatusText(ok ? QStringLiteral("Session batch finalized") : joinCoordinatorErrors_());
    viewer::LiveDataRegistry::instance().clear();
    publishActiveLiveSources_();
    clearCoordinatorBatch_();
    refreshState(detail::CaptureRefreshMode::Full);
}

bool CaptureViewModel::ensureCoordinatorBatch_() {
    return reconcileCoordinatorBatch_();
}

bool CaptureViewModel::reconcileCoordinatorBatch_() {
    const auto configs = makeConfigs();
    const QString missingSymbols = detail::missingVenueSymbolsText(selectedVenueKeys_, venueSymbolsTexts_);
    if (configs.empty()) {
        for (auto& entry : coordinators_) {
            if (entry.coordinator) (void)entry.coordinator->finalizeSession();
        }
        coordinators_.clear();
        viewer::LiveDataRegistry::instance().clear();
        publishActiveLiveSources_();
        setStatusText(missingSymbols.isEmpty() ? QStringLiteral("Enter at least one venue symbol") : missingSymbols);
        return false;
    }
    if (!missingSymbols.isEmpty()) {
        setStatusText(missingSymbols);
    }

    for (auto it = coordinators_.begin(); it != coordinators_.end();) {
        const bool stillDesired = std::any_of(configs.begin(), configs.end(), [&](const capture::CaptureConfig& config) {
            return configsMatch(it->config, config);
        });
        if (stillDesired) {
            ++it;
            continue;
        }
        if (it->coordinator) (void)it->coordinator->finalizeSession();
        it = coordinators_.erase(it);
    }

    for (const auto& config : configs) {
        const auto existing = std::find_if(coordinators_.begin(), coordinators_.end(), [&](const CoordinatorEntry& entry) {
            return configsMatch(entry.config, config);
        });
        if (existing != coordinators_.end()) continue;

        CoordinatorEntry entry{};
        entry.config = config;
        entry.coordinator = std::make_unique<capture::CaptureCoordinator>();
        coordinators_.push_back(std::move(entry));
    }

    bool anyRunning = false;
    bool anyFailed = false;
    std::string skippedSummary;
    for (auto& entry : coordinators_) {
        if (!entry.coordinator) continue;
        if (!startDesiredChannels(*entry.coordinator,
                                  entry.config,
                                  desiredTradesRunning_,
                                  desiredLiquidationsRunning_,
                                  desiredBookTickerRunning_,
                                  desiredOrderbookRunning_,
                                  desiredMarkPriceRunning_,
                                  desiredIndexPriceRunning_,
                                  desiredFundingRunning_,
                                  desiredPriceLimitRunning_,
                                  &skippedSummary)) {
            anyFailed = true;
        }
        anyRunning = anyRunning || hasRunningChannel(*entry.coordinator);
    }

    if (anyFailed && !anyRunning) {
        lastSkippedChannelsSummary_ = QString::fromStdString(skippedSummary);
        setStatusText(lastSkippedChannelsSummary_.isEmpty()
            ? joinCoordinatorErrors_()
            : QStringLiteral("No supported capture channels for requested stream(s): %1").arg(lastSkippedChannelsSummary_));
        refreshState(detail::CaptureRefreshMode::Full);
        return false;
    }

    lastSkippedChannelsSummary_ = QString::fromStdString(skippedSummary);
    if (!lastSkippedChannelsSummary_.isEmpty()) {
        setStatusText(QStringLiteral("Skipped unsupported channel(s): %1").arg(lastSkippedChannelsSummary_));
    }

    return true;
}

void CaptureViewModel::reconcileActiveChannels_() {
    if (!(desiredTradesRunning_ || desiredLiquidationsRunning_ || desiredBookTickerRunning_ || desiredOrderbookRunning_
          || desiredMarkPriceRunning_ || desiredIndexPriceRunning_ || desiredFundingRunning_ || desiredPriceLimitRunning_)) return;
    (void)reconcileCoordinatorBatch_();
    registerLiveSources_();
}

void CaptureViewModel::registerLiveSources_() {
    std::vector<viewer::LiveDataRegistry::RegisteredSource> sources;
    QVariantList descriptors;
    sources.reserve(coordinators_.size());
    descriptors.reserve(static_cast<qsizetype>(coordinators_.size()));

    for (const auto& entry : coordinators_) {
        const auto& coordinator = entry.coordinator;
        if (!coordinator) continue;
        const auto manifest = coordinator->manifestCopy();
        const bool hasLiveChannel = coordinator->tradesRunning() || coordinator->liquidationsRunning()
            || coordinator->bookTickerRunning() || coordinator->orderbookRunning()
            || coordinator->markPriceRunning() || coordinator->indexPriceRunning()
            || coordinator->fundingRunning() || coordinator->priceLimitRunning();
        if (!hasLiveChannel) continue;
        if (coordinator->eventSource() == nullptr) continue;

        const QString exchange = QString::fromStdString(manifest.exchange);
        const QString market = QString::fromStdString(manifest.market);
        const QString symbol = QString::fromStdString(manifest.symbols.empty() ? std::string{} : manifest.symbols.front()).trimmed().toUpper();
        const QString sourceId = buildViewerSourceId(exchange, market, symbol);
        sources.push_back(viewer::LiveDataRegistry::RegisteredSource{
            sourceId.toStdString(),
            exchange.toStdString(),
            market.toStdString(),
            symbol.toStdString(),
            manifest.sessionId,
            coordinator->sessionDirCopy(),
            coordinator.get()});

        QVariantMap descriptor;
        descriptor.insert(QStringLiteral("id"), sourceId);
        descriptor.insert(QStringLiteral("label"), buildLiveLabel(exchange, market, symbol));
        descriptor.insert(QStringLiteral("exchange"), exchange);
        descriptor.insert(QStringLiteral("market"), market);
        descriptor.insert(QStringLiteral("symbol"), symbol);
        descriptor.insert(QStringLiteral("sessionId"), QString::fromStdString(manifest.sessionId));
        descriptor.insert(QStringLiteral("sessionPath"), QString::fromStdString(coordinator->sessionDirCopy().string()));
        descriptor.insert(QStringLiteral("startedAtNs"), static_cast<qlonglong>(manifest.startedAtNs));
        descriptor.insert(QStringLiteral("liveAvailable"), true);
        descriptor.insert(QStringLiteral("bookTickerRunning"), coordinator->bookTickerRunning());
        descriptor.insert(QStringLiteral("bookTickerCount"), static_cast<int>(coordinator->bookTickerCount()));
        descriptors.push_back(descriptor);
    }

    viewer::LiveDataRegistry::instance().setSources(std::move(sources));
    if (activeLiveSources_ != descriptors) {
        activeLiveSources_ = descriptors;
        emit activeLiveSourcesChanged();
    }
}

void CaptureViewModel::publishActiveLiveSources_() {
    if (activeLiveSources_.isEmpty()) return;
    activeLiveSources_.clear();
    emit activeLiveSourcesChanged();
}

void CaptureViewModel::clearCoordinatorBatch_() {
    coordinators_.clear();
}

void CaptureViewModel::abortCoordinatorBatch_(const QString& fallbackStatus) {
    QStringList errors;
    for (auto& entry : coordinators_) {
        const auto& coordinator = entry.coordinator;
        if (!coordinator) continue;
        const auto preFinalizeError = QString::fromStdString(coordinator->lastError()).trimmed();
        coordinator->stopTrades();
        coordinator->stopLiquidations();
        coordinator->stopBookTicker();
        coordinator->stopOrderbook();
        coordinator->stopMarkPrice();
        coordinator->stopIndexPrice();
        coordinator->stopFunding();
        coordinator->stopPriceLimit();
        const auto status = coordinator->finalizeSession();
        if (!preFinalizeError.isEmpty() && !errors.contains(preFinalizeError)) errors.push_back(preFinalizeError);
        if (!isOk(status) && preFinalizeError.isEmpty()) {
            const auto statusText = QString::fromUtf8(hftrec::statusToString(status).data());
            if (!errors.contains(statusText)) errors.push_back(statusText);
        }
    }

    viewer::LiveDataRegistry::instance().clear();
    publishActiveLiveSources_();
    clearCoordinatorBatch_();
    setStatusText(errors.isEmpty() ? fallbackStatus : errors.join(QStringLiteral(" | ")));
}

QString CaptureViewModel::joinCoordinatorErrors_() const {
    QStringList errors;
    for (const auto& entry : coordinators_) {
        const auto& coordinator = entry.coordinator;
        if (!coordinator) continue;
        const auto error = QString::fromStdString(coordinator->lastError()).trimmed();
        if (!error.isEmpty() && !errors.contains(error)) errors.push_back(error);
    }
    if (errors.isEmpty()) return QStringLiteral("Operation failed");
    return errors.join(QStringLiteral(" | "));
}

}  // namespace hftrec::gui


