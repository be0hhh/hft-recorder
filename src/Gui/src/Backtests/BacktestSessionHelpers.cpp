#include "BacktestSessionHelpers.hpp"

#include "Corpus/Recordings/RecordingDiscovery.hpp"
#include "Corpus/Recordings/RecordingRoot.hpp"
#include "../../../Runtime/src/Capture/SessionManifest.hpp"
#include "BacktestResultHelpers.hpp"

#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <array>
#include <cstdint>
#include <limits>
#include <utility>

namespace hftrec::gui {

SessionManifestSnapshot::SessionManifestSnapshot(SessionManifestStatus status,
                                                 QString sessionPath,
                                                 QString manifestPath,
                                                 QJsonObject object,
                                                 QString error,
                                                 qint64 size,
                                                 qint64 lastModifiedMs)
    : status_(status),
      sessionPath_(std::move(sessionPath)),
      manifestPath_(std::move(manifestPath)),
      object_(std::move(object)),
      error_(std::move(error)),
      size_(size),
      lastModifiedMs_(lastModifiedMs) {}

SessionManifestSnapshot loadSessionManifestSnapshot(const QString& sessionPath) {
    if (sessionPath.trimmed().isEmpty()) {
        return {SessionManifestStatus::Missing,
                {},
                {},
                {},
                QStringLiteral("session path is empty"),
                -1,
                0};
    }
    const QString normalizedSessionPath = QDir::cleanPath(sessionPath);
    const QString manifestPath =
        QDir(normalizedSessionPath).absoluteFilePath(QStringLiteral("manifest.json"));
    constexpr int kMaxReadAttempts = 2;
    for (int attempt = 0; attempt < kMaxReadAttempts; ++attempt) {
        const QFileInfo before(manifestPath);
        if (!before.isFile()) {
            return {SessionManifestStatus::Missing,
                    normalizedSessionPath,
                    manifestPath,
                    {},
                    QStringLiteral("session manifest is missing"),
                    -1,
                    0};
        }
        const qint64 beforeSize = before.size();
        const qint64 beforeModifiedMs = before.lastModified().toMSecsSinceEpoch();
        QFile file(manifestPath);
        if (!file.open(QIODevice::ReadOnly)) {
            return {SessionManifestStatus::Unreadable,
                    normalizedSessionPath,
                    manifestPath,
                    {},
                    QStringLiteral("session manifest is unreadable: %1").arg(file.errorString()),
                    beforeSize,
                    beforeModifiedMs};
        }
        const QByteArray bytes = file.readAll();
        const QFileDevice::FileError readError = file.error();
        file.close();
        const QFileInfo after(manifestPath);
        const qint64 afterSize = after.isFile() ? after.size() : -1;
        const qint64 afterModifiedMs = after.isFile()
            ? after.lastModified().toMSecsSinceEpoch()
            : 0;
        const bool generationChanged =
            !after.isFile() ||
            beforeSize != afterSize ||
            beforeModifiedMs != afterModifiedMs ||
            bytes.size() != beforeSize;
        if (readError != QFileDevice::NoError) {
            return {SessionManifestStatus::Unreadable,
                    normalizedSessionPath,
                    manifestPath,
                    {},
                    QStringLiteral("session manifest read failed: %1").arg(file.errorString()),
                    afterSize,
                    afterModifiedMs};
        }
        if (generationChanged) {
            if (attempt + 1 < kMaxReadAttempts) continue;
            return {SessionManifestStatus::ChangedDuringRead,
                    normalizedSessionPath,
                    manifestPath,
                    {},
                    QStringLiteral("session manifest changed while being read"),
                    afterSize,
                    afterModifiedMs};
        }

        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            const QString detail = parseError.error != QJsonParseError::NoError
                ? parseError.errorString()
                : QStringLiteral("root is not an object");
            return {SessionManifestStatus::Malformed,
                    normalizedSessionPath,
                    manifestPath,
                    {},
                    QStringLiteral("session manifest is malformed: %1").arg(detail),
                    afterSize,
                    afterModifiedMs};
        }
        return {SessionManifestStatus::Ready,
                normalizedSessionPath,
                manifestPath,
                document.object(),
                {},
                afterSize,
                afterModifiedMs};
    }
    return {SessionManifestStatus::ChangedDuringRead,
            normalizedSessionPath,
            manifestPath,
            {},
            QStringLiteral("session manifest changed while being read"),
            -1,
            0};
}

