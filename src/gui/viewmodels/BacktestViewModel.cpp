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
#include <QRegularExpression>
#include <QTextStream>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <limits>

#include "hft_backtest/backtest.hpp"
#include "core/common/Status.hpp"

namespace hftrec::gui {
namespace {

constexpr qsizetype kMaxDisplayEquityPoints = 4096;

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
};

constexpr NatrParamPair kNatrPairs[] = {
    {"spread_maker1and2", "distance_bps", "distance_natr_pct"},
    {"spread_maker1and2", "trigger_bps", "trigger_natr_pct"},
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

QJsonObject humanSummaryObject(const QJsonObject& object);

bool isE8Key(const QString& key) {
    return key.size() > 3 && key.at(key.size() - 3) == QLatin1Char('_') && key.at(key.size() - 2) == QLatin1Char('e') && key.at(key.size() - 1) == QLatin1Char('8');
}

QString prettyJson(const QJsonValue& value) {
    if (value.isUndefined()) return {};
    QJsonDocument doc;
    if (value.isObject()) doc = QJsonDocument(value.toObject());
    else if (value.isArray()) doc = QJsonDocument(value.toArray());
    else return QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("value"), value}}).toJson(QJsonDocument::Indented));
    return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
}

QString e8DisplayString(qint64 value) {
    const bool negative = value < 0;
    const quint64 magnitude = negative ? static_cast<quint64>(-(value + 1)) + 1U : static_cast<quint64>(value);
    const quint64 whole = magnitude / 100000000U;
    quint64 fractional = magnitude % 100000000U;
    QString out = QString::number(static_cast<qulonglong>(whole));
    if (fractional != 0U) {
        QString fraction = QString::number(static_cast<qulonglong>(fractional)).rightJustified(8, QLatin1Char('0'));
        while (fraction.endsWith(QLatin1Char('0'))) fraction.chop(1);
        out += QLatin1Char('.');
        out += fraction;
    }
    return negative ? QString(QLatin1Char('-')) + out : out;
}

QJsonValue humanSummaryValue(const QString& key, const QJsonValue& value);

QJsonObject humanSummaryObject(const QJsonObject& object) {
    QJsonObject out;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        const QString key = it.key();
        const QString displayKey = isE8Key(key) ? key.left(key.size() - 3) : key;
        out.insert(displayKey, humanSummaryValue(key, it.value()));
    }
    return out;
}

QJsonArray humanSummaryArray(const QJsonArray& array) {
    QJsonArray out;
    for (const QJsonValue& value : array) out.push_back(humanSummaryValue(QString{}, value));
    return out;
}

QJsonValue humanSummaryValue(const QString& key, const QJsonValue& value) {
    if (isE8Key(key) && value.isDouble()) return e8DisplayString(value.toInteger());
    if (value.isObject()) return humanSummaryObject(value.toObject());
    if (value.isArray()) return humanSummaryArray(value.toArray());
    return value;
}

QString humanSummaryJson(const QJsonValue& value) {
    if (!value.isObject()) return prettyJson(value);
    return prettyJson(humanSummaryObject(value.toObject()));
}

int errorCount(const QJsonValue& value) {
    return value.isArray() ? value.toArray().size() : 0;
}

QString metricDisplayValue(const QString& key, const QJsonValue& value) {
    if (isE8Key(key) && value.isDouble()) return e8DisplayString(value.toInteger());
    if (value.isBool()) return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (value.isDouble()) return QString::number(value.toInteger());
    if (value.isString()) return value.toString();
    return prettyJson(value).trimmed();
}

QString metricLabel(QString key) {
    if (key == QStringLiteral("wallet_balance_e8")) return QStringLiteral("Final balance");
    if (isE8Key(key)) key = key.left(key.size() - 3);
    key.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!key.isEmpty()) key[0] = key[0].toUpper();
    return key;
}

