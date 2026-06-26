#include "gui/backtests/BacktestViewModel.hpp"

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
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "hft_backtest/backtest.hpp"
#include "hft_backtest/backtest_sweep.hpp"
#include "gui/backtests/BacktestExecutionConfigHelpers.hpp"
#include "gui/backtests/BacktestResultHelpers.hpp"
#include "gui/backtests/BacktestSessionHelpers.hpp"
#include "gui/backtests/BacktestStrategyConfigHelpers.hpp"
#include "gui/backtests/BacktestSweepHelpers.hpp"

namespace hftrec::gui {

void BacktestViewModel::reloadSessions() {
    sessions_ = loadSessions_();
    if (manualSessionPath_.trimmed().isEmpty()) {
        const QVariantMap selectedRow = sessionRowById(sessions_, selectedSessionId_);
        if (selectedSessionId_.trimmed().isEmpty() || selectedRow.isEmpty() || !sessionRowSelectable(selectedRow)) {
            selectedSessionId_ = firstSelectableSessionId(sessions_);
        }
    }
    const bool strategyChanged = ensureSelectedStrategySupportsSessionCount_();
    emit sessionsChanged();
    emit selectedSessionChanged();
    emit multiSessionChanged();
    if (!strategyChanged) emit selectedStrategyChanged();
    emit canRunChanged();
}

void BacktestViewModel::setSelectedSessionId(const QString& sessionId) {
    const QString next = sessionId.trimmed();
    if (selectedSessionId_ == next && manualSessionPath_.isEmpty()) return;
    selectedSessionId_ = next;
    manualSessionPath_.clear();
    symbolOverride_.clear();
    selectedRunId_.clear();
    ++detailsLoadGeneration_;
    selectedDetailsErrorText_.clear();
    setDetailsLoading_(false);
    const bool strategyChanged = ensureSelectedStrategySupportsSessionCount_();
    emit selectedSessionChanged();
    emit multiSessionChanged();
    if (!strategyChanged) emit selectedStrategyChanged();
    emit symbolChanged();
    emit canRunChanged();
    emit detailsLoadingChanged();
    scheduleRefresh_();
}

void BacktestViewModel::setSessionPath(const QString& sessionPath) {
    const QString normalized = normalizedPath_(sessionPath);
    if (selectedSessionPath() == normalized) return;
    manualSessionPath_ = normalized;
    selectedSessionId_ = sessionIdFromPath_(normalized);
    symbolOverride_.clear();
    selectedRunId_.clear();
    ++detailsLoadGeneration_;
    selectedDetailsErrorText_.clear();
    setDetailsLoading_(false);
    const bool strategyChanged = ensureSelectedStrategySupportsSessionCount_();
    emit selectedSessionChanged();
    emit multiSessionChanged();
    if (!strategyChanged) emit selectedStrategyChanged();
    emit symbolChanged();
    emit canRunChanged();
    emit detailsLoadingChanged();
    refresh();
}

void BacktestViewModel::setExtraSessionIds(const QString& sessionIds) {
    const QString next = sessionIds.trimmed();
    if (extraSessionIds_ == next) return;
    extraSessionIds_ = next;
    savePersistentConfig_();
    const bool strategyChanged = ensureSelectedStrategySupportsSessionCount_();
    emit multiSessionChanged();
    if (!strategyChanged) emit selectedStrategyChanged();
    emit canRunChanged();
    refreshSessionGateStatus_();
    refresh();
}

void BacktestViewModel::setSelectedSymbol(const QString& symbol) {
    const QString next = symbol.trimmed().toUpper();
    if (symbolOverride_ == next) return;
    symbolOverride_ = next;
    emit symbolChanged();
    emit canRunChanged();
}

void BacktestViewModel::setSelectedStrategy(const QString& strategy) {
    const QString next = strategy.trimmed();
    if (selectedStrategy_ == next || next.isEmpty()) return;
    if (!isDiscoveredStrategy(next)) return;
    savePersistentConfig_();
    if (RunRecord* oldRecord = mutableRecordForRunId_(selectedRunId_)) clearRecordDetails_(*oldRecord);
    ++previewLoadGeneration_;
    ++detailsLoadGeneration_;
    pendingDetailsRunId_.clear();
    selectedDetailsErrorText_.clear();
    selectedRunId_.clear();
    setPreviewLoading_(false);
    setDetailsLoading_(false);
    selectedStrategy_ = next;
    configMode_ = QStringLiteral("fixed");
    loadStrategyDefaults_();
    loadSavedParameterValues_();
    selectedIndicatorProfile_ = settings_.value(QStringLiteral("backtests/indicator_profile/%1").arg(selectedStrategy_), defaultIndicatorProfileForStrategy(selectedStrategy_)).toString().trimmed();
    if (!indicatorProfileAllowedForStrategy(selectedStrategy_, selectedIndicatorProfile_)) selectedIndicatorProfile_ = defaultIndicatorProfileForStrategy(selectedStrategy_);
    savePersistentConfig_();
    emit selectedStrategyChanged();
    emit indicatorProfileChanged();
    emit configChanged();
    emit accountingChanged();
    emit strategyParametersChanged();
    emit canRunChanged();
    emit selectionChanged();
    emit selectedResultMetricChanged();
    emit detailsLoadingChanged();
    refreshSessionGateStatus_();
    refresh();
}

void BacktestViewModel::setSelectedIndicatorProfile(const QString& profile) {
    QString next = profile.trimmed();
    if (!indicatorProfileAllowedForStrategy(selectedStrategy_, next)) next = defaultIndicatorProfileForStrategy(selectedStrategy_);
    if (selectedIndicatorProfile_ == next) return;
    selectedIndicatorProfile_ = next;
    savePersistentConfig_();
    emit indicatorProfileChanged();
    emit canRunChanged();
}

void BacktestViewModel::setConfigMode(const QString& mode) {
    const QString next = normalizeConfigMode(mode);
    if (configMode_ == next) return;
    configMode_ = next;
    savePersistentConfig_();
    emit configChanged();
    emit strategyParametersChanged();
}

void BacktestViewModel::setStrategyParameter(const QString& key, const QString& value) {
    const QString normalizedKey = key.trimmed().toLower();
    if (normalizedKey.isEmpty()) return;
    const hft_backtest::StrategyMetadata* metadata = metadataForStrategy(selectedStrategy_);
    if (metadata == nullptr || (!metadataHasParam(*metadata, normalizedKey) && !paramOrder_.contains(normalizedKey))) return;
    const QString normalizedValue = value.trimmed();
    if (paramValues_.value(normalizedKey) == normalizedValue) return;
    if (!paramOrder_.contains(normalizedKey)) paramOrder_.push_back(normalizedKey);
    paramValues_.insert(normalizedKey, normalizedValue);
    savePersistentConfig_();
    emit strategyParametersChanged();
}

void BacktestViewModel::setStrategyParameterGroup(int group, const QString& key) {
    if (group <= 0) return;
    const hft_backtest::StrategyMetadata* metadata = metadataForStrategy(selectedStrategy_);
    if (metadata == nullptr) return;
    const QString normalizedKey = key.trimmed().toLower();
    if (!paramExistsInExclusiveGroup(*metadata, static_cast<std::uint8_t>(group), normalizedKey)) return;
    if (activeParamByGroup_.value(group) == normalizedKey) return;
    activeParamByGroup_.insert(group, normalizedKey);
    savePersistentConfig_();
    emit strategyParametersChanged();
}
void BacktestViewModel::setProfileName(const QString& profileName) {
    const QString next = cleanProfileName(profileName);
    if (profileName_ == next) return;
    profileName_ = next;
    emit profileChanged();
}

void BacktestViewModel::setOrderLatencyUs(const QString& value) {
    setMarketOrderLatencyUs(value);
}

void BacktestViewModel::setPingLatencyUs(const QString& value) {
    const QString next = value.trimmed();
    if (pingLatencyUs_ == next) return;
    pingLatencyUs_ = next;
    savePersistentConfig_();
    emit latencyChanged();
}

void BacktestViewModel::setLatencySeed(const QString& value) {
    const QString next = value.trimmed();
    if (latencySeed_ == next) return;
    latencySeed_ = next;
    savePersistentConfig_();
    emit latencyChanged();
}

void BacktestViewModel::setMarketDataLatencyUs(const QString& value) {
    const QString next = value.trimmed();
    if (marketDataLatencyUs_ == next) return;
    marketDataLatencyUs_ = next;
    savePersistentConfig_();
    emit latencyChanged();
}

void BacktestViewModel::setMarketDataJitterUs(const QString& value) {
    const QString next = value.trimmed();
    if (marketDataJitterUs_ == next) return;
    marketDataJitterUs_ = next;
    savePersistentConfig_();
    emit latencyChanged();
}

void BacktestViewModel::setMarketOrderLatencyUs(const QString& value) {
    const QString next = value.trimmed();
    if (marketOrderLatencyUs_ == next) return;
    marketOrderLatencyUs_ = next;
    savePersistentConfig_();
    emit latencyChanged();
}

void BacktestViewModel::setMarketOrderJitterUs(const QString& value) {
    const QString next = value.trimmed();
    if (marketOrderJitterUs_ == next) return;
    marketOrderJitterUs_ = next;
    savePersistentConfig_();
    emit latencyChanged();
}

void BacktestViewModel::setLimitOrderLatencyUs(const QString& value) {
    const QString next = value.trimmed();
    if (limitOrderLatencyUs_ == next) return;
    limitOrderLatencyUs_ = next;
    savePersistentConfig_();
    emit latencyChanged();
}

void BacktestViewModel::setLimitOrderJitterUs(const QString& value) {
    const QString next = value.trimmed();
    if (limitOrderJitterUs_ == next) return;
    limitOrderJitterUs_ = next;
    savePersistentConfig_();
    emit latencyChanged();
}

void BacktestViewModel::setCancelOrderLatencyUs(const QString& value) {
    const QString next = value.trimmed();
    if (cancelOrderLatencyUs_ == next) return;
    cancelOrderLatencyUs_ = next;
    savePersistentConfig_();
    emit latencyChanged();
}

void BacktestViewModel::setCancelOrderJitterUs(const QString& value) {
    const QString next = value.trimmed();
    if (cancelOrderJitterUs_ == next) return;
    cancelOrderJitterUs_ = next;
    savePersistentConfig_();
    emit latencyChanged();
}

void BacktestViewModel::setUserDataLatencyUs(const QString& value) {
    const QString next = value.trimmed();
    if (userDataLatencyUs_ == next) return;
    userDataLatencyUs_ = next;
    savePersistentConfig_();
    emit latencyChanged();
}

void BacktestViewModel::setUserDataJitterUs(const QString& value) {
    const QString next = value.trimmed();
    if (userDataJitterUs_ == next) return;
    userDataJitterUs_ = next;
    savePersistentConfig_();
    emit latencyChanged();
}

void BacktestViewModel::setVenueExecutionValue(int legIndex, const QString& field, const QString& value) {
    const QString normalizedField = field.trimmed().toLower();
    if (!isVenueExecutionField(normalizedField)) return;
    const QStringList paths = selectedSessionPaths_();
    if (legIndex < 0 || legIndex >= paths.size()) return;
    const QString venueKey = venueExecutionKey(paths.at(legIndex));
    if (venueKey.isEmpty()) return;
    const QString next = value.trimmed();
    const QString mapKey = venueExecutionMapKey(venueKey, normalizedField);
    if (venueExecutionValues_.value(mapKey) == next) return;
    venueExecutionValues_.insert(mapKey, next);
    settings_.setValue(QStringLiteral("backtests/venue_execution/%1/%2")
                           .arg(venueExecutionSettingKey(venueKey), normalizedField),
                       next);
    settings_.sync();
    emit multiSessionChanged();
    if (normalizedField == QStringLiteral("initial_balance_usdt") ||
        normalizedField == QStringLiteral("maker_fee_bps") ||
        normalizedField == QStringLiteral("taker_fee_bps")) emit accountingChanged();
    else emit latencyChanged();
}

void BacktestViewModel::setInitialBalanceUsdt(const QString& value) {
    const QString next = value.trimmed();
    if (initialBalanceUsdt_ == next) return;
    initialBalanceUsdt_ = next;
    savePersistentConfig_();
    emit accountingChanged();
}

void BacktestViewModel::setRiskEnabled(bool enabled) {
    if (riskEnabled_ == enabled) return;
    riskEnabled_ = enabled;
    savePersistentConfig_();
    emit accountingChanged();
}

void BacktestViewModel::setRiskMinEquityPct(const QString& value) {
    const QString next = value.trimmed();
    if (riskMinEquityPct_ == next) return;
    riskMinEquityPct_ = next;
    savePersistentConfig_();
    emit accountingChanged();
}

void BacktestViewModel::setRiskMinLegEquityPct(const QString& value) {
    const QString next = value.trimmed();
    if (riskMinLegEquityPct_ == next) return;
    riskMinLegEquityPct_ = next;
    savePersistentConfig_();
    emit accountingChanged();
}

void BacktestViewModel::setRiskMinLegEquityUsdt(const QString& value) {
    const QString next = value.trimmed();
    if (riskMinLegEquityUsdt_ == next) return;
    riskMinLegEquityUsdt_ = next;
    savePersistentConfig_();
    emit accountingChanged();
}

void BacktestViewModel::setRiskMaxPositionUsdt(const QString& value) {
    const QString next = value.trimmed();
    if (riskMaxPositionUsdt_ == next) return;
    riskMaxPositionUsdt_ = next;
    savePersistentConfig_();
    emit accountingChanged();
}

void BacktestViewModel::setRiskRateLimitGuardMinRemaining(const QString& value) {
    const QString next = value.trimmed();
    if (riskRateLimitGuardMinRemaining_ == next) return;
    riskRateLimitGuardMinRemaining_ = next;
    savePersistentConfig_();
    emit accountingChanged();
}

void BacktestViewModel::setRateLimitsEnabled(bool enabled) {
    if (rateLimitsEnabled_ == enabled) return;
    rateLimitsEnabled_ = enabled;
    savePersistentConfig_();
    emit rateLimitsChanged();
    emit multiSessionChanged();
}

void BacktestViewModel::setMakerFeeBps(const QString& value) {
    const QString next = value.trimmed();
    if (makerFeeBps_ == next) return;
    makerFeeBps_ = next;
    savePersistentConfig_();
    emit accountingChanged();
}

void BacktestViewModel::setTakerFeeBps(const QString& value) {
    const QString next = value.trimmed();
    if (takerFeeBps_ == next) return;
    takerFeeBps_ = next;
    savePersistentConfig_();
    emit accountingChanged();
}

void BacktestViewModel::setCancelLatencyUs(const QString& value) {
    setCancelOrderLatencyUs(value);
}

void BacktestViewModel::setSweepBudget(const QString& value) {
    const QString next = value.trimmed();
    if (sweepBudget_ == next) return;
    sweepBudget_ = next;
    savePersistentConfig_();
    emit sweepConfigChanged();
}

void BacktestViewModel::setSweepSeed(const QString& value) {
    const QString next = value.trimmed();
    if (sweepSeed_ == next) return;
    sweepSeed_ = next;
    savePersistentConfig_();
    emit sweepConfigChanged();
}

void BacktestViewModel::setSelectedSweepCurveLimit(const QString& limit) {
    QString next = limit.trimmed().toLower();
    if (next != QStringLiteral("16") && next != QStringLiteral("32") && next != QStringLiteral("64") && next != QStringLiteral("all")) next = QStringLiteral("32");
    if (selectedSweepCurveLimit_ == next) return;
    selectedSweepCurveLimit_ = next;
    emit selectionChanged();
}

void BacktestViewModel::setSelectedSweepView(const QString& view) {
    const QString next = view == QStringLiteral("distribution") ? QStringLiteral("distribution") : QStringLiteral("curves");
    if (selectedSweepView_ == next) return;
    selectedSweepView_ = next;
    emit selectionChanged();
}

void BacktestViewModel::setSelectedSweepMetric(const QString& metric) {
    QString next = metric.trimmed();
    bool valid = next == QStringLiteral("total_pnl_e8");
    if (!valid) {
        for (const QVariant& choice : sweepMetricChoices()) {
            if (choice.toMap().value(QStringLiteral("id")).toString() == next) {
                valid = true;
                break;
            }
        }
    }
    if (!valid) next = QStringLiteral("total_pnl_e8");
    if (selectedSweepMetric_ == next) return;
    selectedSweepMetric_ = next;
    emit selectionChanged();
}

void BacktestViewModel::setSelectedSweepDistributionParam(const QString& key) {
    const QString next = key.trimmed();
    if (selectedSweepDistributionParam_ == next) return;
    selectedSweepDistributionParam_ = next;
    emit selectionChanged();
}

void BacktestViewModel::setStrategyParameterMode(const QString& key, const QString& mode) {
    const QString normalizedKey = key.trimmed().toLower();
    if (normalizedKey.isEmpty() || !paramOrder_.contains(normalizedKey)) return;
    const QString next = normalizedParamMode(mode);
    if (paramModes_.value(normalizedKey, QStringLiteral("fixed")) == next) return;
    paramModes_.insert(normalizedKey, next);
    savePersistentConfig_();
    emit strategyParametersChanged();
}

void BacktestViewModel::setStrategyParameterRange(const QString& key, const QString& minValue, const QString& maxValue, const QString& stepValue) {
    const QString normalizedKey = key.trimmed().toLower();
    if (normalizedKey.isEmpty() || !paramOrder_.contains(normalizedKey)) return;
    const QString nextMin = minValue.trimmed();
    const QString nextMax = maxValue.trimmed();
    const QString nextStep = stepValue.trimmed();
    if (paramMinValues_.value(normalizedKey) == nextMin && paramMaxValues_.value(normalizedKey) == nextMax && paramStepValues_.value(normalizedKey) == nextStep) return;
    paramMinValues_.insert(normalizedKey, nextMin);
    paramMaxValues_.insert(normalizedKey, nextMax);
    paramStepValues_.insert(normalizedKey, nextStep);
    savePersistentConfig_();
    emit strategyParametersChanged();
}
void BacktestViewModel::saveProfile() {
    const QString path = profilePath_();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        setStatusText_(QStringLiteral("Failed to save profile"));
        return;
    }
    QTextStream out(&file);
    out << "[backtest]\n";
    out << "latency_seed=" << latencySeed_ << "\n";
    out << "market_data_latency_us=" << marketDataLatencyUs_ << "\n";
    out << "market_data_jitter_us=" << marketDataJitterUs_ << "\n";
    out << "market_order_latency_us=" << marketOrderLatencyUs_ << "\n";
    out << "market_order_jitter_us=" << marketOrderJitterUs_ << "\n";
    out << "limit_order_latency_us=" << limitOrderLatencyUs_ << "\n";
    out << "limit_order_jitter_us=" << limitOrderJitterUs_ << "\n";
    out << "cancel_order_latency_us=" << cancelOrderLatencyUs_ << "\n";
    out << "cancel_order_jitter_us=" << cancelOrderJitterUs_ << "\n";
    out << "user_data_latency_us=" << userDataLatencyUs_ << "\n";
    out << "user_data_jitter_us=" << userDataJitterUs_ << "\n";
    out << "order_latency_us=" << marketOrderLatencyUs_ << "\n";
    out << "cancel_latency_us=" << cancelOrderLatencyUs_ << "\n";
    out << "initial_balance_usdt=" << initialBalanceUsdt_ << "\n";
    out << "risk_enabled=" << (riskEnabled_ ? "true" : "false") << "\n";
    out << "risk_min_equity_pct=" << riskMinEquityPct_ << "\n";
    out << "risk_min_leg_equity_pct=" << riskMinLegEquityPct_ << "\n";
    out << "risk_min_leg_equity_usdt=" << riskMinLegEquityUsdt_ << "\n";
    out << "risk_max_position_usdt=" << riskMaxPositionUsdt_ << "\n";
    out << "risk_rate_limit_guard_min_remaining=" << riskRateLimitGuardMinRemaining_ << "\n";
    writeBacktestRateLimitConfig(out, rateLimitsEnabled_);
    out << "sweep_budget=" << sweepBudget_ << "\n";
    out << "sweep_seed=" << sweepSeed_ << "\n";
    out << "config_mode=" << configMode_ << "\n\n";
    QSet<QString> savedVenueKeys;
    for (const QString& sessionPath : selectedSessionPaths_()) {
        const QString venueKey = venueExecutionKey(sessionPath);
        if (venueKey.isEmpty() || savedVenueKeys.contains(venueKey)) continue;
        savedVenueKeys.insert(venueKey);
        out << "[venue_execution." << venueExecutionSettingKey(venueKey) << "]\n";
        const auto writeOptional = [&out](const QString& key, const QString& value) {
            const QString trimmed = value.trimmed();
            if (!trimmed.isEmpty()) out << key << "=" << trimmed << "\n";
        };
        out << "initial_balance_usdt=" << venueExecutionValue_(venueKey, QStringLiteral("initial_balance_usdt"), initialBalanceUsdt_) << "\n";
        writeOptional(QStringLiteral("maker_fee_bps"), venueExecutionValue_(venueKey, QStringLiteral("maker_fee_bps"), QString{}));
        writeOptional(QStringLiteral("taker_fee_bps"), venueExecutionValue_(venueKey, QStringLiteral("taker_fee_bps"), QString{}));
        out << "market_data_latency_us=" << venueExecutionValue_(venueKey, QStringLiteral("market_data_latency_us"), marketDataLatencyUs_) << "\n";
        out << "market_data_jitter_us=" << venueExecutionValue_(venueKey, QStringLiteral("market_data_jitter_us"), marketDataJitterUs_) << "\n";
        out << "market_order_latency_us=" << venueExecutionValue_(venueKey, QStringLiteral("market_order_latency_us"), marketOrderLatencyUs_) << "\n";
        out << "market_order_jitter_us=" << venueExecutionValue_(venueKey, QStringLiteral("market_order_jitter_us"), marketOrderJitterUs_) << "\n";
        out << "limit_order_latency_us=" << venueExecutionValue_(venueKey, QStringLiteral("limit_order_latency_us"), limitOrderLatencyUs_) << "\n";
        out << "limit_order_jitter_us=" << venueExecutionValue_(venueKey, QStringLiteral("limit_order_jitter_us"), limitOrderJitterUs_) << "\n";
        out << "cancel_order_latency_us=" << venueExecutionValue_(venueKey, QStringLiteral("cancel_order_latency_us"), cancelOrderLatencyUs_) << "\n";
        out << "cancel_order_jitter_us=" << venueExecutionValue_(venueKey, QStringLiteral("cancel_order_jitter_us"), cancelOrderJitterUs_) << "\n";
        out << "user_data_latency_us=" << venueExecutionValue_(venueKey, QStringLiteral("user_data_latency_us"), userDataLatencyUs_) << "\n";
        out << "user_data_jitter_us=" << venueExecutionValue_(venueKey, QStringLiteral("user_data_jitter_us"), userDataJitterUs_) << "\n";
        writeOptional(QStringLiteral("rate_limit_orders_limit"), venueExecutionValue_(venueKey, QStringLiteral("rate_limit_orders_limit"), QString{}));
        writeOptional(QStringLiteral("rate_limit_orders_interval_ms"), venueExecutionValue_(venueKey, QStringLiteral("rate_limit_orders_interval_ms"), QString{}));
        writeOptional(QStringLiteral("rate_limit_cancel_orders_limit"), venueExecutionValue_(venueKey, QStringLiteral("rate_limit_cancel_orders_limit"), QString{}));
        writeOptional(QStringLiteral("rate_limit_cancel_orders_interval_ms"), venueExecutionValue_(venueKey, QStringLiteral("rate_limit_cancel_orders_interval_ms"), QString{}));
        writeOptional(QStringLiteral("rate_limit_reduce_only_orders_limit"), venueExecutionValue_(venueKey, QStringLiteral("rate_limit_reduce_only_orders_limit"), QString{}));
        writeOptional(QStringLiteral("rate_limit_reduce_only_orders_interval_ms"), venueExecutionValue_(venueKey, QStringLiteral("rate_limit_reduce_only_orders_interval_ms"), QString{}));
        writeOptional(QStringLiteral("rate_limit_limit_order_cost"), venueExecutionValue_(venueKey, QStringLiteral("rate_limit_limit_order_cost"), QString{}));
        writeOptional(QStringLiteral("rate_limit_market_order_cost"), venueExecutionValue_(venueKey, QStringLiteral("rate_limit_market_order_cost"), QString{}));
        writeOptional(QStringLiteral("rate_limit_cancel_order_cost"), venueExecutionValue_(venueKey, QStringLiteral("rate_limit_cancel_order_cost"), QString{}));
        writeOptional(QStringLiteral("rate_limit_reduce_only_limit_order_cost"), venueExecutionValue_(venueKey, QStringLiteral("rate_limit_reduce_only_limit_order_cost"), QString{}));
        writeOptional(QStringLiteral("rate_limit_reduce_only_market_order_cost"), venueExecutionValue_(venueKey, QStringLiteral("rate_limit_reduce_only_market_order_cost"), QString{}));
        out << "\n";
    }
    out << "[strategy]\n";
    for (auto it = activeParamByGroup_.constBegin(); it != activeParamByGroup_.constEnd(); ++it) out << groupSettingKey(it.key()) << "=" << it.value() << "\n";
    for (const QString& key : paramOrder_) {
        const hft_backtest::StrategyParamMetadata* param = paramMetadataFor(selectedStrategy_, key);
        if (param != nullptr && param->exclusiveGroup != 0u && activeParamByGroup_.value(static_cast<int>(param->exclusiveGroup)) != key) continue;
        out << key << "=" << paramValues_.value(key) << "\n";
    }
    out << "\n[sweep]\n";
    for (const QString& key : paramOrder_) {
        out << key << ".mode=" << paramModes_.value(key, QStringLiteral("fixed")) << "\n";
        out << key << ".min=" << paramMinValues_.value(key) << "\n";
        out << key << ".max=" << paramMaxValues_.value(key) << "\n";
        out << key << ".step=" << paramStepValues_.value(key) << "\n";
    }
    setStatusText_(QStringLiteral("Profile saved"));
}