bool sessionSupportsCurrentBacktestContract(const SessionManifestSnapshot& snapshot,
                                            QString* error) {
    const auto reject = [error](const QString& reason) {
        if (error != nullptr) *error = reason;
        return false;
    };
    if (!snapshot.ready()) return reject(snapshot.error());
    const QJsonObject& manifest = snapshot.object();
    if (manifest.value(QStringLiteral("manifest_schema_version")).toInt() !=
            hftrec::capture::kManifestSchemaVersionCurrent ||
        manifest.value(QStringLiteral("corpus_schema_version")).toInt() !=
            hftrec::capture::kCorpusSchemaVersionCurrent ||
        manifest.value(QStringLiteral("capture_contract_version")).toString() !=
            QString::fromUtf8(hftrec::capture::kCaptureContractVersionCurrent.data(),
                              static_cast<qsizetype>(hftrec::capture::kCaptureContractVersionCurrent.size()))) {
        return reject(QStringLiteral("legacy recorder corpus is view-only; current v3 is required for backtest"));
    }
    const QString status = manifest.value(QStringLiteral("session_status")).toString();
    if (status != QStringLiteral("complete")) {
        return reject(QStringLiteral("recorder session is not cleanly finalized"));
    }
    if (!manifest.value(QStringLiteral("replay")).toObject()
             .value(QStringLiteral("structurally_loadable")).toBool()) {
        return reject(QStringLiteral("recorder session is not structurally loadable"));
    }
    const QJsonObject integrity =
        manifest.value(QStringLiteral("integrity")).toObject();
    if (integrity.value(QStringLiteral("session_health")).toString() !=
            QStringLiteral("clean") ||
        !integrity.value(QStringLiteral("exact_replay_eligible")).toBool()) {
        return reject(QStringLiteral("recorder session is not exact-replay eligible"));
    }
    const QJsonObject arrival =
        manifest.value(QStringLiteral("arrival_clock")).toObject();
    constexpr std::array<const char*, 14u> arrivalFields{
        "boundary", "realtime_clock", "monotonic_clock", "captured_rows",
        "historical_rows", "unavailable_rows", "realtime_regressions",
        "monotonic_non_increasing", "exchange_ahead_of_receive",
        "exchange_timestamp_missing", "first_receive_realtime_ns",
        "last_receive_realtime_ns", "first_receive_monotonic_ns",
        "last_receive_monotonic_ns"};
    for (const char* field : arrivalFields) {
        if (!arrival.contains(QString::fromLatin1(field))) {
            return reject(QStringLiteral("recorder session has incomplete arrival-clock evidence"));
        }
    }
    if (arrival.value(QStringLiteral("realtime_clock")).toString() !=
            QStringLiteral("CLOCK_REALTIME") ||
        arrival.value(QStringLiteral("monotonic_clock")).toString() !=
            QStringLiteral("CLOCK_MONOTONIC")) {
        return reject(QStringLiteral("recorder session uses an unsupported arrival clock"));
    }
    if (arrival.value(QStringLiteral("boundary")).toString() !=
            QStringLiteral("hft-parser.application-frame-ready") ||
        arrival.value(QStringLiteral("captured_rows")).toVariant().toULongLong() == 0u ||
        arrival.value(QStringLiteral("unavailable_rows")).toVariant().toULongLong() != 0u ||
        arrival.value(QStringLiteral("first_receive_realtime_ns")).toVariant().toLongLong() <= 0 ||
        arrival.value(QStringLiteral("last_receive_realtime_ns")).toVariant().toLongLong() <= 0 ||
        arrival.value(QStringLiteral("first_receive_monotonic_ns")).toVariant().toULongLong() == 0u ||
        arrival.value(QStringLiteral("last_receive_monotonic_ns")).toVariant().toULongLong() == 0u) {
        return reject(QStringLiteral("recorder session has no complete captured-arrival clock evidence"));
    }
    const auto addUnsigned = [](const QJsonValue& value,
                                std::uint64_t& total) {
        bool ok = false;
        const std::uint64_t parsed = value.toVariant().toULongLong(&ok);
        if (!ok || parsed > std::numeric_limits<std::uint64_t>::max() - total)
            return false;
        total += parsed;
        return true;
    };
    std::uint64_t declaredRows = 0u;
    const QJsonObject channels =
        manifest.value(QStringLiteral("channels")).toObject();
    constexpr std::array<const char*, 10u> channelNames{
        "trades", "liquidations", "bookticker", "depth", "candles",
        "candles2", "mark_price", "index_price", "funding", "price_limit"};
    for (const char* name : channelNames) {
        const QJsonObject channel =
            channels.value(QString::fromLatin1(name)).toObject();
        if (channel.isEmpty() ||
            !addUnsigned(channel.value(QStringLiteral("declared_event_count")),
                         declaredRows)) {
            return reject(QStringLiteral("recorder session has invalid declared channel counts"));
        }
    }
    std::uint64_t accountedRows = 0u;
    if (!addUnsigned(arrival.value(QStringLiteral("captured_rows")),
                     accountedRows) ||
        !addUnsigned(arrival.value(QStringLiteral("historical_rows")),
                     accountedRows) ||
        !addUnsigned(arrival.value(QStringLiteral("unavailable_rows")),
                     accountedRows) ||
        accountedRows != declaredRows) {
        return reject(QStringLiteral("recorder arrival summary does not match declared rows"));
    }
    if (error != nullptr) error->clear();
    return true;
}

