#pragma once

#include "hft_backtest/backtest.hpp"

#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>
#include <vector>

namespace hftrec::gui {

struct IniKeyValue {
    QString key;
    QString value;
};

QString qString(const char* text);
const hft_backtest::StrategyMetadata* metadataForStrategy(const QString& strategy);
bool isDiscoveredStrategy(const QString& strategy);
QString firstDiscoveredStrategy();
bool strategyMetadataSupportsSessionCount(const hft_backtest::StrategyMetadata& metadata, int selectedCount);
QString firstDiscoveredStrategyForSessionCount(int selectedCount);
QString configTemplatePathForStrategy(const QString& strategy);
QString readTextFile(const QString& path);
QString iniValue(const QString& text, const QString& sectionName, const QString& keyName);
bool boolIniValue(const QString& value, bool fallback = false);
std::vector<IniKeyValue> iniSectionValues(const QString& text, const QString& sectionName);
bool isTemplateStrategyParamKey(const QString& key);
QString cleanProfileName(QString name);
QString cleanRunSlugPart(QString text);
QString metadataCommentValue(const QString& text, const QString& key);
QString normalizeConfigMode(QString mode);
QString groupSettingKey(int group);
QString normalizedParamMode(QString mode);
QString sweepTemplateValue(const QString& templateText, const QString& key, const QString& field);
QString defaultParamMode(const QString& templateText, const QString& key);
QString defaultRangeMin(const QString& templateText, const QString& key, const QString& value);
QString defaultRangeMax(const QString& templateText, const QString& key, const QString& value);
QString defaultRangeStep(const QString& templateText, const QString& key);
QString paramGroupKey(const hft_backtest::StrategyMetadata& metadata, std::uint8_t group);
QVariantList paramGroupChoices(const hft_backtest::StrategyMetadata& metadata, std::uint8_t group);
bool paramExistsInExclusiveGroup(const hft_backtest::StrategyMetadata& metadata, std::uint8_t group, const QString& key);
bool metadataHasParam(const hft_backtest::StrategyMetadata& metadata, const QString& key);
const hft_backtest::StrategyParamMetadata* paramMetadataFor(const QString& strategy, const QString& key);
QString filteredBaseConfig(const QString& base);
QString defaultIndicatorProfileForStrategy(const QString& strategy);
QVariantMap indicatorChoice(const hft_backtest::StrategyIndicatorMetadata& indicator);
QString strategySessionRangeText(const hft_backtest::StrategyMetadata& metadata);
QString strategySessionGateText(int selectedCount, const hft_backtest::StrategyMetadata& metadata);
QString strategySessionGateText(const QString& strategy, int selectedCount);
bool indicatorProfileAllowedForStrategy(const QString& strategy, const QString& profile);

}  // namespace hftrec::gui