void BacktestViewModel::loadProfile() {
    const QString text = readTextFile(profilePath_());
    if (text.isEmpty()) return;
    const QString orderLatency = iniValue(text, QStringLiteral("backtest"), QStringLiteral("order_latency_us"));
    const QString latencySeed = iniValue(text, QStringLiteral("backtest"), QStringLiteral("latency_seed"));
    const QString marketDataLatency = iniValue(text, QStringLiteral("backtest"), QStringLiteral("market_data_latency_us"));
    const QString marketDataJitter = iniValue(text, QStringLiteral("backtest"), QStringLiteral("market_data_jitter_us"));
    const QString marketOrderLatency = iniValue(text, QStringLiteral("backtest"), QStringLiteral("market_order_latency_us"));
    const QString marketOrderJitter = iniValue(text, QStringLiteral("backtest"), QStringLiteral("market_order_jitter_us"));
    const QString limitOrderLatency = iniValue(text, QStringLiteral("backtest"), QStringLiteral("limit_order_latency_us"));
    const QString limitOrderJitter = iniValue(text, QStringLiteral("backtest"), QStringLiteral("limit_order_jitter_us"));
    const QString cancelLatency = iniValue(text, QStringLiteral("backtest"), QStringLiteral("cancel_latency_us"));
    const QString cancelOrderLatency = iniValue(text, QStringLiteral("backtest"), QStringLiteral("cancel_order_latency_us"));
    const QString cancelOrderJitter = iniValue(text, QStringLiteral("backtest"), QStringLiteral("cancel_order_jitter_us"));
    const QString userDataLatency = iniValue(text, QStringLiteral("backtest"), QStringLiteral("user_data_latency_us"));
    const QString userDataJitter = iniValue(text, QStringLiteral("backtest"), QStringLiteral("user_data_jitter_us"));
    const QString initialBalance = iniValue(text, QStringLiteral("backtest"), QStringLiteral("initial_balance_usdt"));
    const QString riskEnabled = iniValue(text, QStringLiteral("backtest"), QStringLiteral("risk_enabled"));
    const QString riskMinEquity = iniValue(text, QStringLiteral("backtest"), QStringLiteral("risk_min_equity_pct"));
    const QString riskMinLegEquityPct = iniValue(text, QStringLiteral("backtest"), QStringLiteral("risk_min_leg_equity_pct"));
    const QString riskMinLegEquityUsdt = iniValue(text, QStringLiteral("backtest"), QStringLiteral("risk_min_leg_equity_usdt"));
    const QString riskMaxPositionUsdt = iniValue(text, QStringLiteral("backtest"), QStringLiteral("risk_max_position_usdt"));
    const QString riskRateLimitGuardMinRemaining =
        iniValue(text, QStringLiteral("backtest"), QStringLiteral("risk_rate_limit_guard_min_remaining"));
    const QString rateLimitsEnabled = iniValue(text, QStringLiteral("backtest"), QStringLiteral("rate_limits_enabled"));
    const QString sweepBudget = iniValue(text, QStringLiteral("backtest"), QStringLiteral("sweep_budget"));
    const QString sweepSeed = iniValue(text, QStringLiteral("backtest"), QStringLiteral("sweep_seed"));
    const QString mode = iniValue(text, QStringLiteral("backtest"), QStringLiteral("config_mode"));
    if (!orderLatency.isEmpty()) pingLatencyUs_ = orderLatency;
    if (!latencySeed.isEmpty()) latencySeed_ = latencySeed;
    if (!marketDataLatency.isEmpty()) marketDataLatencyUs_ = marketDataLatency;
    if (!marketDataJitter.isEmpty()) marketDataJitterUs_ = marketDataJitter;
    if (!marketOrderLatency.isEmpty()) marketOrderLatencyUs_ = marketOrderLatency;
    else if (!orderLatency.isEmpty()) marketOrderLatencyUs_ = orderLatency;
    if (!marketOrderJitter.isEmpty()) marketOrderJitterUs_ = marketOrderJitter;
    if (!limitOrderLatency.isEmpty()) limitOrderLatencyUs_ = limitOrderLatency;
    else if (!orderLatency.isEmpty()) limitOrderLatencyUs_ = orderLatency;
    if (!limitOrderJitter.isEmpty()) limitOrderJitterUs_ = limitOrderJitter;
    if (!cancelOrderLatency.isEmpty()) cancelOrderLatencyUs_ = cancelOrderLatency;
    else if (!cancelLatency.isEmpty()) cancelOrderLatencyUs_ = cancelLatency;
    else cancelOrderLatencyUs_ = limitOrderLatencyUs_;
    if (!cancelOrderJitter.isEmpty()) cancelOrderJitterUs_ = cancelOrderJitter;
    else cancelOrderJitterUs_ = limitOrderJitterUs_;
    if (!userDataLatency.isEmpty()) userDataLatencyUs_ = userDataLatency;
    if (!userDataJitter.isEmpty()) userDataJitterUs_ = userDataJitter;
    if (!initialBalance.isEmpty()) initialBalanceUsdt_ = initialBalance;
    if (!riskEnabled.isEmpty()) riskEnabled_ = boolIniValue(riskEnabled, riskEnabled_);
    riskMinEquityPct_ = riskMinEquity;
    riskMinLegEquityPct_ = riskMinLegEquityPct;
    riskMinLegEquityUsdt_ = riskMinLegEquityUsdt;
    riskMaxPositionUsdt_ = riskMaxPositionUsdt;
    riskRateLimitGuardMinRemaining_ = riskRateLimitGuardMinRemaining;
    if (!rateLimitsEnabled.isEmpty()) rateLimitsEnabled_ = boolIniValue(rateLimitsEnabled, rateLimitsEnabled_);
    if (!sweepBudget.isEmpty()) sweepBudget_ = sweepBudget;
    if (!sweepSeed.isEmpty()) sweepSeed_ = sweepSeed;
    if (!mode.isEmpty()) configMode_ = normalizeConfigMode(mode);
    for (const QString& sessionPath : selectedSessionPaths_()) {
        const QString venueKey = venueExecutionKey(sessionPath);
        if (venueKey.isEmpty()) continue;
        const QString section = QStringLiteral("venue_execution.%1").arg(venueExecutionSettingKey(venueKey));
        for (const IniKeyValue& row : iniSectionValues(text, section)) {
            const QString field = row.key.trimmed().toLower();
            const QString value = row.value.trimmed();
            if (!isVenueExecutionField(field) || value.isEmpty()) continue;
            venueExecutionValues_.insert(venueExecutionMapKey(venueKey, field), value);
        }
    }
    for (const IniKeyValue& row : iniSectionValues(text, QStringLiteral("strategy"))) {
        const QString key = row.key;
        const QString value = row.value;
        if (key.startsWith(QStringLiteral("__group_"))) {
            const int group = key.mid(QStringLiteral("__group_").size()).toInt();
            const hft_backtest::StrategyMetadata* metadata = metadataForStrategy(selectedStrategy_);
            if (metadata != nullptr && paramExistsInExclusiveGroup(*metadata, static_cast<std::uint8_t>(group), value)) activeParamByGroup_.insert(group, value.trimmed().toLower());
            continue;
        }
        if (!paramOrder_.contains(key)) continue;
        if (!value.isEmpty()) paramValues_.insert(key, value);
    }
    for (const IniKeyValue& row : iniSectionValues(text, QStringLiteral("sweep"))) {
        const QString key = row.key;
        const QString value = row.value.trimmed();
        const qsizetype dot = key.lastIndexOf(QLatin1Char('.'));
        if (dot <= 0 || value.isEmpty()) continue;
        const QString paramKey = key.left(dot);
        const QString field = key.mid(dot + 1);
        if (!paramOrder_.contains(paramKey)) continue;
        if (field == QStringLiteral("mode")) paramModes_.insert(paramKey, normalizedParamMode(value));
        else if (field == QStringLiteral("min")) paramMinValues_.insert(paramKey, value);
        else if (field == QStringLiteral("max")) paramMaxValues_.insert(paramKey, value);
        else if (field == QStringLiteral("step")) paramStepValues_.insert(paramKey, value);
    }
    emit latencyChanged();
    emit accountingChanged();
    emit rateLimitsChanged();
    emit multiSessionChanged();
    emit sweepConfigChanged();
    emit configChanged();
    emit strategyParametersChanged();
}