QVariantList resultMetrics(const QJsonObject& summary) {
    static constexpr const char* kKeys[] = {
        "initial_balance_e8",
        "wallet_balance_e8",
        "total_pnl_e8",
        "gross_realized_pnl_e8",
        "fees_paid_e8",
        "net_realized_pnl_e8",
        "realized_pnl_e8",
        "unrealized_pnl_e8",
        "fills",
        "reduce_only_orders",
        "reduce_only_fills",
        "strategy_closed",
        "open_position_qty_e8",
    };
    QVariantList out;
    for (const char* rawKey : kKeys) {
        const QString key = QString::fromUtf8(rawKey);
        if (!summary.contains(key)) continue;
        QVariantMap row;
        row.insert(QStringLiteral("key"), key);
        row.insert(QStringLiteral("label"), metricLabel(key));
        row.insert(QStringLiteral("value"), metricDisplayValue(key, summary.value(key)));
        out.push_back(row);
    }
    return out;
}

QVariantList equityPoints(const QJsonArray& points, const QJsonObject& summary, qint64& minPnl, qint64& maxPnl) {
    QVariantList out;
    bool hasPoint = false;
    qint64 lastTs = 0;
    auto appendRow = [&](qint64 ts,
                         qint64 grossRealized,
                         qint64 realized,
                         qint64 unrealized,
                         qint64 total,
                         qint64 grossTotal,
                         qint64 netTotal,
                         qint64 fees,
                         qint64 wallet,
                         qint64 position,
                         qint64 fillCount) {
        const qint64 rowMin = std::min(std::min(grossTotal, netTotal), fees);
        const qint64 rowMax = std::max(std::max(grossTotal, netTotal), fees);
        if (!hasPoint) {
            minPnl = rowMin;
            maxPnl = rowMax;
            hasPoint = true;
        } else {
            minPnl = std::min(minPnl, rowMin);
            maxPnl = std::max(maxPnl, rowMax);
        }
        QVariantMap row;
        row.insert(QStringLiteral("index"), out.size());
        row.insert(QStringLiteral("tsNs"), ts);
        row.insert(QStringLiteral("grossRealizedPnlE8"), grossRealized);
        row.insert(QStringLiteral("realizedPnlE8"), realized);
        row.insert(QStringLiteral("unrealizedPnlE8"), unrealized);
        row.insert(QStringLiteral("grossTotalPnlE8"), grossTotal);
        row.insert(QStringLiteral("netTotalPnlE8"), netTotal);
        row.insert(QStringLiteral("totalPnlE8"), total);
        row.insert(QStringLiteral("feesPaidE8"), fees);
        row.insert(QStringLiteral("walletBalanceE8"), wallet);
        row.insert(QStringLiteral("positionQtyE8"), position);
        row.insert(QStringLiteral("fillCount"), fillCount);
        row.insert(QStringLiteral("grossTotalPnl"), e8DisplayString(grossTotal));
        row.insert(QStringLiteral("netTotalPnl"), e8DisplayString(netTotal));
        row.insert(QStringLiteral("feesPaid"), e8DisplayString(fees));
        row.insert(QStringLiteral("totalPnl"), e8DisplayString(total));
        out.push_back(row);
        lastTs = ts;
    };
    for (const QJsonValue& value : points) {
        if (!value.isObject()) continue;
        const QJsonObject object = value.toObject();
        const qint64 ts = object.value(QStringLiteral("ts_ns")).toInteger();
        const qint64 realized = object.value(QStringLiteral("realized_pnl_e8")).toInteger();
        const qint64 grossRealized = object.value(QStringLiteral("gross_realized_pnl_e8")).toInteger(realized);
        const qint64 unrealized = object.value(QStringLiteral("unrealized_pnl_e8")).toInteger();
        const qint64 total = object.value(QStringLiteral("total_pnl_e8")).toInteger(realized + unrealized);
        const qint64 grossTotal = object.value(QStringLiteral("gross_total_pnl_e8")).toInteger(grossRealized + unrealized);
        const qint64 netTotal = object.value(QStringLiteral("net_total_pnl_e8")).toInteger(total);
        const qint64 fees = object.value(QStringLiteral("fees_paid_e8")).toInteger();
        const qint64 wallet = object.value(QStringLiteral("wallet_balance_e8")).toInteger();
        const qint64 position = object.value(QStringLiteral("position_qty_e8")).toInteger();
        const qint64 fillCount = object.value(QStringLiteral("fill_count")).toInteger();
        appendRow(ts, grossRealized, realized, unrealized, total, grossTotal, netTotal, fees, wallet, position, fillCount);
    }
    if (!summary.isEmpty() && summary.contains(QStringLiteral("total_pnl_e8"))) {
        const qint64 realized = summary.value(QStringLiteral("net_realized_pnl_e8")).toInteger(summary.value(QStringLiteral("realized_pnl_e8")).toInteger());
        const qint64 grossRealized = summary.value(QStringLiteral("gross_realized_pnl_e8")).toInteger(realized);
        const qint64 unrealized = summary.value(QStringLiteral("unrealized_pnl_e8")).toInteger();
        const qint64 total = summary.value(QStringLiteral("total_pnl_e8")).toInteger(realized + unrealized);
        const qint64 grossTotal = summary.value(QStringLiteral("gross_total_pnl_e8")).toInteger(grossRealized + unrealized);
        const qint64 netTotal = summary.value(QStringLiteral("net_total_pnl_e8")).toInteger(total);
        const qint64 fees = summary.value(QStringLiteral("fees_paid_e8")).toInteger();
        const qint64 wallet = summary.value(QStringLiteral("wallet_balance_e8")).toInteger();
        const qint64 position = summary.value(QStringLiteral("open_position_qty_e8")).toInteger(summary.value(QStringLiteral("net_qty_e8")).toInteger());
        const qint64 fillCount = summary.value(QStringLiteral("fills")).toInteger();
        bool appendSummary = out.empty();
        if (!appendSummary) {
            const QVariantMap last = out.constLast().toMap();
            appendSummary = last.value(QStringLiteral("grossTotalPnlE8")).toLongLong() != grossTotal ||
                            last.value(QStringLiteral("netTotalPnlE8")).toLongLong() != netTotal ||
                            last.value(QStringLiteral("feesPaidE8")).toLongLong() != fees;
        }
        if (appendSummary) appendRow(lastTs + 1, grossRealized, realized, unrealized, total, grossTotal, netTotal, fees, wallet, position, fillCount);
    }
    if (!hasPoint) {
        minPnl = 0;
        maxPnl = 0;
    }
    return out;
}

