#include "gui/viewmodels/BacktestViewModel.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaObject>
#include <QTextStream>
#include <QVariantMap>

#include <algorithm>
#include <limits>

#include "hft_backtest/backtest.hpp"
#include "core/common/Status.hpp"

namespace hftrec::gui {
namespace {

struct StrategyParamSpec {
    const char* strategy;
    const char* key;
    const char* label;
    const char* description;
};

struct IniKeyValue {
    QString key;
    QString value;
};

struct NatrParamPair {
    const char* strategy;
    const char* fixedKey;
    const char* natrKey;
};

constexpr StrategyParamSpec kParamSpecs[] = {
    {"spread_maker1and2", "distance_bps", "Distance bps", "Limit quote distance from BBO. 100 bps = 1%."},
    {"spread_maker1and2", "distance_natr_pct", "Distance NATR %", "Limit quote distance as percent of trade range EMA."},
    {"spread_maker1and2", "trigger_bps", "Requote trigger bps", "Price move needed before strategy replaces quote."},
    {"spread_maker1and2", "trigger_natr_pct", "Requote trigger NATR %", "Price move threshold as percent of trade range EMA."},
    {"spread_maker1and2", "natr_ema_period_seconds", "NATR EMA seconds", "Trade range EMA window."},
    {"spread_maker1and2", "refresh_ms", "Refresh ms", "Minimum delay between quote refreshes."},
    {"spread_maker1and2", "close_delay_us", "Close delay us", "Strategy close-order delay from ini."},
    {"spread_maker1and2", "amount_qty", "Quote amount", "Order size in quote currency."},
    {"liquidity_volume_maker", "max_quote_distance_bps", "Max distance bps", "Maximum quote distance from BBO."},
    {"liquidity_volume_maker", "max_quote_distance_natr_pct", "Max distance NATR %", "Maximum quote distance as percent of trade range EMA."},
    {"liquidity_volume_maker", "price_move_bps", "Move trigger bps", "Price move needed before requote."},
    {"liquidity_volume_maker", "price_move_natr_pct", "Move trigger NATR %", "Price move threshold as percent of trade range EMA."},
    {"liquidity_volume_maker", "natr_ema_period_seconds", "NATR EMA seconds", "Trade range EMA window."},
    {"liquidity_volume_maker", "refresh_ms", "Refresh ms", "Minimum delay between quote refreshes."},
    {"liquidity_volume_maker", "close_delay_us", "Close delay us", "Strategy close-order delay from ini."},
    {"liquidity_volume_maker", "amount_qty", "Quote amount", "Order size in quote currency."},
    {"liquidity_wall_breakout", "scan_distance_bps", "Scan distance bps", "Scan distance from BBO."},
    {"liquidity_wall_breakout", "scan_distance_natr_pct", "Scan distance NATR %", "Scan distance as percent of trade range EMA."},
    {"liquidity_wall_breakout", "natr_ema_period_seconds", "NATR EMA seconds", "Trade range EMA window."},
    {"liquidity_wall_breakout", "min_wall_notional", "Min wall notional", "Minimum wall size in quote currency."},
    {"liquidity_wall_breakout", "wall_vs_rolling_pct", "Wall vs volume %", "Dynamic wall threshold versus rolling volume."},
    {"liquidity_wall_breakout", "hold_seconds", "Hold seconds", "How long wall must stay before action."},
    {"liquidity_wall_breakout", "close_delay_us", "Close delay us", "Strategy close-order delay from ini."},
    {"liquidity_wall_breakout", "amount_qty", "Quote amount", "Order size in quote currency."},
    {"liquidity_wall_rebound", "detect_distance_bps", "Detect distance bps", "Distance where wall can be detected."},
    {"liquidity_wall_rebound", "detect_distance_natr_pct", "Detect distance NATR %", "Detect distance as percent of trade range EMA."},
    {"liquidity_wall_rebound", "action_distance_bps", "Action distance bps", "Distance where strategy can act."},
    {"liquidity_wall_rebound", "action_distance_natr_pct", "Action distance NATR %", "Action distance as percent of trade range EMA."},
    {"liquidity_wall_rebound", "natr_ema_period_seconds", "NATR EMA seconds", "Trade range EMA window."},
    {"liquidity_wall_rebound", "hold_seconds", "Hold seconds", "How long wall must stay before action."},
    {"liquidity_wall_rebound", "order_qty", "Base order qty", "Order size in base currency."},
    {"iceberg_detector", "book_observe_max_distance_bps", "Observe distance bps", "How far from BBO detector watches book levels."},
    {"iceberg_detector", "classic_min_traded_notional", "Classic min traded", "Minimum traded notional for classic signal."},
    {"iceberg_detector", "smart_min_traded_notional", "Smart min traded", "Minimum traded notional for smart signal."},
    {"iceberg_detector", "amount_qty", "Readiness amount", "Dry-run order amount used by runtime readiness."},
};

constexpr NatrParamPair kNatrPairs[] = {
    {"spread_maker1and2", "distance_bps", "distance_natr_pct"},
    {"spread_maker1and2", "trigger_bps", "trigger_natr_pct"},
    {"liquidity_volume_maker", "max_quote_distance_bps", "max_quote_distance_natr_pct"},
    {"liquidity_volume_maker", "price_move_bps", "price_move_natr_pct"},
    {"liquidity_wall_breakout", "scan_distance_bps", "scan_distance_natr_pct"},
    {"liquidity_wall_rebound", "detect_distance_bps", "detect_distance_natr_pct"},
    {"liquidity_wall_rebound", "action_distance_bps", "action_distance_natr_pct"},
};

QString jsonValueString(const QJsonObject& object, const QString& key) {
    const QJsonValue value = object.value(key);
    if (value.isString()) return value.toString();
    if (value.isDouble()) return QString::number(value.toInteger());
    if (value.isArray() && !value.toArray().isEmpty()) return value.toArray().at(0).toString();
    return {};
}

QString manifestObjectValue(const QJsonObject& object, const QString& key) {
    const QString direct = jsonValueString(object, key);
    if (!direct.isEmpty()) return direct;
    const QJsonObject identity = object.value(QStringLiteral("identity")).toObject();
    return identity.isEmpty() ? QString{} : jsonValueString(identity, key);
}

QString prettyJson(const QJsonValue& value) {
    if (value.isUndefined()) return {};
    QJsonDocument doc;
    if (value.isObject()) doc = QJsonDocument(value.toObject());
    else if (value.isArray()) doc = QJsonDocument(value.toArray());
    else return QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("value"), value}}).toJson(QJsonDocument::Indented));
    return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
}