QString BacktestViewModel::runId_() const {
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    return QStringLiteral("%1-%2-%3-%4")
        .arg(cleanRunSlugPart(selectedStrategy_), cleanRunSlugPart(selectedSymbol()), cleanRunSlugPart(configMode_), stamp);
}

QString BacktestViewModel::displayName_() const {
    return QStringLiteral("%1 %2 %3")
        .arg(selectedStrategy_.trimmed(), selectedSymbol().trimmed(), configMode_.trimmed())
        .simplified();
}

QString BacktestViewModel::configSummary_(const QHash<QString, QString>& overrides) const {
    QStringList parts;
    for (const QString& key : paramOrder_) {
        const hft_backtest::StrategyParamMetadata* param = paramMetadataFor(selectedStrategy_, key);
        if (param != nullptr && param->exclusiveGroup != 0u && activeParamByGroup_.value(static_cast<int>(param->exclusiveGroup)) != key) continue;
        const QString value = overrides.value(key, paramValues_.value(key)).trimmed();
        if (!value.isEmpty()) parts.push_back(QStringLiteral("%1=%2").arg(key, value));
        if (parts.size() >= 3) break;
    }
    QString summary = configMode_.trimmed();
    if (!parts.empty()) summary += QStringLiteral(": ") + parts.join(QStringLiteral(", "));
    if (!selectedIndicatorProfile_.isEmpty()) summary += QStringLiteral(" | indicator=%1").arg(selectedIndicatorProfile_);
    if (riskEnabled_) summary += QStringLiteral(" | risk=on");
    if (!rateLimitsEnabled_) summary += QStringLiteral(" | rate_limits=off");
    return summary;
}

