#include "gui/backtests/BacktestStrategyConfigHelpers.hpp"

#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QVariantMap>

#include <cstddef>
#include <string>

namespace hftrec::gui {

QString qString(const char* text) {
    return text == nullptr ? QString{} : QString::fromUtf8(text);
}

const hft_backtest::StrategyMetadata* metadataForStrategy(const QString& strategy) {
    const std::string id = strategy.trimmed().toStdString();
    return hft_backtest::findStrategyMetadata(id);
}

bool isDiscoveredStrategy(const QString& strategy) {
    return metadataForStrategy(strategy) != nullptr;
}

QString firstDiscoveredStrategy() {
    const hft_backtest::StrategyMetadata* metadata = hft_backtest::strategyMetadataAt(0u);
    return metadata == nullptr ? QString{} : qString(metadata->id);
}

bool strategyMetadataSupportsSessionCount(const hft_backtest::StrategyMetadata& metadata, int selectedCount) {
    const int count = selectedCount <= 0 ? 1 : selectedCount;
    const int minCount = metadata.minSessionCount == 0u ? 1 : static_cast<int>(metadata.minSessionCount);
    const int maxCount = metadata.maxSessionCount == 0u ? 1 : static_cast<int>(metadata.maxSessionCount);
    return count >= minCount && count <= maxCount;
}

QString firstDiscoveredStrategyForSessionCount(int selectedCount) {
    const std::size_t count = hft_backtest::strategyMetadataCount();
    for (std::size_t i = 0; i < count; ++i) {
        const hft_backtest::StrategyMetadata* metadata = hft_backtest::strategyMetadataAt(i);
        if (metadata != nullptr && metadata->id != nullptr && strategyMetadataSupportsSessionCount(*metadata, selectedCount)) return qString(metadata->id);
    }
    return {};
}

QString configTemplatePathForStrategy(const QString& strategy) {
    const hft_backtest::StrategyMetadata* metadata = metadataForStrategy(strategy);
    if (metadata == nullptr || metadata->configTemplateFile == nullptr || metadata->configTemplateFile[0] == '\0') return {};
    return QDir(QStringLiteral(HFT_BACKTEST_TRADER_SOURCE_DIR)).absoluteFilePath(qString(metadata->configTemplateFile));
}

QString readTextFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(file.readAll());
}

QString iniValue(const QString& text, const QString& sectionName, const QString& keyName) {
    QString section;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (QString line : lines) {
        const qsizetype hash = line.indexOf(QLatin1Char('#'));
        if (hash >= 0) line = line.left(hash);
        line = line.trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            section = line.mid(1, line.size() - 2).trimmed().toLower();
            continue;
        }
        if (section != sectionName) continue;
        const qsizetype eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) continue;
        if (line.left(eq).trimmed().toLower() == keyName) return line.mid(eq + 1).trimmed();
    }
    return {};
}

bool boolIniValue(const QString& value, bool fallback) {
    const QString normalized = value.trimmed().toLower();
    if (normalized.isEmpty()) return fallback;
    if (normalized == QStringLiteral("1") || normalized == QStringLiteral("true") ||
        normalized == QStringLiteral("yes") || normalized == QStringLiteral("on")) return true;
    if (normalized == QStringLiteral("0") || normalized == QStringLiteral("false") ||
        normalized == QStringLiteral("no") || normalized == QStringLiteral("off")) return false;
    return fallback;
}

std::vector<IniKeyValue> iniSectionValues(const QString& text, const QString& sectionName) {
    std::vector<IniKeyValue> out;
    QString section;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (QString line : lines) {
        const qsizetype hash = line.indexOf(QLatin1Char('#'));
        if (hash >= 0) line = line.left(hash);
        line = line.trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            section = line.mid(1, line.size() - 2).trimmed().toLower();
            continue;
        }
        if (section != sectionName) continue;
        const qsizetype eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) continue;
        IniKeyValue row;
        row.key = line.left(eq).trimmed().toLower();
        row.value = line.mid(eq + 1).trimmed();
        out.push_back(row);
    }
    return out;
}

bool isTemplateStrategyParamKey(const QString& key) {
    return !key.isEmpty() && key != QStringLiteral("type") && key != QStringLiteral("enabled");
}