int errorCount(const QJsonValue& value) {
    return value.isArray() ? value.toArray().size() : 0;
}

QString resolveRecordingsRoot() {
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString candidate = appDir.absoluteFilePath(QStringLiteral("../../recordings"));
    return QDir::cleanPath(QFileInfo(candidate).absoluteFilePath());
}

QString statusTextFor(hft_backtest::Status status) {
    if (status == hft_backtest::Status::Ok) return QStringLiteral("Backtest complete");
    if (status == hft_backtest::Status::Cancelled) return QStringLiteral("Backtest cancelled");
    return QStringLiteral("Backtest failed: %1").arg(QString::fromUtf8(hft_backtest::statusToString(status).data(), static_cast<qsizetype>(hft_backtest::statusToString(status).size())));
}

bool progressCallback(const hft_backtest::BacktestProgress& progress, void* userData) noexcept {
    auto* vm = static_cast<BacktestViewModel*>(userData);
    if (vm == nullptr) return false;
    const QString text = QStringLiteral("%1: %2/%3 events")
        .arg(QString::fromStdString(progress.stage))
        .arg(static_cast<qulonglong>(progress.eventsDone))
        .arg(static_cast<qulonglong>(progress.eventsTotal));
    QMetaObject::invokeMethod(vm, [vm, percent = static_cast<int>(progress.percent), text] {
        vm->applyWorkerProgress(percent, text);
    }, Qt::QueuedConnection);
    return !vm->workerCancelRequested();
}

QString configFileForStrategy(const QString& strategy) {
    const QString root = QStringLiteral(HFT_BACKTEST_TRADER_SOURCE_DIR);
    const QString id = strategy.trimmed();
    QString file = QStringLiteral("1and2.ini");
    if (id == QStringLiteral("horizontal_levels")) file = QStringLiteral("hor.ini");
    else if (id == QStringLiteral("iceberg_detector")) file = QStringLiteral("iceberg.ini");
    else if (id == QStringLiteral("liquidity_wall_breakout")) file = QStringLiteral("wall.ini");
    else if (id == QStringLiteral("liquidity_wall_rebound")) file = QStringLiteral("rebound.ini");
    else if (id == QStringLiteral("liquidity_volume_maker")) file = QStringLiteral("liq.ini");
    return QDir(root).absoluteFilePath(file);
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

const StrategyParamSpec* paramSpecFor(const QString& strategy, const QString& key) {
    for (const StrategyParamSpec& spec : kParamSpecs) {
        if (strategy == QString::fromUtf8(spec.strategy) && key == QString::fromUtf8(spec.key)) return &spec;
    }
    return nullptr;
}

QString labelForParamKey(const QString& key) {
    QString out = key;
    out.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!out.isEmpty()) out[0] = out[0].toUpper();
    return out;
}

QString manifestValue(const QString& sessionPath, const QString& key) {
    QFile file(QDir(sessionPath).absoluteFilePath(QStringLiteral("manifest.json")));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return {};
    return manifestObjectValue(doc.object(), key);
}

QString symbolFromSessionId(const QString& sessionId) {
    const QStringList parts = sessionId.split(QLatin1Char('_'), Qt::SkipEmptyParts);
    return parts.isEmpty() ? QString{} : parts.constLast().trimmed();
}

QString venueSectionFor(const QString& exchange, const QString& market) {
    if (exchange == QStringLiteral("binance") && market.startsWith(QStringLiteral("futures"))) return QStringLiteral("binance_futures");
    if (exchange == QStringLiteral("bybit")) return QStringLiteral("bybit_linear");
    if (exchange == QStringLiteral("kucoin")) return QStringLiteral("kucoin_futures");
    if (exchange == QStringLiteral("gate")) return QStringLiteral("gate_futures");
    if (exchange == QStringLiteral("bitget")) return QStringLiteral("bitget_futures");
    if (exchange == QStringLiteral("okx")) return QStringLiteral("okx_swap");
    return QStringLiteral("binance_futures");
}