void BacktestViewModel::loadStrategyDefaults_() {
    paramValues_.clear();
    paramModes_.clear();
    paramMinValues_.clear();
    paramMaxValues_.clear();
    paramStepValues_.clear();
    activeParamByGroup_.clear();
    paramOrder_.clear();
    const hft_backtest::StrategyMetadata* metadata = metadataForStrategy(selectedStrategy_);
    if (metadata == nullptr) return;
    const QString templatePath = configTemplatePathForStrategy(selectedStrategy_);
    const QString templateText = templatePath.isEmpty() ? QString{} : readTextFile(templatePath);
    for (std::size_t i = 0; i < metadata->paramCount && i < hft_backtest::kStrategyMetadataMaxParams; ++i) {
        const hft_backtest::StrategyParamMetadata& param = metadata->params[i];
        if (param.key == nullptr || param.key[0] == '\0') continue;
        const QString key = qString(param.key).trimmed().toLower();
        if (key.isEmpty() || paramOrder_.contains(key)) continue;
        const QString value = qString(param.defaultValue);
        paramOrder_.push_back(key);
        paramValues_.insert(key, value);
        paramModes_.insert(key, defaultParamMode(templateText, key));
        paramMinValues_.insert(key, defaultRangeMin(templateText, key, value));
        paramMaxValues_.insert(key, defaultRangeMax(templateText, key, value));
        paramStepValues_.insert(key, defaultRangeStep(templateText, key));
        if (param.exclusiveGroup != 0u && (param.defaultActive || !activeParamByGroup_.contains(static_cast<int>(param.exclusiveGroup)))) {
            activeParamByGroup_.insert(static_cast<int>(param.exclusiveGroup), key);
        }
    }
    for (const IniKeyValue& row : iniSectionValues(templateText, QStringLiteral("strategy"))) {
        const QString key = row.key.trimmed().toLower();
        if (!isTemplateStrategyParamKey(key) || paramOrder_.contains(key)) continue;
        const QString value = row.value.trimmed();
        paramOrder_.push_back(key);
        paramValues_.insert(key, value);
        paramModes_.insert(key, defaultParamMode(templateText, key));
        paramMinValues_.insert(key, defaultRangeMin(templateText, key, value));
        paramMaxValues_.insert(key, defaultRangeMax(templateText, key, value));
        paramStepValues_.insert(key, defaultRangeStep(templateText, key));
    }
    riskEnabled_ = boolIniValue(iniValue(templateText, QStringLiteral("risk"), QStringLiteral("enabled")), false);
    riskMinEquityPct_ = iniValue(templateText, QStringLiteral("risk"), QStringLiteral("min_equity_pct"));
    riskMinLegEquityPct_ = iniValue(templateText, QStringLiteral("risk"), QStringLiteral("min_leg_equity_pct"));
    riskMinLegEquityUsdt_ = iniValue(templateText, QStringLiteral("risk"), QStringLiteral("min_leg_equity_usdt"));
    riskMaxPositionUsdt_ = iniValue(templateText, QStringLiteral("risk"), QStringLiteral("max_position_usdt"));
    riskRateLimitGuardMinRemaining_ = iniValue(templateText, QStringLiteral("risk"), QStringLiteral("rate_limit_guard_min_remaining"));
}

