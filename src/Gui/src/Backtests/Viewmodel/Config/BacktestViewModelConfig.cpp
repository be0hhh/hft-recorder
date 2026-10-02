#include "BacktestViewModelConfigInternal.hpp"
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
namespace detail {

QString normalizedTradeMode(const QString& mode) {
    const QString normalized = mode.trimmed().toLower();
    if (normalized == QStringLiteral("primary")) return QStringLiteral("primary");
    return QStringLiteral("all");
}

}  // namespace detail

using detail::normalizedTradeMode;

void BacktestViewModel::reloadSessions() {
    if (recordingCatalog_ != nullptr) recordingCatalog_->refresh();
    ++sessionsLoadGeneration_;
    sessions_ = loadSessions_();
    if (manualSessionPath_.trimmed().isEmpty()) {
        const QVariantMap selectedRow = sessionRowById(sessions_, selectedSessionId_);
        const bool selectedGroup = selectedRow.value(QStringLiteral("isGroup")).toBool();
        if (selectedSessionId_.trimmed().isEmpty() || selectedRow.isEmpty() || (!selectedGroup && !sessionRowSelectable(selectedRow))) {
            selectedSessionId_ = firstSelectableSessionId(sessions_);
        }
    }
    loadLegSelectionForCurrentSession_();
    const bool primaryChanged = normalizeSelectedPrimaryLeg_();
    if (primaryChanged) savePersistentConfig_();
    const bool strategyChanged = ensureSelectedStrategySupportsSessionCount_();
    emit sessionsChanged();
    emit selectedSessionChanged();
    emit multiSessionChanged();
    emit legSelectionChanged();
    emit primaryLegChanged();
    if (!strategyChanged) emit selectedStrategyChanged();
    emit canRunChanged();
}

void BacktestViewModel::setSelectedSessionId(const QString& sessionId) {
    setSelectedSessionId_(sessionId, false);
}

void BacktestViewModel::setSelectedSessionIdForLegSelection(const QString& sessionId) {
    const QString next = sessionId.trimmed();
    if (next.isEmpty()) return;
    pendingLegSelectionSessionId_ = next;
    deferredLegSelectionRefresh_ = true;
    loadLegSelectionForCurrentSession_();
    emit multiSessionChanged();
    emit legSelectionChanged();
    emit primaryLegChanged();
}

void BacktestViewModel::setSelectedSessionId_(const QString& sessionId, bool deferRefresh) {
    const QString next = sessionId.trimmed();
    if (selectedSessionId_ == next && manualSessionPath_.isEmpty() && pendingLegSelectionSessionId_.trimmed().isEmpty()) return;
    pendingLegSelectionSessionId_.clear();
    selectedSessionId_ = next;
    manualSessionPath_.clear();
    deferredLegSelectionRefresh_ = deferRefresh;
    loadLegSelectionForCurrentSession_();
    symbolOverride_.clear();
    selectedRunId_.clear();
    ++detailsLoadGeneration_;
    selectedDetailsErrorText_.clear();
    setDetailsLoading_(false);
    const bool strategyChanged = ensureSelectedStrategySupportsSessionCount_();
    emit selectedSessionChanged();
    emit multiSessionChanged();
    emit legSelectionChanged();
    emit primaryLegChanged();
    if (!strategyChanged) emit selectedStrategyChanged();
    emit symbolChanged();
    emit canRunChanged();
    emit detailsLoadingChanged();
    if (!deferRefresh) {
        scheduleRefresh_();
    }
}

void BacktestViewModel::setSessionPath(const QString& sessionPath) {
    const QString normalized = normalizedPath_(sessionPath);
    if (selectedSessionPath() == normalized) return;
    pendingLegSelectionSessionId_.clear();
    manualSessionPath_ = normalized;
    selectedSessionId_ = sessionIdFromPath_(normalized);
    deferredLegSelectionRefresh_ = false;
    loadLegSelectionForCurrentSession_();
    symbolOverride_.clear();
    selectedRunId_.clear();
    ++detailsLoadGeneration_;
    selectedDetailsErrorText_.clear();
    setDetailsLoading_(false);
    const bool strategyChanged = ensureSelectedStrategySupportsSessionCount_();
    emit selectedSessionChanged();
    emit multiSessionChanged();
    emit legSelectionChanged();
    emit primaryLegChanged();
    if (!strategyChanged) emit selectedStrategyChanged();
    emit symbolChanged();
    emit canRunChanged();
    emit detailsLoadingChanged();
    refresh();
}

void BacktestViewModel::setExtraSessionIds(const QString& sessionIds) {
    const QString next = sessionIds.trimmed();
    if (extraSessionIds_ == next) return;
    pendingLegSelectionSessionId_.clear();
    extraSessionIds_ = next;
    deferredLegSelectionRefresh_ = false;
    loadLegSelectionForCurrentSession_();
    const bool primaryChanged = normalizeSelectedPrimaryLeg_();
    savePersistentConfig_();
    const bool strategyChanged = ensureSelectedStrategySupportsSessionCount_();
    emit multiSessionChanged();
    emit legSelectionChanged();
    emit primaryLegChanged();
    if (!strategyChanged) emit selectedStrategyChanged();
    emit canRunChanged();
    refreshSessionGateStatus_();
    refresh();
}

void BacktestViewModel::setSelectedPrimaryLegIndex(int index) {
    const QStringList paths = selectedSessionCandidatePaths_();
    if (paths.empty()) index = 0;
    if (index < 0 || index >= paths.size() || disabledSessionLegPaths_.contains(paths.at(index))) {
        index = normalizedSelectedPrimaryLegIndex_();
    }
    if (selectedPrimaryLegIndex_ == index) return;
    selectedPrimaryLegIndex_ = index;
    savePersistentConfig_();
    emit primaryLegChanged();
    emit multiSessionChanged();
}

void BacktestViewModel::setSelectedTradeMode(const QString& mode) {
    const QString next = normalizedTradeMode(mode);
    if (selectedTradeMode_ == next) return;
    selectedTradeMode_ = next;
    savePersistentConfig_();
    emit tradeModeChanged();
    emit multiSessionChanged();
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
    const QStringList paths = selectedSessionCandidatePaths_();
    if (legIndex < 0 || legIndex >= paths.size()) return;
    const QString venueKey = venueExecutionKeyForPath_(paths.at(legIndex));
    if (venueKey.isEmpty()) return;
    const QString next = value.trimmed();
    const QString mapKey = venueExecutionMapKey(venueKey, normalizedField);
    if (venueExecutionValues_.value(mapKey) == next) return;
    venueExecutionValues_.insert(mapKey, next);
    settings_.setValue(QStringLiteral("backtests/venue_execution/%1/%2")
                           .arg(venueExecutionSettingKey(venueKey), normalizedField),
                       next);
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
    if (!rateLimitsEnabled_) strictRateLimitsEnabled_ = false;
    savePersistentConfig_();
    emit rateLimitsChanged();
    emit multiSessionChanged();
}

void BacktestViewModel::setStrictRateLimitsEnabled(bool enabled) {
    const bool next = enabled && rateLimitsEnabled_;
    if (strictRateLimitsEnabled_ == next) return;
    strictRateLimitsEnabled_ = next;
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

}  // namespace hftrec::gui