QString cleanProfileName(QString name) {
    name = name.trimmed();
    if (name.isEmpty()) name = QStringLiteral("default");
    name.replace(QLatin1Char('/'), QLatin1Char('_'));
    name.replace(QLatin1Char('\\'), QLatin1Char('_'));
    return name;
}

QString normalizeConfigMode(QString mode) {
    mode = mode.trimmed().toLower();
    if (mode == QStringLiteral("natr") || mode == QStringLiteral("natr_pct") || mode == QStringLiteral("natr%")) return QStringLiteral("natr");
    return QStringLiteral("fixed");
}

bool strategyHasNatrMode(const QString& strategy) noexcept {
    for (const NatrParamPair& pair : kNatrPairs) {
        if (strategy == QString::fromUtf8(pair.strategy)) return true;
    }
    return false;
}

bool natrControlledKey(const QString& strategy, const QString& key) noexcept {
    if (key == QStringLiteral("natr_ema_period_seconds") && strategyHasNatrMode(strategy)) return true;
    for (const NatrParamPair& pair : kNatrPairs) {
        if (strategy != QString::fromUtf8(pair.strategy)) continue;
        if (key == QString::fromUtf8(pair.fixedKey) || key == QString::fromUtf8(pair.natrKey)) return true;
    }
    return false;
}

bool keyVisibleForConfigMode(const QString& strategy, const QString& key, const QString& mode) noexcept {
    if (!strategyHasNatrMode(strategy)) return true;
    if (key == QStringLiteral("natr_ema_period_seconds")) return mode == QStringLiteral("natr");
    for (const NatrParamPair& pair : kNatrPairs) {
        if (strategy != QString::fromUtf8(pair.strategy)) continue;
        if (key == QString::fromUtf8(pair.fixedKey)) return mode == QStringLiteral("fixed");
        if (key == QString::fromUtf8(pair.natrKey)) return mode == QStringLiteral("natr");
    }
    return true;
}

QString fallbackValueForKey(const QString& strategy, const QString& key) {
    if (key == QStringLiteral("natr_ema_period_seconds")) return QStringLiteral("60");
    if (strategy == QStringLiteral("spread_maker1and2") && key == QStringLiteral("distance_bps")) return QStringLiteral("20");
    if (strategy == QStringLiteral("spread_maker1and2") && key == QStringLiteral("distance_natr_pct")) return QStringLiteral("200");
    if (strategy == QStringLiteral("spread_maker1and2") && key == QStringLiteral("trigger_bps")) return QStringLiteral("2");
    if (strategy == QStringLiteral("spread_maker1and2") && key == QStringLiteral("trigger_natr_pct")) return QStringLiteral("100");
    if (strategy == QStringLiteral("liquidity_volume_maker") && key == QStringLiteral("max_quote_distance_bps")) return QStringLiteral("300");
    if (strategy == QStringLiteral("liquidity_volume_maker") && key == QStringLiteral("max_quote_distance_natr_pct")) return QStringLiteral("800");
    if (strategy == QStringLiteral("liquidity_volume_maker") && key == QStringLiteral("price_move_bps")) return QStringLiteral("10");
    if (strategy == QStringLiteral("liquidity_volume_maker") && key == QStringLiteral("price_move_natr_pct")) return QStringLiteral("100");
    if (strategy == QStringLiteral("liquidity_wall_breakout") && key == QStringLiteral("scan_distance_bps")) return QStringLiteral("120");
    if (strategy == QStringLiteral("liquidity_wall_breakout") && key == QStringLiteral("scan_distance_natr_pct")) return QStringLiteral("600");
    if (strategy == QStringLiteral("liquidity_wall_rebound") && key == QStringLiteral("detect_distance_bps")) return QStringLiteral("300");
    if (strategy == QStringLiteral("liquidity_wall_rebound") && key == QStringLiteral("detect_distance_natr_pct")) return QStringLiteral("100");
    if (strategy == QStringLiteral("liquidity_wall_rebound") && key == QStringLiteral("action_distance_bps")) return QStringLiteral("200");
    if (strategy == QStringLiteral("liquidity_wall_rebound") && key == QStringLiteral("action_distance_natr_pct")) return QStringLiteral("100");
    for (const NatrParamPair& pair : kNatrPairs) {
        if (strategy != QString::fromUtf8(pair.strategy)) continue;
        const QString fixedKey = QString::fromUtf8(pair.fixedKey);
        const QString natrKey = QString::fromUtf8(pair.natrKey);
        if (key == natrKey) {
            if (fixedKey.contains(QStringLiteral("trigger")) || fixedKey.contains(QStringLiteral("move")) || fixedKey.contains(QStringLiteral("action"))) {
                return QStringLiteral("100");
            }
            return QStringLiteral("600");
        }
    }
    return {};
}

QString filteredBaseConfig(const QString& base, const QString& strategy) {
    QString out;
    QTextStream stream(&out);
    QString section;
    bool skipSection = false;
    const QStringList lines = base.split(QLatin1Char('\n'));
    for (QString line : lines) {
        if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
        QString probe = line;
        const qsizetype hash = probe.indexOf(QLatin1Char('#'));
        if (hash >= 0) probe = probe.left(hash);
        probe = probe.trimmed();
        if (probe.startsWith(QLatin1Char('[')) && probe.endsWith(QLatin1Char(']'))) {
            section = probe.mid(1, probe.size() - 2).trimmed().toLower();
            skipSection = section.startsWith(QStringLiteral("venue."));
            if (skipSection) continue;
            stream << line << "\n";
            continue;
        }
        if (skipSection) continue;
        const qsizetype eq = probe.indexOf(QLatin1Char('='));
        if (section == QStringLiteral("strategy") && eq > 0) {
            const QString key = probe.left(eq).trimmed().toLower();
            if (natrControlledKey(strategy, key)) continue;
        }
        stream << line << "\n";
    }
    return out;
}

}  // namespace