bool parseEquityLine(const QByteArray& line, std::array<qint64, 11>& out) {
    const QByteArray trimmed = line.trimmed();
    if (trimmed.size() < 2 || !trimmed.startsWith('[') || !trimmed.endsWith(']')) return false;
    const QList<QByteArray> parts = trimmed.mid(1, trimmed.size() - 2).split(',');
    if (parts.size() != static_cast<int>(out.size())) return false;
    for (qsizetype i = 0; i < static_cast<qsizetype>(out.size()); ++i) {
        bool ok = false;
        out[static_cast<std::size_t>(i)] = parts.at(i).toLongLong(&ok);
        if (!ok) return false;
    }
    return true;
}

QVariantList equityPointsFromJsonl(const QString& path, const QJsonObject& summary, qint64 totalRows, qint64& minPnl, qint64& maxPnl) {
    QJsonArray sampled;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return equityPoints(sampled, summary, minPnl, maxPnl);

    const qint64 stride = totalRows > kMaxDisplayEquityPoints ? ((totalRows + kMaxDisplayEquityPoints - 1) / kMaxDisplayEquityPoints) : 1;
    qint64 index = 0;
    QJsonObject lastObject;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine();
        if (line.trimmed().isEmpty()) continue;
        std::array<qint64, 11> row{};
        if (!parseEquityLine(line, row)) continue;
        QJsonObject object{
            {QStringLiteral("ts_ns"), row[0]},
            {QStringLiteral("gross_realized_pnl_e8"), row[1]},
            {QStringLiteral("realized_pnl_e8"), row[2]},
            {QStringLiteral("unrealized_pnl_e8"), row[3]},
            {QStringLiteral("gross_total_pnl_e8"), row[4]},
            {QStringLiteral("net_total_pnl_e8"), row[5]},
            {QStringLiteral("total_pnl_e8"), row[6]},
            {QStringLiteral("fees_paid_e8"), row[7]},
            {QStringLiteral("wallet_balance_e8"), row[8]},
            {QStringLiteral("position_qty_e8"), row[9]},
            {QStringLiteral("fill_count"), row[10]},
        };
        lastObject = object;
        if (index == 0 || stride <= 1 || (index % stride) == 0) sampled.push_back(object);
        ++index;
    }
    if (!lastObject.isEmpty() && (sampled.isEmpty() || sampled.last().toObject().value(QStringLiteral("ts_ns")).toInteger() != lastObject.value(QStringLiteral("ts_ns")).toInteger())) {
        sampled.push_back(lastObject);
    }
    return equityPoints(sampled, summary, minPnl, maxPnl);
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
    if (id == QStringLiteral("backtest_probe")) file = QStringLiteral("probe.ini");
    return QDir(root).absoluteFilePath(file);
}