QString cleanProfileName(QString name) {
    name = name.trimmed();
    if (name.isEmpty()) name = QStringLiteral("default");
    name.replace(QLatin1Char('/'), QLatin1Char('_'));
    name.replace(QLatin1Char('\\'), QLatin1Char('_'));
    return name;
}

QString cleanRunSlugPart(QString text) {
    text = text.trimmed();
    QString out;
    out.reserve(text.size());
    bool lastDash = false;
    for (const QChar ch : text) {
        const bool keep = ch.isLetterOrNumber() || ch == QLatin1Char('_');
        if (keep) {
            out.push_back(ch);
            lastDash = false;
        } else if (!lastDash) {
            out.push_back(QLatin1Char('-'));
            lastDash = true;
        }
    }
    while (out.startsWith(QLatin1Char('-'))) out.remove(0, 1);
    while (out.endsWith(QLatin1Char('-'))) out.chop(1);
    return out.isEmpty() ? QStringLiteral("run") : out;
}

QString metadataCommentValue(const QString& text, const QString& key) {
    const QString prefix = QStringLiteral("# %1=").arg(key);
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (QString line : lines) {
        if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
        if (line.startsWith(prefix)) return line.mid(prefix.size()).trimmed();
    }
    return {};
}

QString normalizeConfigMode(QString) {
    return QStringLiteral("fixed");
}

QString groupSettingKey(int group) {
    return QStringLiteral("__group_%1").arg(group);
}

QString normalizedParamMode(QString mode) {
    mode = mode.trimmed().toLower();
    if (mode == QStringLiteral("sweep") || mode == QStringLiteral("random")) return QStringLiteral("sweep");
    return QStringLiteral("fixed");
}

QString sweepTemplateValue(const QString& templateText, const QString& key, const QString& field) {
    return iniValue(templateText, QStringLiteral("sweep"), QStringLiteral("%1.%2").arg(key, field)).trimmed();
}

QString defaultParamMode(const QString& templateText, const QString& key) {
    return normalizedParamMode(sweepTemplateValue(templateText, key, QStringLiteral("mode")));
}

QString defaultRangeMin(const QString& templateText, const QString& key, const QString& value) {
    const QString configured = sweepTemplateValue(templateText, key, QStringLiteral("min"));
    return configured.isEmpty() ? value : configured;
}

QString defaultRangeMax(const QString& templateText, const QString& key, const QString& value) {
    const QString configured = sweepTemplateValue(templateText, key, QStringLiteral("max"));
    return configured.isEmpty() ? value : configured;
}

QString defaultRangeStep(const QString& templateText, const QString& key) {
    const QString configured = sweepTemplateValue(templateText, key, QStringLiteral("step"));
    return configured.isEmpty() ? QStringLiteral("1") : configured;
}

QString paramGroupKey(const hft_backtest::StrategyMetadata& metadata, std::uint8_t group) {
    for (std::size_t i = 0; i < metadata.paramGroupCount && i < hft_backtest::kStrategyMetadataMaxParamGroups; ++i) {
        const hft_backtest::StrategyParamGroupMetadata& paramGroup = metadata.paramGroups[i];
        if (paramGroup.id == group && paramGroup.key != nullptr && paramGroup.key[0] != '\0') return qString(paramGroup.key);
    }
    return groupSettingKey(group);
}

QVariantList paramGroupChoices(const hft_backtest::StrategyMetadata& metadata, std::uint8_t group) {
    QVariantList choices;
    for (std::size_t i = 0; i < metadata.paramCount && i < hft_backtest::kStrategyMetadataMaxParams; ++i) {
        const hft_backtest::StrategyParamMetadata& param = metadata.params[i];
        if (param.exclusiveGroup != group || param.key == nullptr || param.key[0] == '\0') continue;
        const QString key = qString(param.key);
        QVariantMap row;
        row.insert(QStringLiteral("id"), key);
        row.insert(QStringLiteral("label"), key);
        choices.push_back(row);
    }
    return choices;
}

bool paramExistsInExclusiveGroup(const hft_backtest::StrategyMetadata& metadata, std::uint8_t group, const QString& key) {
    const std::string needle = key.trimmed().toLower().toStdString();
    for (std::size_t i = 0; i < metadata.paramCount && i < hft_backtest::kStrategyMetadataMaxParams; ++i) {
        const hft_backtest::StrategyParamMetadata& param = metadata.params[i];
        if (param.exclusiveGroup == group && param.key != nullptr && needle == param.key) return true;
    }
    return false;
}