BacktestViewModel::BacktestViewModel(QObject* parent) : QObject(parent) {
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, [this]() { refresh(); });
    connect(&watcher_, &QFileSystemWatcher::fileChanged, this, [this]() { refresh(); });
    loadPersistentConfig_();
    const QVariantList rows = sessions();
    if (!rows.empty()) selectedSessionId_ = rows.front().toMap().value(QStringLiteral("id")).toString();
    refresh();
}

BacktestViewModel::~BacktestViewModel() {
    cancelBacktest();
    stopWorker_();
}

QString BacktestViewModel::recordingsRoot() const {
    return resolveRecordingsRoot();
}

QVariantList BacktestViewModel::sessions() const {
    QVariantList out;
    QDir recordingsDir(recordingsRoot());
    if (!recordingsDir.exists()) return out;

    const auto entries = recordingsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time | QDir::Reversed);
    for (const auto& entry : entries) {
        if (entry == QStringLiteral("backtest_profiles")) continue;
        const QString path = recordingsDir.absoluteFilePath(entry);
        QVariantMap row;
        row.insert(QStringLiteral("id"), entry);
        row.insert(QStringLiteral("label"), entry);
        row.insert(QStringLiteral("path"), path);
        row.insert(QStringLiteral("hasManifest"), QFileInfo::exists(QDir(path).absoluteFilePath(QStringLiteral("manifest.json"))));
        row.insert(QStringLiteral("hasBacktests"), QDir(QDir(path).absoluteFilePath(QStringLiteral("backtests"))).exists());
        out.push_back(row);
    }
    return out;
}

QString BacktestViewModel::selectedSessionPath() const {
    if (!manualSessionPath_.trimmed().isEmpty()) return manualSessionPath_;
    if (selectedSessionId_.trimmed().isEmpty()) return {};
    return QDir(recordingsRoot()).absoluteFilePath(selectedSessionId_);
}

QString BacktestViewModel::selectedSymbol() const {
    const QString manual = symbolOverride_.trimmed().toUpper();
    if (!manual.isEmpty()) return manual;
    const QString fromManifest = manifestValue(selectedSessionPath(), QStringLiteral("symbols")).trimmed().toUpper();
    if (!fromManifest.isEmpty()) return fromManifest;
    return symbolFromSessionId(selectedSessionId_).toUpper();
}

QString BacktestViewModel::backtestsDirectory() const {
    const QString path = selectedSessionPath();
    return path.isEmpty() ? QString{} : QDir(path).absoluteFilePath(QStringLiteral("backtests"));
}

QVariantList BacktestViewModel::strategyChoices() const {
    static constexpr const char* kStrategies[] = {
        "spread_maker1and2",
        "horizontal_levels",
        "iceberg_detector",
        "liquidity_wall_breakout",
        "liquidity_wall_rebound",
        "liquidity_volume_maker",
    };
    QVariantList out;
    for (const char* strategy : kStrategies) {
        QVariantMap row;
        row.insert(QStringLiteral("id"), QString::fromUtf8(strategy));
        row.insert(QStringLiteral("label"), QString::fromUtf8(strategy));
        out.push_back(row);
    }
    return out;
}

QVariantList BacktestViewModel::configModeChoices() const {
    QVariantList out;
    QVariantMap fixed;
    fixed.insert(QStringLiteral("id"), QStringLiteral("fixed"));
    fixed.insert(QStringLiteral("label"), QStringLiteral("Fixed bps"));
    out.push_back(fixed);
    if (strategyHasNatrMode(selectedStrategy_)) {
        QVariantMap natr;
        natr.insert(QStringLiteral("id"), QStringLiteral("natr"));
        natr.insert(QStringLiteral("label"), QStringLiteral("NATR %"));
        out.push_back(natr);
    }
    return out;
}

QVariantList BacktestViewModel::strategyParameters() const {
    QVariantList out;
    for (const QString& key : paramOrder_) {
        if (!keyVisibleForConfigMode(selectedStrategy_, key, configMode_)) continue;
        const StrategyParamSpec* spec = paramSpecFor(selectedStrategy_, key);
        QVariantMap row;
        row.insert(QStringLiteral("key"), key);
        row.insert(QStringLiteral("label"), spec ? QString::fromUtf8(spec->label) : labelForParamKey(key));
        row.insert(QStringLiteral("description"), spec ? QString::fromUtf8(spec->description) : QStringLiteral("Runtime config parameter from trader ini."));
        row.insert(QStringLiteral("value"), paramValues_.value(key));
        out.push_back(row);
    }
    return out;
}