void BacktestViewModel::loadPersistentConfig_() {
    const QString defaultStrategy = selectedStrategy_;
    const QString strategy = settings_.value(QStringLiteral("backtests/selected_strategy"), selectedStrategy_).toString().trimmed();
    if (!strategy.isEmpty()) selectedStrategy_ = strategy;
    if (!isDiscoveredStrategy(selectedStrategy_)) {
        const QString fallback = isDiscoveredStrategy(defaultStrategy) ? defaultStrategy : firstDiscoveredStrategy();
        if (!fallback.isEmpty()) selectedStrategy_ = fallback;
    }
    extraSessionIds_ = settings_.value(QStringLiteral("backtests/extra_session_ids"), extraSessionIds_).toString().trimmed();
    configMode_ = normalizeConfigMode(settings_.value(QStringLiteral("backtests/config_mode/%1").arg(selectedStrategy_), configMode_).toString());
    configMode_ = QStringLiteral("fixed");
    selectedIndicatorProfile_ = settings_.value(QStringLiteral("backtests/indicator_profile/%1").arg(selectedStrategy_), defaultIndicatorProfileForStrategy(selectedStrategy_)).toString().trimmed();
    if (!indicatorProfileAllowedForStrategy(selectedStrategy_, selectedIndicatorProfile_)) selectedIndicatorProfile_ = defaultIndicatorProfileForStrategy(selectedStrategy_);
    const bool hasLegacyPingLatency = settings_.contains(QStringLiteral("backtests/ping_latency_us"));
    pingLatencyUs_ = settings_.value(QStringLiteral("backtests/ping_latency_us"), pingLatencyUs_).toString().trimmed();
    if (pingLatencyUs_.isEmpty()) pingLatencyUs_ = QStringLiteral("1000");
    latencySeed_ = settings_.value(QStringLiteral("backtests/latency_seed"), latencySeed_).toString().trimmed();
    if (latencySeed_.isEmpty()) latencySeed_ = QStringLiteral("0");
    marketDataLatencyUs_ = settings_.value(QStringLiteral("backtests/market_data_latency_us"), marketDataLatencyUs_).toString().trimmed();
    if (marketDataLatencyUs_.isEmpty()) marketDataLatencyUs_ = QStringLiteral("250");
    marketDataJitterUs_ = settings_.value(QStringLiteral("backtests/market_data_jitter_us"), marketDataJitterUs_).toString().trimmed();
    if (marketDataJitterUs_.isEmpty()) marketDataJitterUs_ = QStringLiteral("100");
    if (settings_.contains(QStringLiteral("backtests/market_order_latency_us"))) marketOrderLatencyUs_ = settings_.value(QStringLiteral("backtests/market_order_latency_us"), marketOrderLatencyUs_).toString().trimmed();
    else if (hasLegacyPingLatency) marketOrderLatencyUs_ = pingLatencyUs_;
    if (marketOrderLatencyUs_.isEmpty()) marketOrderLatencyUs_ = QStringLiteral("2500");
    marketOrderJitterUs_ = settings_.value(QStringLiteral("backtests/market_order_jitter_us"), marketOrderJitterUs_).toString().trimmed();
    if (marketOrderJitterUs_.isEmpty()) marketOrderJitterUs_ = QStringLiteral("1000");
    if (settings_.contains(QStringLiteral("backtests/limit_order_latency_us"))) limitOrderLatencyUs_ = settings_.value(QStringLiteral("backtests/limit_order_latency_us"), limitOrderLatencyUs_).toString().trimmed();
    else if (hasLegacyPingLatency) limitOrderLatencyUs_ = pingLatencyUs_;
    if (limitOrderLatencyUs_.isEmpty()) limitOrderLatencyUs_ = QStringLiteral("1800");
    limitOrderJitterUs_ = settings_.value(QStringLiteral("backtests/limit_order_jitter_us"), limitOrderJitterUs_).toString().trimmed();
    if (limitOrderJitterUs_.isEmpty()) limitOrderJitterUs_ = QStringLiteral("700");
    if (settings_.contains(QStringLiteral("backtests/cancel_order_latency_us"))) cancelOrderLatencyUs_ = settings_.value(QStringLiteral("backtests/cancel_order_latency_us"), cancelOrderLatencyUs_).toString().trimmed();
    else cancelOrderLatencyUs_ = limitOrderLatencyUs_;
    if (cancelOrderLatencyUs_.isEmpty()) cancelOrderLatencyUs_ = limitOrderLatencyUs_;
    if (settings_.contains(QStringLiteral("backtests/cancel_order_jitter_us"))) cancelOrderJitterUs_ = settings_.value(QStringLiteral("backtests/cancel_order_jitter_us"), cancelOrderJitterUs_).toString().trimmed();
    else cancelOrderJitterUs_ = limitOrderJitterUs_;
    if (cancelOrderJitterUs_.isEmpty()) cancelOrderJitterUs_ = limitOrderJitterUs_;
    userDataLatencyUs_ = settings_.value(QStringLiteral("backtests/user_data_latency_us"), userDataLatencyUs_).toString().trimmed();
    if (userDataLatencyUs_.isEmpty()) userDataLatencyUs_ = QStringLiteral("0");
    userDataJitterUs_ = settings_.value(QStringLiteral("backtests/user_data_jitter_us"), userDataJitterUs_).toString().trimmed();
    if (userDataJitterUs_.isEmpty()) userDataJitterUs_ = QStringLiteral("0");
    initialBalanceUsdt_ = settings_.value(QStringLiteral("backtests/initial_balance_usdt"), initialBalanceUsdt_).toString().trimmed();
    if (initialBalanceUsdt_.isEmpty()) initialBalanceUsdt_ = QStringLiteral("1000");
    loadStrategyDefaults_();
    loadSavedParameterValues_();
    if (settings_.contains(QStringLiteral("backtests/risk_enabled"))) {
        riskEnabled_ = settings_.value(QStringLiteral("backtests/risk_enabled"), riskEnabled_).toBool();
    }
    if (settings_.contains(QStringLiteral("backtests/risk_min_equity_pct"))) {
        riskMinEquityPct_ = settings_.value(QStringLiteral("backtests/risk_min_equity_pct"), riskMinEquityPct_).toString().trimmed();
    }
    if (settings_.contains(QStringLiteral("backtests/risk_min_leg_equity_pct"))) {
        riskMinLegEquityPct_ = settings_.value(QStringLiteral("backtests/risk_min_leg_equity_pct"), riskMinLegEquityPct_).toString().trimmed();
    }
    if (settings_.contains(QStringLiteral("backtests/risk_min_leg_equity_usdt"))) {
        riskMinLegEquityUsdt_ = settings_.value(QStringLiteral("backtests/risk_min_leg_equity_usdt"), riskMinLegEquityUsdt_).toString().trimmed();
    }
    if (settings_.contains(QStringLiteral("backtests/risk_max_position_usdt"))) {
        riskMaxPositionUsdt_ = settings_.value(QStringLiteral("backtests/risk_max_position_usdt"), riskMaxPositionUsdt_).toString().trimmed();
    }
    if (settings_.contains(QStringLiteral("backtests/risk_rate_limit_guard_min_remaining"))) {
        riskRateLimitGuardMinRemaining_ =
            settings_.value(QStringLiteral("backtests/risk_rate_limit_guard_min_remaining"), riskRateLimitGuardMinRemaining_).toString().trimmed();
    }
    if (settings_.contains(QStringLiteral("backtests/rate_limits_enabled"))) {
        rateLimitsEnabled_ = settings_.value(QStringLiteral("backtests/rate_limits_enabled"), rateLimitsEnabled_).toBool();
    }
    makerFeeBps_ = QStringLiteral("0");
    takerFeeBps_ = QStringLiteral("0");
    sweepBudget_ = settings_.value(QStringLiteral("backtests/sweep_budget"), sweepBudget_).toString().trimmed();
    if (sweepBudget_.isEmpty()) sweepBudget_ = QStringLiteral("64");
    sweepSeed_ = settings_.value(QStringLiteral("backtests/sweep_seed"), sweepSeed_).toString().trimmed();
    if (sweepSeed_.isEmpty()) sweepSeed_ = QStringLiteral("0");
}

