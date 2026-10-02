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

#include "BacktestViewModelConfigInternal.hpp"

namespace hftrec::gui {
using detail::normalizedTradeMode;

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
    writeBacktestRateLimitConfig(out, rateLimitsEnabled_, strictRateLimitsEnabled_);
    out << "sweep_budget=" << sweepBudget_ << "\n";
    out << "sweep_seed=" << sweepSeed_ << "\n";
    out << "primary_leg_index=" << selectedPrimaryLegIndex() << "\n";
    out << "trade_mode=" << selectedTradeMode_ << "\n";
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
    const QString strictRateLimits = iniValue(text, QStringLiteral("backtest"), QStringLiteral("strict_rate_limits"));
    const QString sweepBudget = iniValue(text, QStringLiteral("backtest"), QStringLiteral("sweep_budget"));
    const QString sweepSeed = iniValue(text, QStringLiteral("backtest"), QStringLiteral("sweep_seed"));
    const QString primaryLegIndex = iniValue(text, QStringLiteral("backtest"), QStringLiteral("primary_leg_index"));
    const QString tradeMode = iniValue(text, QStringLiteral("backtest"), QStringLiteral("trade_mode"));
    const QString mode = iniValue(text, QStringLiteral("backtest"), QStringLiteral("config_mode"));
    if (!orderLatency.isEmpty()) pingLatencyUs_ = orderLatency;
    if (!latencySeed.isEmpty()) latencySeed_ = latencySeed;
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
    if (!strictRateLimits.isEmpty()) strictRateLimitsEnabled_ = rateLimitsEnabled_ && boolIniValue(strictRateLimits, strictRateLimitsEnabled_);
    if (!rateLimitsEnabled_) strictRateLimitsEnabled_ = false;
    if (!sweepBudget.isEmpty()) sweepBudget_ = sweepBudget;
    if (!sweepSeed.isEmpty()) sweepSeed_ = sweepSeed;
    if (!primaryLegIndex.isEmpty()) {
        bool ok = false;
        const int index = primaryLegIndex.toInt(&ok);
        if (ok) selectedPrimaryLegIndex_ = index;
    }
    if (!tradeMode.isEmpty()) selectedTradeMode_ = normalizedTradeMode(tradeMode);
    (void)normalizeSelectedPrimaryLeg_();
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
    emit primaryLegChanged();
    emit tradeModeChanged();
    emit sweepConfigChanged();
    emit configChanged();
    emit strategyParametersChanged();
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
    selectedPrimaryLegIndex_ = settings_.value(QStringLiteral("backtests/primary_leg_index"), selectedPrimaryLegIndex_).toInt();
    selectedTradeMode_ = normalizedTradeMode(settings_.value(QStringLiteral("backtests/trade_mode"), selectedTradeMode_).toString());
    configMode_ = normalizeConfigMode(settings_.value(QStringLiteral("backtests/config_mode/%1").arg(selectedStrategy_), configMode_).toString());
    configMode_ = QStringLiteral("fixed");
    selectedIndicatorProfile_ = settings_.value(QStringLiteral("backtests/indicator_profile/%1").arg(selectedStrategy_), defaultIndicatorProfileForStrategy(selectedStrategy_)).toString().trimmed();
    if (!indicatorProfileAllowedForStrategy(selectedStrategy_, selectedIndicatorProfile_)) selectedIndicatorProfile_ = defaultIndicatorProfileForStrategy(selectedStrategy_);
    const bool hasLegacyPingLatency = settings_.contains(QStringLiteral("backtests/ping_latency_us"));
    pingLatencyUs_ = settings_.value(QStringLiteral("backtests/ping_latency_us"), pingLatencyUs_).toString().trimmed();
    if (pingLatencyUs_.isEmpty()) pingLatencyUs_ = QStringLiteral("1000");
    latencySeed_ = settings_.value(QStringLiteral("backtests/latency_seed"), latencySeed_).toString().trimmed();
    if (latencySeed_.isEmpty()) latencySeed_ = QStringLiteral("0");
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
    if (settings_.contains(QStringLiteral("backtests/strict_rate_limits"))) {
        strictRateLimitsEnabled_ = rateLimitsEnabled_ && settings_.value(QStringLiteral("backtests/strict_rate_limits"), strictRateLimitsEnabled_).toBool();
    }
    if (!rateLimitsEnabled_) strictRateLimitsEnabled_ = false;
    makerFeeBps_ = QStringLiteral("0");
    takerFeeBps_ = QStringLiteral("0");
    sweepBudget_ = settings_.value(QStringLiteral("backtests/sweep_budget"), sweepBudget_).toString().trimmed();
    if (sweepBudget_.isEmpty()) sweepBudget_ = QStringLiteral("64");
    sweepSeed_ = settings_.value(QStringLiteral("backtests/sweep_seed"), sweepSeed_).toString().trimmed();
    if (sweepSeed_.isEmpty()) sweepSeed_ = QStringLiteral("0");
    batchUniverseId_ = settings_.value(QStringLiteral("backtests/batch_universe_id"), batchUniverseId_).toString().trimmed();
    batchPairBudget_ = settings_.value(QStringLiteral("backtests/batch_pair_budget"), batchPairBudget_).toString().trimmed();
    if (batchPairBudget_.isEmpty()) batchPairBudget_ = QStringLiteral("64");
    batchOnlyFutures_ = settings_.value(QStringLiteral("backtests/batch_only_futures"), batchOnlyFutures_).toBool();
    batchRawTableMode_ = settings_.value(QStringLiteral("backtests/batch_raw_table_mode"), batchRawTableMode_).toString().trimmed();
    if (batchRawTableMode_.isEmpty()) batchRawTableMode_ = QStringLiteral("stable");
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
    settings_.setValue(QStringLiteral("backtests/primary_leg_index"), selectedPrimaryLegIndex_);
    settings_.setValue(QStringLiteral("backtests/trade_mode"), selectedTradeMode_);
    settings_.setValue(QStringLiteral("backtests/config_mode/%1").arg(selectedStrategy_), configMode_);
    settings_.setValue(QStringLiteral("backtests/indicator_profile/%1").arg(selectedStrategy_), selectedIndicatorProfile_);
    settings_.setValue(QStringLiteral("backtests/ping_latency_us"), pingLatencyUs_);
    settings_.setValue(QStringLiteral("backtests/latency_seed"), latencySeed_);
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
    settings_.setValue(QStringLiteral("backtests/strict_rate_limits"), strictRateLimitsEnabled_);
    settings_.setValue(QStringLiteral("backtests/sweep_budget"), sweepBudget_);
    settings_.setValue(QStringLiteral("backtests/sweep_seed"), sweepSeed_);
    settings_.setValue(QStringLiteral("backtests/batch_universe_id"), batchUniverseId_);
    settings_.setValue(QStringLiteral("backtests/batch_pair_budget"), batchPairBudget_);
    settings_.setValue(QStringLiteral("backtests/batch_only_futures"), batchOnlyFutures_);
    settings_.setValue(QStringLiteral("backtests/batch_raw_table_mode"), batchRawTableMode_);
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

}  // namespace hftrec::gui