QVariantList BacktestViewModel::runs() const {
    QVariantList out;
    for (const auto& record : records_) {
        QVariantMap row;
        row.insert(QStringLiteral("runId"), record.runId);
        row.insert(QStringLiteral("status"), record.status);
        row.insert(QStringLiteral("strategy"), record.strategy);
        row.insert(QStringLiteral("filePath"), record.filePath);
        row.insert(QStringLiteral("fileName"), record.fileName);
        row.insert(QStringLiteral("modifiedText"), QDateTime::fromMSecsSinceEpoch(record.modifiedMs).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
        row.insert(QStringLiteral("errorCount"), record.errorCount);
        row.insert(QStringLiteral("valid"), record.valid);
        out.push_back(row);
    }
    return out;
}

QString BacktestViewModel::selectedJson() const {
    const auto* record = selectedRecord_();
    return record == nullptr ? QString{} : record->rawJson;
}

QString BacktestViewModel::selectedSummaryJson() const {
    const auto* record = selectedRecord_();
    return record == nullptr ? QString{} : record->summaryJson;
}

QString BacktestViewModel::selectedErrorText() const {
    const auto* record = selectedRecord_();
    return record == nullptr ? QString{} : record->errorText;
}

bool BacktestViewModel::canRun() const {
    return !running_ && !selectedSessionPath().trimmed().isEmpty() && !selectedStrategy_.trimmed().isEmpty();
}

void BacktestViewModel::reloadSessions() {
    emit sessionsChanged();
    emit canRunChanged();
}

void BacktestViewModel::setSelectedSessionId(const QString& sessionId) {
    const QString next = sessionId.trimmed();
    if (selectedSessionId_ == next && manualSessionPath_.isEmpty()) return;
    selectedSessionId_ = next;
    manualSessionPath_.clear();
    symbolOverride_.clear();
    selectedRunId_.clear();
    emit selectedSessionChanged();
    emit symbolChanged();
    emit canRunChanged();
    refresh();
}

void BacktestViewModel::setSessionPath(const QString& sessionPath) {
    const QString normalized = normalizedPath_(sessionPath);
    if (selectedSessionPath() == normalized) return;
    manualSessionPath_ = normalized;
    selectedSessionId_ = sessionIdFromPath_(normalized);
    symbolOverride_.clear();
    selectedRunId_.clear();
    emit selectedSessionChanged();
    emit symbolChanged();
    emit canRunChanged();
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
    savePersistentConfig_();
    selectedStrategy_ = next;
    if (!strategyHasNatrMode(selectedStrategy_)) configMode_ = QStringLiteral("fixed");
    loadStrategyDefaults_();
    loadSavedParameterValues_();
    savePersistentConfig_();
    emit selectedStrategyChanged();
    emit configChanged();
    emit strategyParametersChanged();
    emit canRunChanged();
}

void BacktestViewModel::setConfigMode(const QString& mode) {
    QString next = normalizeConfigMode(mode);
    if (!strategyHasNatrMode(selectedStrategy_)) next = QStringLiteral("fixed");
    if (configMode_ == next) return;
    configMode_ = next;
    savePersistentConfig_();
    emit configChanged();
    emit strategyParametersChanged();
}

void BacktestViewModel::setStrategyParameter(const QString& key, const QString& value) {
    const QString normalizedKey = key.trimmed().toLower();
    if (normalizedKey.isEmpty()) return;
    const QString normalizedValue = value.trimmed();
    if (paramValues_.value(normalizedKey) == normalizedValue) return;
    if (!paramOrder_.contains(normalizedKey)) paramOrder_.push_back(normalizedKey);
    paramValues_.insert(normalizedKey, normalizedValue);
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
    setPingLatencyUs(value);
}

void BacktestViewModel::setPingLatencyUs(const QString& value) {
    const QString next = value.trimmed();
    if (pingLatencyUs_ == next) return;
    pingLatencyUs_ = next;
    savePersistentConfig_();
    emit latencyChanged();
}

void BacktestViewModel::setInitialBalanceUsdt(const QString& value) {
    const QString next = value.trimmed();
    if (initialBalanceUsdt_ == next) return;
    initialBalanceUsdt_ = next;
    savePersistentConfig_();
    emit accountingChanged();
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

void BacktestViewModel::setAmendLatencyUs(const QString& value) {
    setPingLatencyUs(value);
}

void BacktestViewModel::setCancelLatencyUs(const QString& value) {
    setPingLatencyUs(value);
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
    out << "[backtest]\\n";
    out << "order_latency_us=" << pingLatencyUs_ << "\\n";
    out << "amend_latency_us=" << pingLatencyUs_ << "\\n";
    out << "cancel_latency_us=" << pingLatencyUs_ << "\\n";
    out << "initial_balance_usdt=" << initialBalanceUsdt_ << "\\n";
    out << "maker_fee_bps=" << makerFeeBps_ << "\\n";
    out << "taker_fee_bps=" << takerFeeBps_ << "\\n";
    out << "config_mode=" << configMode_ << "\\n\\n";
    out << "[strategy]\\n";
    for (const QString& key : paramOrder_) {
        out << key << "=" << paramValues_.value(key) << "\\n";
    }
    setStatusText_(QStringLiteral("Profile saved"));
}

void BacktestViewModel::loadProfile() {
    const QString text = readTextFile(profilePath_());
    if (text.isEmpty()) return;
    const QString orderLatency = iniValue(text, QStringLiteral("backtest"), QStringLiteral("order_latency_us"));
    const QString initialBalance = iniValue(text, QStringLiteral("backtest"), QStringLiteral("initial_balance_usdt"));
    const QString makerFee = iniValue(text, QStringLiteral("backtest"), QStringLiteral("maker_fee_bps"));
    const QString takerFee = iniValue(text, QStringLiteral("backtest"), QStringLiteral("taker_fee_bps"));
    const QString mode = iniValue(text, QStringLiteral("backtest"), QStringLiteral("config_mode"));
    if (!orderLatency.isEmpty()) pingLatencyUs_ = orderLatency;
    if (!initialBalance.isEmpty()) initialBalanceUsdt_ = initialBalance;
    if (!makerFee.isEmpty()) makerFeeBps_ = makerFee;
    if (!takerFee.isEmpty()) takerFeeBps_ = takerFee;
    if (!mode.isEmpty()) configMode_ = strategyHasNatrMode(selectedStrategy_) ? normalizeConfigMode(mode) : QStringLiteral("fixed");
    for (const IniKeyValue& row : iniSectionValues(text, QStringLiteral("strategy"))) {
        const QString key = row.key;
        if (!paramOrder_.contains(key)) continue;
        const QString value = row.value;
        if (!value.isEmpty()) paramValues_.insert(key, value);
    }
    emit latencyChanged();
    emit accountingChanged();
    emit configChanged();
    emit strategyParametersChanged();
}

void BacktestViewModel::refresh() {
    updateWatcher_();
    std::vector<RunRecord> next;
    const QString dirPath = backtestsDirectory();
    QStringList filesToWatch;
    if (!dirPath.isEmpty()) {
        QDir dir(dirPath);
        const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Time | QDir::Name);
        next.reserve(static_cast<std::size_t>(files.size()));
        for (const QFileInfo& file : files) {
            filesToWatch.push_back(file.absoluteFilePath());
            next.push_back(loadRecord_(file.absoluteFilePath()));
        }
    }

    std::sort(next.begin(), next.end(), [](const RunRecord& lhs, const RunRecord& rhs) {
        if (lhs.modifiedMs != rhs.modifiedMs) return lhs.modifiedMs > rhs.modifiedMs;
        return lhs.fileName < rhs.fileName;
    });
    records_ = std::move(next);

    if (!selectedRunId_.isEmpty() && selectedRecord_() == nullptr) selectedRunId_.clear();
    if (selectedRunId_.isEmpty() && !records_.empty()) selectedRunId_ = records_.front().runId;

    if (!running_) {
        if (selectedSessionPath().isEmpty()) setStatusText_(QStringLiteral("Select a session and strategy"));
        else setStatusText_(QStringLiteral("Watching %1 result%2").arg(static_cast<qulonglong>(records_.size())).arg(records_.size() == 1u ? QString{} : QStringLiteral("s")));
    }
    if (!filesToWatch.empty()) (void)watcher_.addPaths(filesToWatch);
    emit runsChanged();
    emit selectionChanged();
}

void BacktestViewModel::selectRun(const QString& runId) {
    if (selectedRunId_ == runId) return;
    selectedRunId_ = runId;
    emit selectionChanged();
}

void BacktestViewModel::startBacktest() {
    if (!canRun()) return;
    stopWorker_();
    cancelRequested_.store(false, std::memory_order_release);
    setRunning_(true);
    setProgress_(0, QStringLiteral("Starting"));
    setStatusText_(QStringLiteral("Backtest running"));

    const QString sessionPath = selectedSessionPath();
    const QString strategy = selectedStrategy_;
    const QString runId = runId_();
    const QString configPath = writeRunConfig_(runId);
    if (configPath.isEmpty()) {
        setRunning_(false);
        setStatusText_(QStringLiteral("Failed to write backtest config"));
        return;
    }
    const quint64 pingLatency = latencyValue_(pingLatencyUs_, 1000);
    const quint64 orderLatency = pingLatency;
    const quint64 amendLatency = pingLatency;
    const quint64 cancelLatency = pingLatency;
    const qint64 initialBalance = decimalE8Value_(initialBalanceUsdt_, 0);
    const qint64 makerFee = decimalE8Value_(makerFeeBps_, 0);
    const qint64 takerFee = decimalE8Value_(takerFeeBps_, 0);
    worker_ = std::thread([this, sessionPath, strategy, runId, configPath, orderLatency, amendLatency, cancelLatency, initialBalance, makerFee, takerFee] {
        hft_backtest::BacktestRunRequest request{};
        request.sessionPath = sessionPath.toStdString();
        request.configPath = configPath.toStdString();
        request.strategy = strategy.toStdString();
        request.runId = runId.toStdString();
        request.requestId = runId.toStdString();
        request.orderLatencyUs = orderLatency;
        request.amendLatencyUs = amendLatency;
        request.cancelLatencyUs = cancelLatency;
        request.initialBalanceE8 = initialBalance;
        request.makerFeeBpsE8 = makerFee;
        request.takerFeeBpsE8 = takerFee;

        const auto result = hft_backtest::runBacktest(request, progressCallback, this);
        const QString status = statusTextFor(result.status);
        const QString selected = QString::fromStdString(result.runId);
        QMetaObject::invokeMethod(this, [this, status, selected] {
            setRunning_(false);
            setProgress_(100, status);
            setStatusText_(status);
            refresh();
            if (!selected.isEmpty()) selectRun(selected);
        }, Qt::QueuedConnection);
    });
}

void BacktestViewModel::cancelBacktest() {
    cancelRequested_.store(true, std::memory_order_release);
    if (running_) setStatusText_(QStringLiteral("Cancelling backtest"));
}

void BacktestViewModel::applyWorkerProgress(int percent, const QString& text) {
    setProgress_(percent, text);
}

BacktestViewModel::RunRecord BacktestViewModel::loadRecord_(const QString& filePath) {
    RunRecord record;
    const QFileInfo info(filePath);
    record.filePath = info.absoluteFilePath();
    record.fileName = info.fileName();
    record.runId = info.completeBaseName();
    record.modifiedMs = info.lastModified().toMSecsSinceEpoch();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        record.status = QStringLiteral("unreadable");
        record.errorText = QStringLiteral("failed to open result file");
        return record;
    }
    const QByteArray raw = file.readAll();
    record.rawJson = QString::fromUtf8(raw);

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        record.status = QStringLiteral("invalid_json");
        record.errorText = parseError.errorString();
        return record;
    }

    const QJsonObject object = doc.object();
    record.valid = true;
    const QString runId = jsonValueString(object, QStringLiteral("run_id"));
    if (!runId.trimmed().isEmpty()) record.runId = runId.trimmed();
    record.status = jsonValueString(object, QStringLiteral("status"));
    if (record.status.trimmed().isEmpty()) record.status = QStringLiteral("unknown");
    record.strategy = jsonValueString(object, QStringLiteral("strategy"));
    record.summaryJson = prettyJson(object.value(QStringLiteral("summary")));
    record.errorCount = errorCount(object.value(QStringLiteral("errors")));
    record.rawJson = QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
    return record;
}