QStringList discoverStrategyIds() {
    QStringList out;
    const QDir strategyRoot(QDir(QStringLiteral(HFT_BACKTEST_TRADER_SOURCE_DIR)).absoluteFilePath(QStringLiteral("strategy")));
    const QStringList dirs = strategyRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& dirName : dirs) {
        const QString headerPath = strategyRoot.absoluteFilePath(QStringLiteral("%1/Strategy.hpp").arg(dirName));
        QFile header(headerPath);
        if (!header.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        const QString text = QString::fromUtf8(header.readAll());
        const QRegularExpression descriptorPattern(QStringLiteral("\\b%1StrategyDescriptor\\s*\\(")
                                                        .arg(QRegularExpression::escape(dirName)));
        if (text.contains(descriptorPattern)) out.push_back(dirName);
    }
    return out;
}

bool isDiscoveredStrategy(const QString& strategy) {
    return discoverStrategyIds().contains(strategy.trimmed());
}

QString firstDiscoveredStrategy() {
    const QStringList ids = discoverStrategyIds();
    return ids.empty() ? QString{} : ids.front();
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
    QVariantList out;
    for (const QString& strategy : discoverStrategyIds()) {
        QVariantMap row;
        row.insert(QStringLiteral("id"), strategy);
        row.insert(QStringLiteral("label"), strategy);
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
        row.insert(QStringLiteral("id"), record.runId);
        row.insert(QStringLiteral("label"), record.displayName.isEmpty() ? record.runId : record.displayName);
        row.insert(QStringLiteral("configText"), record.configText);
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

QVariantList BacktestViewModel::selectedEquityPoints() const {
    const auto* record = selectedRecord_();
    return record == nullptr ? QVariantList{} : record->equityPoints;
}

QVariantList BacktestViewModel::selectedResultMetrics() const {
    const auto* record = selectedRecord_();
    return record == nullptr ? QVariantList{} : record->resultMetrics;
}

bool BacktestViewModel::hasEquityPoints() const {
    const auto* record = selectedRecord_();
    return record != nullptr && !record->equityPoints.empty();
}

qint64 BacktestViewModel::selectedInitialBalanceE8() const {
    const auto* record = selectedRecord_();
    return record == nullptr ? 0 : record->initialBalanceE8;
}

qint64 BacktestViewModel::selectedPnlMinE8() const {
    const auto* record = selectedRecord_();
    return record == nullptr ? 0 : record->pnlMinE8;
}

qint64 BacktestViewModel::selectedPnlMaxE8() const {
    const auto* record = selectedRecord_();
    return record == nullptr ? 0 : record->pnlMaxE8;
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
    if (!isDiscoveredStrategy(next)) return;
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
        const QFileInfoList dirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time | QDir::Name);
        next.reserve(static_cast<std::size_t>(dirs.size()));
        for (const QFileInfo& runDir : dirs) {
            const QString manifestPath = QDir(runDir.absoluteFilePath()).absoluteFilePath(QStringLiteral("manifest.json"));
            if (!QFileInfo::exists(manifestPath)) continue;
            filesToWatch.push_back(manifestPath);
            filesToWatch.push_back(QDir(runDir.absoluteFilePath()).absoluteFilePath(QStringLiteral("equity.jsonl")));
            next.push_back(loadRecord_(runDir.absoluteFilePath()));
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
        request.outputPath = (QDir(sessionPath).absoluteFilePath(QStringLiteral("backtests/%1").arg(runId))).toStdString();

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
    const QDir runDir(info.absoluteFilePath());
    const QString manifestPath = runDir.absoluteFilePath(QStringLiteral("manifest.json"));
    record.filePath = info.absoluteFilePath();
    record.fileName = info.fileName();
    record.runId = info.fileName();
    record.modifiedMs = info.lastModified().toMSecsSinceEpoch();

    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        record.status = QStringLiteral("unreadable");
        record.errorText = QStringLiteral("failed to open result manifest");
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
    const QString configPath = jsonValueString(object, QStringLiteral("config_path"));
    if (!configPath.trimmed().isEmpty()) {
        const QFileInfo configInfo(configPath);
        const QString resolvedConfigPath = configInfo.isRelative() ? runDir.absoluteFilePath(configPath) : configPath;
        const QString configText = readTextFile(resolvedConfigPath);
        record.displayName = metadataCommentValue(configText, QStringLiteral("display_name"));
        record.configText = metadataCommentValue(configText, QStringLiteral("config_summary"));
    }
    if (record.displayName.isEmpty()) record.displayName = jsonValueString(object, QStringLiteral("display_name"));
    if (record.configText.isEmpty()) record.configText = jsonValueString(object, QStringLiteral("config_summary"));
    const QJsonObject summary = object.value(QStringLiteral("summary")).toObject();
    record.initialBalanceE8 = summary.value(QStringLiteral("initial_balance_e8")).toInteger();
    record.summaryJson = humanSummaryJson(object.value(QStringLiteral("summary")));
    record.resultMetrics = resultMetrics(summary);
    const QJsonObject streams = object.value(QStringLiteral("streams")).toObject();
    const QJsonObject equityStream = streams.value(QStringLiteral("equity")).toObject();
    const qint64 equityRows = equityStream.value(QStringLiteral("rows")).toInteger();
    record.equityPoints = equityPointsFromJsonl(runDir.absoluteFilePath(QStringLiteral("equity.jsonl")), summary, equityRows, record.pnlMinE8, record.pnlMaxE8);
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
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    return QStringLiteral("%1-%2-%3-%4")
        .arg(cleanRunSlugPart(selectedStrategy_), cleanRunSlugPart(selectedSymbol()), cleanRunSlugPart(configMode_), stamp);
}

QString BacktestViewModel::displayName_() const {
    return QStringLiteral("%1 %2 %3")
        .arg(selectedStrategy_.trimmed(), selectedSymbol().trimmed(), configMode_.trimmed())
        .simplified();
}

QString BacktestViewModel::configSummary_() const {
    QStringList parts;
    for (const QString& key : paramOrder_) {
        if (!keyVisibleForConfigMode(selectedStrategy_, key, configMode_)) continue;
        const QString value = paramValues_.value(key).trimmed();
        if (!value.isEmpty()) parts.push_back(QStringLiteral("%1=%2").arg(key, value));
        if (parts.size() >= 3) break;
    }
    QString summary = configMode_.trimmed();
    if (!parts.empty()) summary += QStringLiteral(": ") + parts.join(QStringLiteral(", "));
    return summary;
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
    const QString defaultStrategy = selectedStrategy_;
    const QString strategy = settings_.value(QStringLiteral("backtests/selected_strategy"), selectedStrategy_).toString().trimmed();
    if (!strategy.isEmpty()) selectedStrategy_ = strategy;
    if (!isDiscoveredStrategy(selectedStrategy_)) {
        const QString fallback = isDiscoveredStrategy(defaultStrategy) ? defaultStrategy : firstDiscoveredStrategy();
        if (!fallback.isEmpty()) selectedStrategy_ = fallback;
    }
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
    outDir.mkpath(runId);
    const QString path = QDir(outDir.absoluteFilePath(runId)).absoluteFilePath(QStringLiteral("config.ini"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) return {};
    QTextStream out(&file);
    out << "# recorder backtest metadata\n";
    out << "# display_name=" << displayName_() << "\n";
    out << "# config_summary=" << configSummary_() << "\n\n";
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