void BacktestViewModel::loadSavedParameterValues_() {
    settings_.beginGroup(QStringLiteral("backtests/params/%1").arg(selectedStrategy_));
    for (const QString& key : paramOrder_) {
        const QString value = settings_.value(key, paramValues_.value(key)).toString().trimmed();
        if (!value.isEmpty()) paramValues_.insert(key, value);
        paramModes_.insert(key, normalizedParamMode(settings_.value(QStringLiteral("%1.mode").arg(key), paramModes_.value(key, QStringLiteral("fixed"))).toString()));
        const QString minValue = settings_.value(QStringLiteral("%1.min").arg(key), paramMinValues_.value(key)).toString().trimmed();
        const QString maxValue = settings_.value(QStringLiteral("%1.max").arg(key), paramMaxValues_.value(key)).toString().trimmed();
        const QString stepValue = settings_.value(QStringLiteral("%1.step").arg(key), paramStepValues_.value(key)).toString().trimmed();
        if (!minValue.isEmpty()) paramMinValues_.insert(key, minValue);
        if (!maxValue.isEmpty()) paramMaxValues_.insert(key, maxValue);
        if (!stepValue.isEmpty()) paramStepValues_.insert(key, stepValue);
    }
    const hft_backtest::StrategyMetadata* metadata = metadataForStrategy(selectedStrategy_);
    if (metadata != nullptr) {
        for (std::size_t i = 0; i < metadata->paramGroupCount && i < hft_backtest::kStrategyMetadataMaxParamGroups; ++i) {
            const int group = static_cast<int>(metadata->paramGroups[i].id);
            const QString key = settings_.value(groupSettingKey(group), activeParamByGroup_.value(group)).toString().trimmed().toLower();
            if (paramExistsInExclusiveGroup(*metadata, static_cast<std::uint8_t>(group), key)) activeParamByGroup_.insert(group, key);
        }
    }
    settings_.endGroup();
}