QString BacktestViewModel::normalizedPath_(const QString& path) {
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) return {};
    return QDir::cleanPath(QDir(trimmed).absolutePath());
}

QString BacktestViewModel::sessionIdFromPath_(const QString& path) {
    return QFileInfo(path).fileName();
}

const BacktestViewModel::RunRecord* BacktestViewModel::selectedRecord_() const noexcept {
    const auto it = std::find_if(records_.begin(), records_.end(), [this](const RunRecord& record) {
        return record.runId == selectedRunId_;
    });
    return it == records_.end() ? nullptr : &(*it);
}

void BacktestViewModel::updateWatcher_() {
    const QStringList watchedFiles = watcher_.files();
    if (!watchedFiles.empty()) (void)watcher_.removePaths(watchedFiles);
    const QStringList watchedDirs = watcher_.directories();
    if (!watchedDirs.empty()) (void)watcher_.removePaths(watchedDirs);

    const QString dirPath = backtestsDirectory();
    if (dirPath.isEmpty()) return;
    QDir sessionDir(selectedSessionPath());
    if (!sessionDir.exists()) return;
    sessionDir.mkpath(QStringLiteral("backtests"));
    if (QDir(dirPath).exists()) (void)watcher_.addPath(dirPath);
}

void BacktestViewModel::setStatusText_(const QString& statusText) {
    if (statusText_ == statusText) return;
    statusText_ = statusText;
    emit statusTextChanged();
}