bool sessionSupportsCurrentBacktestContract(const QString& sessionPath, QString* error) {
    return sessionSupportsCurrentBacktestContract(loadSessionManifestSnapshot(sessionPath), error);
}

QString resolveRecordingsRoot() {
    return QDir::cleanPath(QString::fromStdString(recordings::defaultRecordingsRoot().string()));
}

QString sessionSourceSummary(const SessionManifestSnapshot& snapshot,
                             const BacktestLegCounts& backtestCounts) {
    if (!snapshot.ready()) return sessionBacktestSummaryText(0, backtestCounts, 0);
    const QJsonObject& manifest = snapshot.object();
    const QJsonObject bookTicker = manifest.value(QStringLiteral("channels")).toObject().value(QStringLiteral("bookticker")).toObject();
    const QString summary = sessionBacktestSummaryText(
        bookTicker.value(QStringLiteral("declared_event_count")).toInt(),
        backtestCounts,
        manifest.value(QStringLiteral("capture")).toObject().value(QStringLiteral("started_at_ns")).toInteger());
    return appendSessionHealthSummary(
        summary,
        manifest.value(QStringLiteral("integrity")).toObject().value(QStringLiteral("session_health")).toString(),
        manifest.value(QStringLiteral("summary")).toObject().value(QStringLiteral("warning_summary")).toString());
}

QString sessionSourceSummary(const QString& sessionPath, const BacktestLegCounts& backtestCounts) {
    return sessionSourceSummary(loadSessionManifestSnapshot(sessionPath), backtestCounts);
}

QString manifestValue(const SessionManifestSnapshot& manifest, const QString& key) {
    if (!manifest.ready()) return {};
    return manifestObjectValue(manifest.object(), key);
}

QString manifestValue(const QString& sessionPath, const QString& key) {
    return manifestValue(loadSessionManifestSnapshot(sessionPath), key);
}

std::uint64_t manifestChannelDeclaredCount(const SessionManifestSnapshot& manifest,
                                           const QString& channel) {
    if (!manifest.ready()) return 0u;
    const qint64 count = manifest.object()
                             .value(QStringLiteral("channels"))
                             .toObject()
                             .value(channel)
                             .toObject()
                             .value(QStringLiteral("declared_event_count"))
                             .toInteger();
    return count < 0 ? 0u : static_cast<std::uint64_t>(count);
}

std::uint64_t manifestChannelDeclaredCount(const QString& sessionPath,
                                           const QString& channel) {
    return manifestChannelDeclaredCount(loadSessionManifestSnapshot(sessionPath), channel);
}

QString symbolFromSessionId(const QString& sessionId) {
    const QStringList parts = sessionId.split(QLatin1Char('_'), Qt::SkipEmptyParts);
    return parts.isEmpty() ? QString{} : parts.constLast().trimmed();
}