bool metadataHasParam(const hft_backtest::StrategyMetadata& metadata, const QString& key) {
    const std::string needle = key.trimmed().toLower().toStdString();
    for (std::size_t i = 0; i < metadata.paramCount && i < hft_backtest::kStrategyMetadataMaxParams; ++i) {
        if (metadata.params[i].key != nullptr && needle == metadata.params[i].key) return true;
    }
    return false;
}

const hft_backtest::StrategyParamMetadata* paramMetadataFor(const QString& strategy, const QString& key) {
    const hft_backtest::StrategyMetadata* metadata = metadataForStrategy(strategy);
    if (metadata == nullptr) return nullptr;
    const std::string needle = key.trimmed().toLower().toStdString();
    for (std::size_t i = 0; i < metadata->paramCount && i < hft_backtest::kStrategyMetadataMaxParams; ++i) {
        const hft_backtest::StrategyParamMetadata& param = metadata->params[i];
        if (param.key != nullptr && needle == param.key) return &param;
    }
    return nullptr;
}

QString filteredBaseConfig(const QString& base) {
    QString out;
    QTextStream stream(&out);
    bool skipSection = false;
    const QStringList lines = base.split(QLatin1Char('\n'));
    for (QString line : lines) {
        if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
        QString probe = line;
        const qsizetype hash = probe.indexOf(QLatin1Char('#'));
        if (hash >= 0) probe = probe.left(hash);
        probe = probe.trimmed();
        if (probe.startsWith(QLatin1Char('[')) && probe.endsWith(QLatin1Char(']'))) {
            skipSection = true;
            continue;
        }
        if (skipSection) continue;
        stream << line << "\n";
    }
    return out;
}

QString defaultIndicatorProfileForStrategy(const QString& strategy) {
    const hft_backtest::StrategyMetadata* metadata = metadataForStrategy(strategy);
    if (metadata == nullptr || metadata->indicatorCount == 0u || metadata->indicators[0].id == nullptr) return {};
    return qString(metadata->indicators[0].id);
}

QVariantMap indicatorChoice(const hft_backtest::StrategyIndicatorMetadata& indicator) {
    QVariantMap row;
    const QString id = qString(indicator.id);
    const QString label = qString(indicator.labelRu);
    row.insert(QStringLiteral("id"), id);
    row.insert(QStringLiteral("label"), label.isEmpty() ? id : label);
    return row;
}

QString strategySessionRangeText(const hft_backtest::StrategyMetadata& metadata) {
    const int minCount = metadata.minSessionCount == 0u ? 1 : static_cast<int>(metadata.minSessionCount);
    const int maxCount = metadata.maxSessionCount == 0u ? 1 : static_cast<int>(metadata.maxSessionCount);
    if (minCount == maxCount) return QStringLiteral("%1 session%2").arg(maxCount).arg(maxCount == 1 ? QString{} : QStringLiteral("s"));
    return QStringLiteral("%1-%2 sessions").arg(minCount).arg(maxCount);
}

QString strategySessionGateText(int selectedCount, const hft_backtest::StrategyMetadata& metadata) {
    return QStringLiteral("Selected %1 session%2; strategy supports %3")
        .arg(selectedCount)
        .arg(selectedCount == 1 ? QString{} : QStringLiteral("s"))
        .arg(strategySessionRangeText(metadata));
}

QString strategySessionGateText(const QString& strategy, int selectedCount) {
    const hft_backtest::StrategyMetadata* metadata = metadataForStrategy(strategy);
    return metadata == nullptr ? QString{} : strategySessionGateText(selectedCount, *metadata);
}

bool indicatorProfileAllowedForStrategy(const QString& strategy, const QString& profile) {
    if (profile.trimmed().isEmpty()) return true;
    const hft_backtest::StrategyMetadata* metadata = metadataForStrategy(strategy);
    if (metadata == nullptr) return false;
    const std::string needle = profile.trimmed().toStdString();
    for (std::size_t i = 0; i < metadata->indicatorCount && i < hft_backtest::kStrategyMetadataMaxIndicators; ++i) {
        const hft_backtest::StrategyIndicatorMetadata& indicator = metadata->indicators[i];
        if (indicator.id != nullptr && needle == indicator.id) return true;
    }
    return false;
}

}  // namespace hftrec::gui