void BacktestViewModel::setRunning_(bool running) {
    if (running_ == running) return;
    running_ = running;
    emit runningChanged();
    emit canRunChanged();
}

void BacktestViewModel::setProgress_(int percent, const QString& text) {
    const int clamped = std::max(0, std::min(100, percent));
    if (progressPercent_ == clamped && progressText_ == text) return;
    progressPercent_ = clamped;
    progressText_ = text;
    emit progressChanged();
}

void BacktestViewModel::stopWorker_() {
    if (worker_.joinable()) worker_.join();
}

QString BacktestViewModel::runId_() const {
    return QStringLiteral("run-%1").arg(QDateTime::currentMSecsSinceEpoch());
}

void BacktestViewModel::loadStrategyDefaults_() {
    paramValues_.clear();
    paramOrder_.clear();
    const QString text = readTextFile(configFileForStrategy(selectedStrategy_));
    for (const IniKeyValue& row : iniSectionValues(text, QStringLiteral("strategy"))) {
        if (row.key == QStringLiteral("type") || row.key == QStringLiteral("enabled") || row.key == QStringLiteral("inputs")) continue;
        paramOrder_.push_back(row.key);
        paramValues_.insert(row.key, row.value);
    }
    for (const NatrParamPair& pair : kNatrPairs) {
        if (selectedStrategy_ != QString::fromUtf8(pair.strategy)) continue;
        const QString fixedKey = QString::fromUtf8(pair.fixedKey);
        const QString natrKey = QString::fromUtf8(pair.natrKey);
        if (!paramOrder_.contains(fixedKey)) paramOrder_.push_back(fixedKey);
        if (!paramOrder_.contains(natrKey)) paramOrder_.push_back(natrKey);
        if (!paramValues_.contains(fixedKey)) paramValues_.insert(fixedKey, fallbackValueForKey(selectedStrategy_, fixedKey));
        if (!paramValues_.contains(natrKey)) paramValues_.insert(natrKey, fallbackValueForKey(selectedStrategy_, natrKey));
    }
    if (strategyHasNatrMode(selectedStrategy_) && !paramOrder_.contains(QStringLiteral("natr_ema_period_seconds"))) {
        paramOrder_.push_back(QStringLiteral("natr_ema_period_seconds"));
    }
    if (strategyHasNatrMode(selectedStrategy_) && !paramValues_.contains(QStringLiteral("natr_ema_period_seconds"))) {
        paramValues_.insert(QStringLiteral("natr_ema_period_seconds"), fallbackValueForKey(selectedStrategy_, QStringLiteral("natr_ema_period_seconds")));
    }
}

