#include "../BacktestViewModel.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QTextStream>
#include <QVariantMap>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "hft_backtest/Backtest.hpp"
#include "hft_backtest/BacktestSweep.hpp"
#include "../../Config/BacktestExecutionConfigHelpers.hpp"
#include "../../Results/BacktestResultHelpers.hpp"
#include "../../Sessions/BacktestSessionHelpers.hpp"
#include "../../Config/BacktestStrategyConfigHelpers.hpp"
#include "../../Batch/BacktestSweepHelpers.hpp"
#include "../../../Models/RecordingCatalog.hpp"


namespace hftrec::gui {

QString BacktestViewModel::runId_() const {
    return runIdForSymbol_(selectedSymbol());
}

QString BacktestViewModel::runIdForSymbol_(const QString& symbol) const {
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    return QStringLiteral("%1-%2-%3-%4")
        .arg(cleanRunSlugPart(selectedStrategy_), cleanRunSlugPart(symbol), cleanRunSlugPart(configMode_), stamp);
}

QString BacktestViewModel::displayName_() const {
    return displayNameForSymbol_(selectedSymbol());
}

QString BacktestViewModel::displayNameForSymbol_(const QString& symbol) const {
    return QStringLiteral("%1 %2 %3")
        .arg(selectedStrategy_.trimmed(), symbol.trimmed(), configMode_.trimmed())
        .simplified();
}

QString BacktestViewModel::configSummary_(const QHash<QString, QString>& overrides) const {
    QStringList parts;
    QSet<QString> shown;
    for (const QString& key : paramOrder_) {
        const hft_backtest::StrategyParamMetadata* param = paramMetadataFor(selectedStrategy_, key);
        if (param != nullptr && param->exclusiveGroup != 0u && activeParamByGroup_.value(static_cast<int>(param->exclusiveGroup)) != key) continue;
        const QString value = overrides.value(key, paramValues_.value(key)).trimmed();
        if (!value.isEmpty()) {
            parts.push_back(QStringLiteral("%1=%2").arg(key, value));
            shown.insert(key);
        }
        if (parts.size() >= 3) break;
    }
    if (parts.size() < 3 && !overrides.empty()) {
        QStringList keys = overrides.keys();
        std::sort(keys.begin(), keys.end());
        for (const QString& rawKey : keys) {
            const QString key = rawKey.trimmed().toLower();
            if (key.isEmpty() || shown.contains(key)) continue;
            const QString value = overrides.value(rawKey).trimmed();
            if (value.isEmpty()) continue;
            parts.push_back(QStringLiteral("%1=%2").arg(key, value));
            if (parts.size() >= 3) break;
        }
    }
    QString summary = configMode_.trimmed();
    if (!parts.empty()) summary += QStringLiteral(": ") + parts.join(QStringLiteral(", "));
    if (!selectedIndicatorProfile_.isEmpty()) summary += QStringLiteral(" | indicator=%1").arg(selectedIndicatorProfile_);
    if (riskEnabled_) summary += QStringLiteral(" | risk=on");
    if (!rateLimitsEnabled_) summary += QStringLiteral(" | rate_limits=off");
    return summary;
}

BacktestViewModel::RunConfigWriteResult BacktestViewModel::writeRunConfig_(const QString& runId, const QHash<QString, QString>& overrides, bool fixedOnly) {
    return writeRunConfigForSessionPaths_(runId, orderedSessionPathsForRun_(), overrides, fixedOnly);
}

BacktestViewModel::RunConfigWriteResult BacktestViewModel::writeRunConfigForSessionPaths_(const QString& runId,
                                                                                          const QStringList& sessionPaths,
                                                                                          const QHash<QString, QString>& overrides,
                                                                                          bool fixedOnly,
                                                                                          bool useSelectedSymbolOverride) {
    std::vector<BacktestPreparedSession> sessions;
    sessions.reserve(static_cast<std::size_t>(sessionPaths.size()));
    for (const QString& path : sessionPaths) {
        const SessionManifestSnapshot manifest = loadSessionManifestSnapshot(path);
        if (!manifest.ready()) {
            return {{}, QStringLiteral("%1: %2").arg(manifest.error(), path)};
        }
        BacktestPreparedSession session;
        session.path = path;
        session.exchange = manifestValue(manifest, QStringLiteral("exchange")).trimmed().toLower();
        session.market = manifestValue(manifest, QStringLiteral("market")).trimmed().toLower();
        session.venue = venueSectionFor(session.exchange, session.market);
        session.symbol = symbolForSessionPath(manifest);
        session.configSymbol = session.symbol;
        if (useSelectedSymbolOverride && path == selectedSessionPath()) {
            const QString manualSymbol = symbolOverride_.trimmed().toUpper();
            const QString manifestSymbol =
                manifestValue(manifest, QStringLiteral("symbols")).trimmed().toUpper();
            session.configSymbol = !manualSymbol.isEmpty()
                ? manualSymbol
                : (!manifestSymbol.isEmpty()
                       ? manifestSymbol
                       : symbolFromSessionId(selectedSessionId_).toUpper());
        }
        sessions.push_back(std::move(session));
    }
    return writeRunConfigForPreparedSessions_(runId, sessions, overrides, fixedOnly);
}

BacktestViewModel::RunConfigWriteResult BacktestViewModel::writeRunConfigForPreparedSessions_(
    const QString& runId,
    const std::vector<BacktestPreparedSession>& sessions,
    const QHash<QString, QString>& overrides,
    bool fixedOnly) {
    const QString templatePath = configTemplatePathForStrategy(selectedStrategy_);
    const QString base = templatePath.isEmpty() ? QString{} : readTextFile(templatePath);
    if (!templatePath.isEmpty() && base.isEmpty()) {
        return {{}, QStringLiteral("failed to read config template: %1").arg(templatePath)};
    }
    if (sessions.empty()) return {{}, QStringLiteral("no session paths selected")};
    const QString session = sessions.front().path;
    QStringList sessionPaths;
    sessionPaths.reserve(static_cast<qsizetype>(sessions.size()));
    for (const BacktestPreparedSession& prepared : sessions) {
        sessionPaths.push_back(prepared.path);
    }
    QStringList legRefs;
    QStringList venueOrder;
    QHash<QString, QStringList> venueSymbols;
    QHash<QString, QString> venueApiSlots;
    QHash<QString, QVariantMap> venueExecutionByVenue;
    for (const BacktestPreparedSession& prepared : sessions) {
        const QString& path = prepared.path;
        const QString& venue = prepared.venue;
        const QString symbol = prepared.configSymbol.isEmpty()
            ? prepared.symbol
            : prepared.configSymbol;
        if (venue.isEmpty() || symbol.isEmpty()) {
            return {{}, QStringLiteral("missing venue or symbol for session: %1").arg(path)};
        }
        legRefs.push_back(QStringLiteral("%1:%2").arg(venue, symbol));
        if (!venueSymbols.contains(venue)) venueOrder.push_back(venue);
        QStringList symbols = venueSymbols.value(venue);
        if (!symbols.contains(symbol)) symbols.push_back(symbol);
        venueSymbols.insert(venue, symbols);
        QString apiSlot = iniValue(base, QStringLiteral("venue.%1").arg(venue), QStringLiteral("api_slot"));
        if (apiSlot.isEmpty()) apiSlot = QStringLiteral("1");
        if (!venueApiSlots.contains(venue)) venueApiSlots.insert(venue, apiSlot);
        if (!venueExecutionByVenue.contains(venue)) {
            const QString venueKey = prepared.exchange + QLatin1Char('|') +
                normalizedFeeMarket(prepared.market);
            const QString makerFeeOverride = venueExecutionOverrideValue_(venueKey, QStringLiteral("maker_fee_bps"));
            const QString takerFeeOverride = venueExecutionOverrideValue_(venueKey, QStringLiteral("taker_fee_bps"));
            QVariantMap row;
            row.insert(QStringLiteral("initialBalanceUsdt"), venueExecutionValue_(venueKey, QStringLiteral("initial_balance_usdt"), initialBalanceUsdt_));
            if (!makerFeeOverride.isEmpty()) row.insert(QStringLiteral("makerFeeBps"), makerFeeOverride);
            if (!takerFeeOverride.isEmpty()) row.insert(QStringLiteral("takerFeeBps"), takerFeeOverride);
            row.insert(QStringLiteral("rateLimitOrdersLimit"), venueExecutionValue_(venueKey, QStringLiteral("rate_limit_orders_limit"), QString{}));
            row.insert(QStringLiteral("rateLimitOrdersIntervalMs"), venueExecutionValue_(venueKey, QStringLiteral("rate_limit_orders_interval_ms"), QString{}));
            row.insert(QStringLiteral("rateLimitCancelOrdersLimit"), venueExecutionValue_(venueKey, QStringLiteral("rate_limit_cancel_orders_limit"), QString{}));
            row.insert(QStringLiteral("rateLimitCancelOrdersIntervalMs"), venueExecutionValue_(venueKey, QStringLiteral("rate_limit_cancel_orders_interval_ms"), QString{}));
            row.insert(QStringLiteral("rateLimitReduceOnlyOrdersLimit"), venueExecutionValue_(venueKey, QStringLiteral("rate_limit_reduce_only_orders_limit"), QString{}));
            row.insert(QStringLiteral("rateLimitReduceOnlyOrdersIntervalMs"), venueExecutionValue_(venueKey, QStringLiteral("rate_limit_reduce_only_orders_interval_ms"), QString{}));
            row.insert(QStringLiteral("rateLimitLimitOrderCost"), venueExecutionValue_(venueKey, QStringLiteral("rate_limit_limit_order_cost"), QStringLiteral("1")));
            row.insert(QStringLiteral("rateLimitMarketOrderCost"), venueExecutionValue_(venueKey, QStringLiteral("rate_limit_market_order_cost"), QStringLiteral("1")));
            row.insert(QStringLiteral("rateLimitCancelOrderCost"), venueExecutionValue_(venueKey, QStringLiteral("rate_limit_cancel_order_cost"), QStringLiteral("1")));
            row.insert(QStringLiteral("rateLimitReduceOnlyLimitOrderCost"), venueExecutionValue_(venueKey, QStringLiteral("rate_limit_reduce_only_limit_order_cost"), QStringLiteral("1")));
            row.insert(QStringLiteral("rateLimitReduceOnlyMarketOrderCost"), venueExecutionValue_(venueKey, QStringLiteral("rate_limit_reduce_only_market_order_cost"), QStringLiteral("1")));
            venueExecutionByVenue.insert(venue, row);
        }
    }
    QDir outDir(QDir(session).absoluteFilePath(QStringLiteral("backtests")));
    const QString runDir = outDir.absoluteFilePath(runId);
    const QString cleanRunDir = QDir::cleanPath(runDir);
    std::error_code mkdirError;
    std::filesystem::create_directories(std::filesystem::path{cleanRunDir.toStdString()}, mkdirError);
    if (mkdirError) {
        return {{}, QStringLiteral("cannot create directory %1: %2").arg(cleanRunDir, QString::fromStdString(mkdirError.message()))};
    }
    std::error_code statError;
    if (!std::filesystem::is_directory(std::filesystem::path{cleanRunDir.toStdString()}, statError)) {
        const QString detail = statError ? QString::fromStdString(statError.message()) : QStringLiteral("path exists but is not a directory");
        return {{}, QStringLiteral("cannot create directory %1: %2").arg(cleanRunDir, detail)};
    }
    const QString path = QDir(runDir).absoluteFilePath(QStringLiteral("config.ini"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return {{}, QStringLiteral("cannot open %1: %2").arg(QDir::cleanPath(path), file.errorString())};
    }
    QTextStream out(&file);
    out << "# recorder backtest metadata\n";
    out << "# display_name=" << displayNameForSymbol_(sessions.front().configSymbol) << "\n";
    out << "# config_summary=" << configSummary_(overrides) << "\n\n";
    const QString filteredBase = filteredBaseConfig(base);
    out << filteredBase;
    if (!filteredBase.endsWith(QLatin1Char('\n'))) out << "\n";
    out << "\n# recorder backtest overrides\n";
    out << "[backtest]\n";
    writeBacktestRateLimitConfig(out, rateLimitsEnabled_, strictRateLimitsEnabled_);
    out << "\n";
    out << "[strategy]\n";
    out << "type=" << selectedStrategy_ << "\n";
    QSet<QString> writtenStrategyKeys;
    writtenStrategyKeys.insert(QStringLiteral("type"));
    for (const QString& key : paramOrder_) {
        const hft_backtest::StrategyParamMetadata* param = paramMetadataFor(selectedStrategy_, key);
        if (param != nullptr && param->exclusiveGroup != 0u && activeParamByGroup_.value(static_cast<int>(param->exclusiveGroup)) != key) continue;
        if (fixedOnly && paramModes_.value(key, QStringLiteral("fixed")) != QStringLiteral("fixed")) continue;
        const QString value = overrides.value(key, paramValues_.value(key)).trimmed();
        if (!value.isEmpty()) {
            out << key << "=" << value << "\n";
            writtenStrategyKeys.insert(key);
        }
    }
    if (!overrides.empty()) {
        QStringList keys = overrides.keys();
        std::sort(keys.begin(), keys.end());
        for (const QString& rawKey : keys) {
            const QString key = rawKey.trimmed().toLower();
            if (key.isEmpty() || key == QStringLiteral("type") || key == QStringLiteral("enabled") || writtenStrategyKeys.contains(key)) continue;
            const QString value = overrides.value(rawKey).trimmed();
            if (value.isEmpty()) continue;
            out << key << "=" << value << "\n";
            writtenStrategyKeys.insert(key);
        }
    }
    const bool hasRiskRateLimitGuard = rateLimitsEnabled_ && !riskRateLimitGuardMinRemaining_.trimmed().isEmpty();
    if (riskEnabled_ || hasRiskRateLimitGuard) {
        const bool hasRiskMaxPosition = !riskMaxPositionUsdt_.trimmed().isEmpty();
        out << "\n[risk]\n";
        out << "enabled=" << (riskEnabled_ ? "true" : "false") << "\n";
        if (!riskMinEquityPct_.trimmed().isEmpty()) out << "min_equity_pct=" << riskMinEquityPct_.trimmed() << "\n";
        if (!riskMinLegEquityPct_.trimmed().isEmpty()) out << "min_leg_equity_pct=" << riskMinLegEquityPct_.trimmed() << "\n";
        if (!riskMinLegEquityUsdt_.trimmed().isEmpty()) out << "min_leg_equity_usdt=" << riskMinLegEquityUsdt_.trimmed() << "\n";
        if (hasRiskMaxPosition) out << "max_position_usdt=" << riskMaxPositionUsdt_.trimmed() << "\n";
        if (hasRiskRateLimitGuard) out << "rate_limit_guard_min_remaining=" << riskRateLimitGuardMinRemaining_.trimmed() << "\n";
    }
    for (const QString& venue : venueOrder) {
        out << "\n[venue." << venue << "]\n";
        out << "api_slot=" << venueApiSlots.value(venue, QStringLiteral("1")) << "\n";
        out << "symbols=" << venueSymbols.value(venue).join(QLatin1Char(',')) << "\n";
        const QVariantMap execution = venueExecutionByVenue.value(venue);
        out << "initial_balance_usdt=" << execution.value(QStringLiteral("initialBalanceUsdt"), initialBalanceUsdt_).toString() << "\n";
        const QString makerFee = execution.value(QStringLiteral("makerFeeBps")).toString().trimmed();
        const QString takerFee = execution.value(QStringLiteral("takerFeeBps")).toString().trimmed();
        if (!makerFee.isEmpty()) out << "maker_fee_bps=" << makerFee << "\n";
        if (!takerFee.isEmpty()) out << "taker_fee_bps=" << takerFee << "\n";
        writeRuntimeRateLimitConfig(out, execution);
    }
    if (legRefs.size() > 1) {
        out << "\n[portfolio.recorder]\n";
        out << "legs=" << legRefs.join(QLatin1Char(',')) << "\n";
        out << "primary_leg_index=" << selectedPrimaryLegIndexForPaths_(sessionPaths) << "\n";
        out << "trade_mode=" << selectedTradeMode_ << "\n";
    }
    out.flush();
    if (out.status() != QTextStream::Ok) {
        const QString detail = file.errorString().trimmed().isEmpty() ? QStringLiteral("QTextStream write failed") : file.errorString();
        return {{}, QStringLiteral("cannot write %1: %2").arg(QDir::cleanPath(path), detail)};
    }
    file.close();
    if (file.error() != QFileDevice::NoError) {
        return {{}, QStringLiteral("cannot write %1: %2").arg(QDir::cleanPath(path), file.errorString())};
    }
    return {path, {}};
}

quint64 BacktestViewModel::latencyValue_(const QString& value, quint64 fallback) const noexcept {
    bool ok = false;
    const quint64 parsed = value.trimmed().toULongLong(&ok);
    return ok ? parsed : fallback;
}

qint64 BacktestViewModel::decimalE8Value_(const QString& value, qint64 fallback) const noexcept {
    const QString text = value.trimmed();
    if (text.isEmpty()) return fallback;
    qsizetype pos = 0;
    bool negative = false;
    if (text.at(pos) == QLatin1Char('-')) {
        negative = true;
        ++pos;
    }
    qint64 whole = 0;
    bool anyWhole = false;
    while (pos < text.size() && text.at(pos).isDigit()) {
        anyWhole = true;
        const int digit = text.at(pos).unicode() - QLatin1Char('0').unicode();
        if (whole > (std::numeric_limits<qint64>::max() / 10)) return fallback;
        whole = whole * 10 + digit;
        ++pos;
    }
    qint64 frac = 0;
    qint64 scale = 10000000;
    if (pos < text.size() && text.at(pos) == QLatin1Char('.')) {
        ++pos;
        while (pos < text.size() && text.at(pos).isDigit()) {
            if (scale > 0) {
                const int digit = text.at(pos).unicode() - QLatin1Char('0').unicode();
                frac += static_cast<qint64>(digit) * scale;
                scale /= 10;
            }
            ++pos;
        }
    }
    if (!anyWhole || pos != text.size()) return fallback;
    if (whole > (std::numeric_limits<qint64>::max() - frac) / 100000000ll) return fallback;
    qint64 out = whole * 100000000ll + frac;
    if (negative) out = -out;
    return out < 0 ? fallback : out;
}

}  // namespace hftrec::gui
