#include "CaptureViewModel.hpp"

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

#include "../../../../Runtime/src/Capture/Coordinator/CaptureChannelSupport.hpp"
#include "../../../../Runtime/src/Corpus/InstrumentMetadata.hpp"
#include "Corpus/Recordings/BasisChainManifest.hpp"
#include "Corpus/Recordings/BasisChainSeries.hpp"
#include "Corpus/Recordings/RecordingDiscovery.hpp"
#include "Corpus/Recordings/RecordingRoot.hpp"
#include "../../../../Runtime/src/Replay/SessionReplay.hpp"
#include "../../Viewer/Chart/Controller/LiveDataProvider.hpp"
#include "CaptureViewModelInternal.hpp"

namespace hftrec::gui {

namespace {

constexpr qint64 kNsPerMs = 1'000'000ll;
constexpr std::uint32_t kDetailedCandlesProbeLimit = 256u;

QString buildViewerSourceId(const QString& exchange, const QString& market, const QString& symbol) {
    return QStringLiteral("live:%1:%2:%3")
        .arg(exchange.trimmed().toLower(), market.trimmed().toLower(), symbol.trimmed().toUpper());
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
    return textEqualsAscii(config.exchange, "finam");
}

capture::CaptureConfig detailedCandlesProbeConfig(capture::CaptureConfig config) {
    const std::uint32_t requested = config.detailedCandlesLimit == 0u
        ? kDetailedCandlesProbeLimit
        : std::min(config.detailedCandlesLimit, kDetailedCandlesProbeLimit);
    config.detailedCandlesLimit = std::max<std::uint32_t>(1u, requested);
    if (config.detailedCandlesPageLimit == 0u || config.detailedCandlesPageLimit > config.detailedCandlesLimit) {
        config.detailedCandlesPageLimit = config.detailedCandlesLimit;
    }
    return config;
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
    return manifest.tradesCount+manifest.liquidationsCount+manifest.bookTickerCount+
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
    return coordinator.tradesRunning() || coordinator.bookTickerRunning() || coordinator.orderbookRunning();
}

}  // namespace

bool CaptureViewModel::startTrades() {
    const auto previous=desiredParserChannels_;desiredParserChannels_|=2u;
    if(!reconcileCoordinatorBatch_()) {desiredParserChannels_=previous;return false;}
    desiredTradesRunning_=true;
    setStatusText(QStringLiteral("Shared Parser capture starting; compressed binary corpus"));
    refreshState(detail::CaptureRefreshMode::Full);return true;
}

void CaptureViewModel::stopTrades() {
    const auto next=std::uint16_t(desiredParserChannels_&~std::uint16_t{2u});
    if(next==0u) {stopAllChannels();return;}
    const auto previous=desiredParserChannels_;desiredParserChannels_=next;
    if(!reconcileCoordinatorBatch_()) {desiredParserChannels_=previous;return;}
    desiredTradesRunning_=false;
    setStatusText(QStringLiteral("Channel removal requested; waiting for producer confirmation"));
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




bool CaptureViewModel::startBookTicker() {
    const auto previous=desiredParserChannels_;desiredParserChannels_|=1u;
    if(!reconcileCoordinatorBatch_()) {desiredParserChannels_=previous;return false;}
    desiredBookTickerRunning_=true;
    setStatusText(QStringLiteral("Shared Parser capture starting; compressed binary corpus"));
    refreshState(detail::CaptureRefreshMode::Full);return true;
}

void CaptureViewModel::stopBookTicker() {
    const auto next=std::uint16_t(desiredParserChannels_&~std::uint16_t{1u});
    if(next==0u) {stopAllChannels();return;}
    const auto previous=desiredParserChannels_;desiredParserChannels_=next;
    if(!reconcileCoordinatorBatch_()) {desiredParserChannels_=previous;return;}
    desiredBookTickerRunning_=false;
    setStatusText(QStringLiteral("Channel removal requested; waiting for producer confirmation"));
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
                const auto probeStatus = probe.probeDetailedCandlesOnce(detailedCandlesProbeConfig(config));
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
        const auto captureError = QString::fromStdString(existing->coordinator->lastError()).trimmed();
        const auto finalizeStatus = hasRunningChannel(*existing->coordinator)
            ? hftrec::Status::Ok
            : existing->coordinator->finalizeSession();
        const auto symbol = config.symbols.empty() ? QString{} : QString::fromStdString(config.symbols.front());
        const auto venue = QStringLiteral("%1/%2/%3/%4")
            .arg(QString::fromStdString(config.exchange),
                 QString::fromStdString(config.market),
                 symbol,
                 QString::fromStdString(config.detailedCandlesTimeframe));
        if (!isOk(status) || !isOk(finalizeStatus)) {
            ok = false;
            const auto error = captureError.isEmpty()
                ? QString::fromStdString(existing->coordinator->lastError()).trimmed()
                : captureError;
            const auto statusText = QString::fromUtf8(hftrec::statusToString(!isOk(status) ? status : finalizeStatus).data());
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
            const auto probeStatus = probe.probeDetailedCandlesOnce(detailedCandlesProbeConfig(probeConfigs.front()));
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
    const auto previous=desiredParserChannels_;desiredParserChannels_|=4u;
    if(!reconcileCoordinatorBatch_()) {desiredParserChannels_=previous;return false;}
    desiredOrderbookRunning_=true;
    setStatusText(QStringLiteral("Shared Parser capture starting; compressed binary corpus"));
    refreshState(detail::CaptureRefreshMode::Full);return true;
}

void CaptureViewModel::stopOrderbook() {
    const auto next=std::uint16_t(desiredParserChannels_&~std::uint16_t{4u});
    if(next==0u) {stopAllChannels();return;}
    const auto previous=desiredParserChannels_;desiredParserChannels_=next;
    if(!reconcileCoordinatorBatch_()) {desiredParserChannels_=previous;return;}
    desiredOrderbookRunning_=false;
    setStatusText(QStringLiteral("Channel removal requested; waiting for producer confirmation"));
    refreshState(detail::CaptureRefreshMode::Full);
}



















bool CaptureViewModel::startAllChannels() {
    const auto previous=desiredParserChannels_;desiredParserChannels_=static_cast<std::uint16_t>(captureChannels_);
    if(!reconcileCoordinatorBatch_()) {desiredParserChannels_=previous;return false;}
    desiredTradesRunning_=(desiredParserChannels_&2u)!=0u;
    desiredBookTickerRunning_=(desiredParserChannels_&1u)!=0u;
    desiredOrderbookRunning_=(desiredParserChannels_&4u)!=0u;
    setStatusText(QStringLiteral("One shared Parser capture starting for the selected batch"));
    refreshState(detail::CaptureRefreshMode::Full);return true;
}

void CaptureViewModel::stopAllChannels() {
    if(parserCapture_) parserCapture_->requestStop();
    desiredParserChannels_=0u;desiredTradesRunning_=desiredBookTickerRunning_=desiredOrderbookRunning_=false;
    setStatusText(QStringLiteral("Shared capture stop requested; freezing Parser and draining compressed writer"));
    refreshState(detail::CaptureRefreshMode::Full);
}

void CaptureViewModel::finalizeSession() {
    stopAllChannels();
    bool okay=!parserCapture_ || isOk(parserCapture_->stop());
    for(auto& entry:coordinators_) if(entry.coordinator && !isOk(entry.coordinator->finalizeSession())) okay=false;
    setStatusText(okay?QStringLiteral("Capture finalized into compressed binary corpus"):joinCoordinatorErrors_());
    viewer::LiveDataRegistry::instance().clear();publishActiveLiveSources_();
    clearCoordinatorBatch_();refreshState(detail::CaptureRefreshMode::Full);
}

bool CaptureViewModel::ensureCoordinatorBatch_() {
    // Explicit cold history actions keep their own history coordinator. The
    // realtime GUI never opens a native market session through this path.
    const auto configs=makeConfigs();if(configs.empty()) return false;
    for(const auto& config:configs) {
        const auto found=std::find_if(coordinators_.begin(),coordinators_.end(),[&](const auto& entry){return configsMatch(entry.config,config);});
        if(found==coordinators_.end()) {CoordinatorEntry entry;entry.config=config;entry.coordinator=std::make_unique<capture::CaptureCoordinator>();coordinators_.push_back(std::move(entry));}
    }
    return true;
}

capture::RecorderCaptureSessionConfig CaptureViewModel::makeParserCaptureConfig_() const {
    capture::RecorderCaptureSessionConfig config;
    config.workspaceRoot=capture::findRecorderWorkspaceRoot();config.parserTemplate=parserTemplatePath_.toStdString();
    config.envPath=envPath_.toStdString();config.outputRoot=outputDirectory_.toStdString();
    config.channelMask=desiredParserChannels_==0u?static_cast<std::uint16_t>(captureChannels_):desiredParserChannels_;
    config.durationSec=static_cast<std::uint64_t>(captureDurationSec_);config.maximumBytes=captureMaximumBytes_;
    for(const auto& item:venueChoices()) {
        const auto choice=item.toMap();const auto key=choice.value(QStringLiteral("key")).toString();
        if(!isVenueSelected(key)) continue;
        capture::RecorderCaptureVenue venue;
        venue.exchange=choice.value(QStringLiteral("exchange")).toString().toStdString();
        venue.market=choice.value(QStringLiteral("market")).toString().toStdString();venue.fullUniverse=fullUniverse_;
        if(!fullUniverse_) venue.instruments=detail::normalizedSymbols(venueSymbolsText(key));
        config.venues.push_back(std::move(venue));
    }
    return config;
}

bool CaptureViewModel::reconcileCoordinatorBatch_() {
    const auto config=makeParserCaptureConfig_();std::string error;
    if(!parserCapture_) parserCapture_=std::make_unique<capture::RecorderCaptureSession>();
    const bool active=parserCapture_->snapshot().active;
    const auto status=active?parserCapture_->updateSelection(config,error):parserCapture_->start(config,error);
    if(!active && isOk(status)) publishActiveLiveSources_();
    if(!isOk(status)) {setStatusText(QString::fromStdString(error));return false;}
    return true;
}

void CaptureViewModel::reconcileActiveChannels_() {
    if(!parserCapture_ || !parserCapture_->snapshot().active) return;
    std::string error;const auto status=parserCapture_->updateSelection(makeParserCaptureConfig_(),error);
    if(!isOk(status)) setStatusText(QString::fromStdString(error));
    else if(parserCapture_->snapshot().selectionPending) setStatusText(QStringLiteral("Selection requested; waiting for producer confirmation"));
}

void CaptureViewModel::registerLiveSources_() {
    QVariantList descriptors;
    if(parserCapture_) {
        const auto snapshot=parserCapture_->snapshot();
        const auto directory=parserCapture_->sourceDirectory();
        const auto displayed=std::min<std::size_t>(directory.size(),256u);
        for(std::size_t sourceIndex=0u;sourceIndex<displayed;++sourceIndex) {
            const auto& source=directory[sourceIndex];
            const QString exchange=QString::fromUtf8(source.venue.data(),source.venueBytes);
            const QString market=QString::fromUtf8(source.market.data(),source.marketBytes);
            const QString symbol=QString::fromUtf8(source.canonicalSymbol.data(),source.canonicalSymbolBytes);
            QVariantMap descriptor;
            descriptor.insert(QStringLiteral("id"),buildViewerSourceId(exchange,market,symbol));
            descriptor.insert(QStringLiteral("label"),QStringLiteral("Captured source | %1 | %2 | %3").arg(exchange,market,symbol));
            descriptor.insert(QStringLiteral("exchange"),exchange);descriptor.insert(QStringLiteral("market"),market);descriptor.insert(QStringLiteral("symbol"),symbol);
            descriptor.insert(QStringLiteral("sessionId"),QString::fromStdString(snapshot.sessionPath.filename().string()));
            descriptor.insert(QStringLiteral("sessionPath"),QString::fromStdString(snapshot.sessionPath.string()));
            descriptor.insert(QStringLiteral("sourceGeneration"),static_cast<qulonglong>(source.initialSourceGeneration));
            descriptor.insert(QStringLiteral("capturedChannels"),source.availableChannelMask);
            descriptor.insert(QStringLiteral("traderReplayChannels"),source.traderReplayChannelMask);
            descriptor.insert(QStringLiteral("liveAvailable"),false);
            descriptors.push_back(descriptor);
        }
    }
    // Preview must use a bounded binary adapter; a native session is never opened.
    viewer::LiveDataRegistry::instance().clear();
    if(activeLiveSources_!=descriptors) {activeLiveSources_=descriptors;emit activeLiveSourcesChanged();}
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
        coordinator->stopBookTicker();
        coordinator->stopOrderbook();
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
    if(parserCapture_ && !parserCapture_->snapshot().error.empty()) errors.push_back(QString::fromStdString(parserCapture_->snapshot().error));
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