QString venueSectionFor(const QString& exchange, const QString& market) {
    const QString normalizedExchange = exchange.trimmed().toLower();
    const QString normalizedMarket = market.trimmed().toLower();
    if (normalizedExchange == QStringLiteral("binance") && normalizedMarket == QStringLiteral("spot")) return QStringLiteral("binance_spot");
    if (normalizedExchange == QStringLiteral("binance")) return QStringLiteral("binance_futures");
    if (normalizedExchange == QStringLiteral("bybit") && normalizedMarket == QStringLiteral("spot")) return QStringLiteral("bybit_spot");
    if (normalizedExchange == QStringLiteral("bybit")) return QStringLiteral("bybit_futures");
    if (normalizedExchange == QStringLiteral("kucoin") && normalizedMarket == QStringLiteral("spot")) return QStringLiteral("kucoin_spot");
    if (normalizedExchange == QStringLiteral("kucoin")) return QStringLiteral("kucoin_futures");
    if (normalizedExchange == QStringLiteral("gate") && normalizedMarket == QStringLiteral("spot")) return QStringLiteral("gate_spot");
    if (normalizedExchange == QStringLiteral("gate")) return QStringLiteral("gate_futures");
    if (normalizedExchange == QStringLiteral("bitget") && normalizedMarket == QStringLiteral("spot")) return QStringLiteral("bitget_spot");
    if (normalizedExchange == QStringLiteral("bitget") && normalizedMarket == QStringLiteral("inverse")) return QStringLiteral("bitget_inverse");
    if (normalizedExchange == QStringLiteral("bitget") && normalizedMarket == QStringLiteral("swap")) return QStringLiteral("bitget_swap");
    if (normalizedExchange == QStringLiteral("bitget")) return QStringLiteral("bitget_futures");
    if (normalizedExchange == QStringLiteral("aster") && normalizedMarket == QStringLiteral("spot")) return QStringLiteral("aster_spot");
    if (normalizedExchange == QStringLiteral("aster")) return QStringLiteral("aster_futures");
    if (normalizedExchange == QStringLiteral("okx") && normalizedMarket == QStringLiteral("spot")) return QStringLiteral("okx_spot");
    if (normalizedExchange == QStringLiteral("okx")) return QStringLiteral("okx_futures");
    if (normalizedExchange == QStringLiteral("mexc") && normalizedMarket == QStringLiteral("spot")) return QStringLiteral("mexc_spot");
    if (normalizedExchange == QStringLiteral("mexc")) return QStringLiteral("mexc_futures");
    if (normalizedExchange == QStringLiteral("finam") && normalizedMarket == QStringLiteral("spot")) return QStringLiteral("finam_spot");
    if (normalizedExchange == QStringLiteral("finam")) return QStringLiteral("finam_futures");
    return {};
}

bool isVenueSectionKnown(const QString& exchange, const QString& market) {
    return !venueSectionFor(exchange, market).isEmpty();
}

QString venueSectionForSession(const SessionManifestSnapshot& manifest) {
    return venueSectionFor(manifestValue(manifest, QStringLiteral("exchange")),
                           manifestValue(manifest, QStringLiteral("market")));
}

QString venueSectionForSession(const QString& sessionPath) {
    return venueSectionForSession(loadSessionManifestSnapshot(sessionPath));
}

QString symbolForSessionPath(const SessionManifestSnapshot& manifest) {
    QString raw = manifestValue(manifest, QStringLiteral("symbols")).trimmed();
    if (raw.isEmpty()) {
        raw = symbolFromSessionId(QFileInfo(manifest.sessionPath()).fileName()).trimmed();
    }
    if (raw.isEmpty()) return {};
    const QString exchange = manifestValue(manifest, QStringLiteral("exchange")).trimmed();
    const QString market = manifestValue(manifest, QStringLiteral("market")).trimmed();
    const std::string local = recordings::recordingLocalSymbol(exchange.toStdString(),
                                                               market.toStdString(),
                                                               raw.toStdString());
    if (!local.empty()) return QString::fromStdString(local).toUpper();
    return raw.toUpper();
}

QString symbolForSessionPath(const QString& sessionPath) {
    return symbolForSessionPath(loadSessionManifestSnapshot(sessionPath));
}

QString sessionPathFromToken(const QString& recordingsRoot, const QString& token) {
    const QString trimmed = token.trimmed();
    if (trimmed.isEmpty()) return {};
    const QFileInfo info(trimmed);
    if (info.isAbsolute()) return QDir::cleanPath(info.absoluteFilePath());
    const QString groupPrefix = QStringLiteral("group:");
    if (trimmed.startsWith(groupPrefix)) return {};
    return QDir(recordingsRoot).absoluteFilePath(trimmed);
}

QVariantMap sessionRowById(const QVariantList& rows, const QString& id) {
    for (const QVariant& value : rows) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("id")).toString() == id) return row;
    }
    return {};
}

bool sessionRowSelectable(const QVariantMap& row) {
    return row.value(QStringLiteral("selectable"), true).toBool();
}

QString firstSelectableSessionId(const QVariantList& rows) {
    for (const QVariant& value : rows) {
        const QVariantMap row = value.toMap();
        if (sessionRowSelectable(row)) return row.value(QStringLiteral("id")).toString();
    }
    return {};
}

QStringList sessionPathsFromRow(const QVariantMap& row) {
    QStringList paths;
    const QVariantList rowPaths = row.value(QStringLiteral("sessionPaths")).toList();
    for (const QVariant& value : rowPaths) {
        const QString path = value.toString().trimmed();
        if (!path.isEmpty() && !paths.contains(path)) paths.push_back(path);
    }
    const QString path = row.value(QStringLiteral("path")).toString().trimmed();
    if (paths.empty() && !path.isEmpty()) paths.push_back(path);
    return paths;
}

}  // namespace hftrec::gui