void BacktestViewModel::loadPersistentConfig_() {
    const QString strategy = settings_.value(QStringLiteral("backtests/selected_strategy"), selectedStrategy_).toString().trimmed();
    if (!strategy.isEmpty()) selectedStrategy_ = strategy;
    configMode_ = normalizeConfigMode(settings_.value(QStringLiteral("backtests/config_mode/%1").arg(selectedStrategy_), configMode_).toString());
    if (!strategyHasNatrMode(selectedStrategy_)) configMode_ = QStringLiteral("fixed");
    pingLatencyUs_ = settings_.value(QStringLiteral("backtests/ping_latency_us"), pingLatencyUs_).toString().trimmed();
    if (pingLatencyUs_.isEmpty()) pingLatencyUs_ = QStringLiteral("1000");
    initialBalanceUsdt_ = settings_.value(QStringLiteral("backtests/initial_balance_usdt"), initialBalanceUsdt_).toString().trimmed();
    if (initialBalanceUsdt_.isEmpty()) initialBalanceUsdt_ = QStringLiteral("1000");
    makerFeeBps_ = settings_.value(QStringLiteral("backtests/maker_fee_bps"), makerFeeBps_).toString().trimmed();
    if (makerFeeBps_.isEmpty()) makerFeeBps_ = QStringLiteral("0");
    takerFeeBps_ = settings_.value(QStringLiteral("backtests/taker_fee_bps"), takerFeeBps_).toString().trimmed();
    if (takerFeeBps_.isEmpty()) takerFeeBps_ = QStringLiteral("0");
    loadStrategyDefaults_();
    loadSavedParameterValues_();
}

void BacktestViewModel::loadSavedParameterValues_() {
    settings_.beginGroup(QStringLiteral("backtests/params/%1").arg(selectedStrategy_));
    for (const QString& key : paramOrder_) {
        const QString value = settings_.value(key, paramValues_.value(key)).toString().trimmed();
        if (!value.isEmpty()) paramValues_.insert(key, value);
    }
    settings_.endGroup();
}

void BacktestViewModel::savePersistentConfig_() {
    settings_.setValue(QStringLiteral("backtests/selected_strategy"), selectedStrategy_);
    settings_.setValue(QStringLiteral("backtests/config_mode/%1").arg(selectedStrategy_), configMode_);
    settings_.setValue(QStringLiteral("backtests/ping_latency_us"), pingLatencyUs_);
    settings_.setValue(QStringLiteral("backtests/initial_balance_usdt"), initialBalanceUsdt_);
    settings_.setValue(QStringLiteral("backtests/maker_fee_bps"), makerFeeBps_);
    settings_.setValue(QStringLiteral("backtests/taker_fee_bps"), takerFeeBps_);
    settings_.beginGroup(QStringLiteral("backtests/params/%1").arg(selectedStrategy_));
    for (const QString& key : paramOrder_) settings_.setValue(key, paramValues_.value(key));
    settings_.endGroup();
    settings_.sync();
}

QString BacktestViewModel::profilePath_() const {
    return QDir(recordingsRoot()).absoluteFilePath(QStringLiteral("backtest_profiles/%1/%2.ini").arg(selectedStrategy_, cleanProfileName(profileName_)));
}

QString BacktestViewModel::writeRunConfig_(const QString& runId) {
    const QString base = readTextFile(configFileForStrategy(selectedStrategy_));
    if (base.isEmpty()) return {};
    const QString session = selectedSessionPath();
    const QString exchange = manifestValue(session, QStringLiteral("exchange"));
    const QString market = manifestValue(session, QStringLiteral("market"));
    const QString symbol = selectedSymbol();
    const QString venue = venueSectionFor(exchange, market);
    QString apiSlot = iniValue(base, QStringLiteral("venue.%1").arg(venue), QStringLiteral("api_slot"));
    if (apiSlot.isEmpty()) apiSlot = QStringLiteral("1");
    QDir outDir(QDir(session).absoluteFilePath(QStringLiteral("backtests")));
    outDir.mkpath(QStringLiteral("."));
    const QString path = outDir.absoluteFilePath(runId + QStringLiteral(".ini"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) return {};
    QTextStream out(&file);
    const QString filteredBase = filteredBaseConfig(base, selectedStrategy_);
    out << filteredBase;
    if (!filteredBase.endsWith(QLatin1Char('\n'))) out << "\n";
    out << "\n# recorder backtest overrides\n";
    out << "[strategy]\n";
    out << "type=" << selectedStrategy_ << "\n";
    for (const QString& key : paramOrder_) {
        if (!keyVisibleForConfigMode(selectedStrategy_, key, configMode_)) continue;
        const QString value = paramValues_.value(key).trimmed();
        if (!value.isEmpty()) out << key << "=" << value << "\n";
    }
    out << "\n[venue." << venue << "]\n";
    out << "api_slot=" << apiSlot << "\n";
    if (!symbol.isEmpty()) out << "symbols=" << symbol << "\n";
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