void BacktestViewModel::savePersistentConfig_() {
    settings_.setValue(QStringLiteral("backtests/selected_strategy"), selectedStrategy_);
    settings_.setValue(QStringLiteral("backtests/extra_session_ids"), extraSessionIds_);
    settings_.setValue(QStringLiteral("backtests/config_mode/%1").arg(selectedStrategy_), configMode_);
    settings_.setValue(QStringLiteral("backtests/indicator_profile/%1").arg(selectedStrategy_), selectedIndicatorProfile_);
    settings_.setValue(QStringLiteral("backtests/ping_latency_us"), pingLatencyUs_);
    settings_.setValue(QStringLiteral("backtests/latency_seed"), latencySeed_);
    settings_.setValue(QStringLiteral("backtests/market_data_latency_us"), marketDataLatencyUs_);
    settings_.setValue(QStringLiteral("backtests/market_data_jitter_us"), marketDataJitterUs_);
    settings_.setValue(QStringLiteral("backtests/market_order_latency_us"), marketOrderLatencyUs_);
    settings_.setValue(QStringLiteral("backtests/market_order_jitter_us"), marketOrderJitterUs_);
    settings_.setValue(QStringLiteral("backtests/limit_order_latency_us"), limitOrderLatencyUs_);
    settings_.setValue(QStringLiteral("backtests/limit_order_jitter_us"), limitOrderJitterUs_);
    settings_.setValue(QStringLiteral("backtests/cancel_order_latency_us"), cancelOrderLatencyUs_);
    settings_.setValue(QStringLiteral("backtests/cancel_order_jitter_us"), cancelOrderJitterUs_);
    settings_.setValue(QStringLiteral("backtests/user_data_latency_us"), userDataLatencyUs_);
    settings_.setValue(QStringLiteral("backtests/user_data_jitter_us"), userDataJitterUs_);
    settings_.setValue(QStringLiteral("backtests/initial_balance_usdt"), initialBalanceUsdt_);
    settings_.setValue(QStringLiteral("backtests/risk_enabled"), riskEnabled_);
    settings_.setValue(QStringLiteral("backtests/risk_min_equity_pct"), riskMinEquityPct_);
    settings_.setValue(QStringLiteral("backtests/risk_min_leg_equity_pct"), riskMinLegEquityPct_);
    settings_.setValue(QStringLiteral("backtests/risk_min_leg_equity_usdt"), riskMinLegEquityUsdt_);
    settings_.setValue(QStringLiteral("backtests/risk_max_position_usdt"), riskMaxPositionUsdt_);
    settings_.setValue(QStringLiteral("backtests/risk_rate_limit_guard_min_remaining"), riskRateLimitGuardMinRemaining_);
    settings_.setValue(QStringLiteral("backtests/rate_limits_enabled"), rateLimitsEnabled_);
    settings_.setValue(QStringLiteral("backtests/sweep_budget"), sweepBudget_);
    settings_.setValue(QStringLiteral("backtests/sweep_seed"), sweepSeed_);
    settings_.beginGroup(QStringLiteral("backtests/params/%1").arg(selectedStrategy_));
    for (const QString& key : paramOrder_) {
        settings_.setValue(key, paramValues_.value(key));
        settings_.setValue(QStringLiteral("%1.mode").arg(key), paramModes_.value(key, QStringLiteral("fixed")));
        settings_.setValue(QStringLiteral("%1.min").arg(key), paramMinValues_.value(key));
        settings_.setValue(QStringLiteral("%1.max").arg(key), paramMaxValues_.value(key));
        settings_.setValue(QStringLiteral("%1.step").arg(key), paramStepValues_.value(key));
    }
    for (auto it = activeParamByGroup_.constBegin(); it != activeParamByGroup_.constEnd(); ++it) settings_.setValue(groupSettingKey(it.key()), it.value());
    settings_.endGroup();
    settings_.sync();
}

QString BacktestViewModel::profilePath_() const {
    return QDir(recordingsRoot()).absoluteFilePath(QStringLiteral("backtest_profiles/%1/%2.ini").arg(selectedStrategy_, cleanProfileName(profileName_)));
}

bool BacktestViewModel::strategySupportsSelectedSessionCount_() const {
    const hft_backtest::StrategyMetadata* metadata = metadataForStrategy(selectedStrategy_);
    if (metadata == nullptr) return false;
    return strategyMetadataSupportsSessionCount(*metadata, selectedSessionCount());
}

bool BacktestViewModel::ensureSelectedStrategySupportsSessionCount_() {
    if (strategySupportsSelectedSessionCount_()) return false;
    const QString fallback = firstDiscoveredStrategyForSessionCount(selectedSessionCount());
    if (fallback.isEmpty() || fallback == selectedStrategy_) return false;
    selectedStrategy_ = fallback;
    configMode_ = QStringLiteral("fixed");
    loadStrategyDefaults_();
    loadSavedParameterValues_();
    selectedIndicatorProfile_ = settings_.value(QStringLiteral("backtests/indicator_profile/%1").arg(selectedStrategy_),
                                                defaultIndicatorProfileForStrategy(selectedStrategy_))
                                    .toString()
                                    .trimmed();
    if (!indicatorProfileAllowedForStrategy(selectedStrategy_, selectedIndicatorProfile_)) selectedIndicatorProfile_ = defaultIndicatorProfileForStrategy(selectedStrategy_);
    savePersistentConfig_();
    emit selectedStrategyChanged();
    emit indicatorProfileChanged();
    emit configChanged();
    emit accountingChanged();
    emit strategyParametersChanged();
    return true;
}

QString BacktestViewModel::writeRunConfig_(const QString& runId, const QHash<QString, QString>& overrides, bool fixedOnly) {
    const QString templatePath = configTemplatePathForStrategy(selectedStrategy_);
    const QString base = templatePath.isEmpty() ? QString{} : readTextFile(templatePath);
    if (!templatePath.isEmpty() && base.isEmpty()) return {};
    const QString session = selectedSessionPath();
    const QStringList sessionPaths = orderedSessionPathsForRun_();
    if (sessionPaths.empty()) return {};
    QStringList legRefs;
    QStringList venueOrder;
    QHash<QString, QStringList> venueSymbols;
    QHash<QString, QString> venueApiSlots;
    QHash<QString, QVariantMap> venueExecutionByVenue;
    for (int i = 0; i < sessionPaths.size(); ++i) {
        const QString path = sessionPaths.at(i);
        const QString venue = venueSectionForSession(path);
        const QString symbol = path == selectedSessionPath() ? selectedSymbol() : symbolForSessionPath(path);
        if (venue.isEmpty() || symbol.isEmpty()) return {};
        legRefs.push_back(QStringLiteral("%1:%2").arg(venue, symbol));
        if (!venueSymbols.contains(venue)) venueOrder.push_back(venue);
        QStringList symbols = venueSymbols.value(venue);
        if (!symbols.contains(symbol)) symbols.push_back(symbol);
        venueSymbols.insert(venue, symbols);
        QString apiSlot = iniValue(base, QStringLiteral("venue.%1").arg(venue), QStringLiteral("api_slot"));
        if (apiSlot.isEmpty()) apiSlot = QStringLiteral("1");
        if (!venueApiSlots.contains(venue)) venueApiSlots.insert(venue, apiSlot);
        if (!venueExecutionByVenue.contains(venue)) {
            const QString venueKey = venueExecutionKey(path);
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
    outDir.mkpath(runId);
    const QString path = QDir(outDir.absoluteFilePath(runId)).absoluteFilePath(QStringLiteral("config.ini"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) return {};
    QTextStream out(&file);
    out << "# recorder backtest metadata\n";
    out << "# display_name=" << displayName_() << "\n";
    out << "# config_summary=" << configSummary_(overrides) << "\n\n";
    const QString filteredBase = filteredBaseConfig(base);
    out << filteredBase;
    if (!filteredBase.endsWith(QLatin1Char('\n'))) out << "\n";
    out << "\n# recorder backtest overrides\n";
    out << "[backtest]\n";
    writeBacktestRateLimitConfig(out, rateLimitsEnabled_);
    out << "\n";
    out << "[strategy]\n";
    out << "type=" << selectedStrategy_ << "\n";
    for (const QString& key : paramOrder_) {
        const hft_backtest::StrategyParamMetadata* param = paramMetadataFor(selectedStrategy_, key);
        if (param != nullptr && param->exclusiveGroup != 0u && activeParamByGroup_.value(static_cast<int>(param->exclusiveGroup)) != key) continue;
        if (fixedOnly && paramModes_.value(key, QStringLiteral("fixed")) != QStringLiteral("fixed")) continue;
        const QString value = overrides.value(key, paramValues_.value(key)).trimmed();
        if (!value.isEmpty()) out << key << "=" << value << "\n";
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
    }
    return path;
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
