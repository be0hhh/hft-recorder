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
#include <array>
#include <limits>
#include <string>
#include <utility>

#include "hft_backtest/backtest.hpp"
#include "hft_backtest/backtest_sweep.hpp"
#include "core/common/Status.hpp"

namespace hftrec::gui {
namespace {

constexpr qsizetype kMaxDisplayEquityPoints = 4096;

struct IniKeyValue {
    QString key;
    QString value;
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

QString pnlPercentText(qint64 pnlE8, qint64 initialBalanceE8) {
    if (initialBalanceE8 <= 0) return {};
    const qint64 bps = (pnlE8 * 10000) / initialBalanceE8;
    const QString sign = bps > 0 ? QStringLiteral("+") : (bps < 0 ? QStringLiteral("-") : QString{});
    const qint64 absBps = bps < 0 ? -bps : bps;
    return QStringLiteral("%1%2.%3%").arg(sign, QString::number(absBps / 100), QString::number(absBps % 100).rightJustified(2, QLatin1Char('0')));
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

QString sweepStatusTextFor(hft_backtest::Status status) {
    if (status == hft_backtest::Status::Ok) return QStringLiteral("Sweep complete");
    if (status == hft_backtest::Status::Cancelled) return QStringLiteral("Sweep cancelled");
    return QStringLiteral("Sweep failed: %1").arg(QString::fromUtf8(hft_backtest::statusToString(status).data(), static_cast<qsizetype>(hft_backtest::statusToString(status).size())));
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

bool isSweepKey(const QString& key) {
    return key == QStringLiteral("distance_bps") || key == QStringLiteral("trigger_bps") || key == QStringLiteral("close_delay_us");
}

QString defaultRangeMin(const QString& key, const QString& value) {
    if (key == QStringLiteral("distance_bps")) return QStringLiteral("10");
    if (key == QStringLiteral("trigger_bps")) return QStringLiteral("1");
    if (key == QStringLiteral("close_delay_us")) return QStringLiteral("0");
    return value;
}

QString defaultRangeMax(const QString& key, const QString& value) {
    if (key == QStringLiteral("distance_bps")) return QStringLiteral("1000");
    if (key == QStringLiteral("trigger_bps")) return QStringLiteral("100");
    if (key == QStringLiteral("close_delay_us")) return QStringLiteral("10000000");
    return value;
}

QString defaultRangeStep(const QString& key) {
    if (key == QStringLiteral("distance_bps")) return QStringLiteral("10");
    if (key == QStringLiteral("trigger_bps")) return QStringLiteral("1");
    if (key == QStringLiteral("close_delay_us")) return QStringLiteral("500000");
    return QStringLiteral("1");
}

QVariantList sweepParamModeChoices() {
    QVariantList out;
    for (const QString& id : {QStringLiteral("fixed"), QStringLiteral("sweep")}) {
        QVariantMap row;
        row.insert(QStringLiteral("id"), id);
        row.insert(QStringLiteral("label"), id.at(0).toUpper() + id.mid(1));
        out.push_back(row);
    }
    return out;
}

QVariantList sweepCurveLimitChoiceRows() {
    QVariantList out;
    for (const QString& id : {QStringLiteral("16"), QStringLiteral("32"), QStringLiteral("64"), QStringLiteral("all")}) {
        QVariantMap row;
        row.insert(QStringLiteral("id"), id);
        row.insert(QStringLiteral("label"), id == QStringLiteral("all") ? QStringLiteral("All") : QStringLiteral("Top %1").arg(id));
        out.push_back(row);
    }
    return out;
}

qint64 jsonIntValue(const QJsonObject& object, const QString& key) {
    return object.value(key).toInteger();
}

QVariantMap sweepRowMap(const QJsonObject& row, const QString& metricKey) {
    QVariantMap out;
    const QJsonObject params = row.value(QStringLiteral("params")).toObject();
    QStringList parts;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) parts.push_back(QStringLiteral("%1=%2").arg(it.key(), QString::number(it.value().toInteger())));
    out.insert(QStringLiteral("pointId"), jsonIntValue(row, QStringLiteral("point_id")));
    out.insert(QStringLiteral("label"), parts.join(QStringLiteral(", ")));
    out.insert(QStringLiteral("params"), params.toVariantMap());
    out.insert(QStringLiteral("metricKey"), metricKey);
    out.insert(QStringLiteral("metricRaw"), jsonIntValue(row, metricKey));
    out.insert(QStringLiteral("metricText"), isE8Key(metricKey) ? e8DisplayString(jsonIntValue(row, metricKey)) : QString::number(jsonIntValue(row, metricKey)));
    out.insert(QStringLiteral("totalPnlE8"), jsonIntValue(row, QStringLiteral("total_pnl_e8")));
    out.insert(QStringLiteral("status"), row.value(QStringLiteral("status")).toString());
    return out;
}

QVariantMap sweepCurveMap(const QJsonObject& row) {
    QVariantMap out;
    const QJsonObject params = row.value(QStringLiteral("params")).toObject();
    QStringList parts;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) parts.push_back(QStringLiteral("%1=%2").arg(it.key(), QString::number(it.value().toInteger())));
    QVariantList curve;
    const QJsonArray values = row.value(QStringLiteral("curve_e8")).toArray();
    for (const QJsonValue& value : values) curve.push_back(value.toInteger());
    if (curve.empty()) curve.push_back(row.value(QStringLiteral("total_pnl_e8")).toInteger());
    out.insert(QStringLiteral("pointId"), jsonIntValue(row, QStringLiteral("point_id")));
    out.insert(QStringLiteral("label"), parts.join(QStringLiteral(", ")));
    out.insert(QStringLiteral("params"), params.toVariantMap());
    out.insert(QStringLiteral("status"), row.value(QStringLiteral("status")).toString());
    out.insert(QStringLiteral("initialBalanceE8"), jsonIntValue(row, QStringLiteral("initial_balance_e8")));
    out.insert(QStringLiteral("totalPnlE8"), jsonIntValue(row, QStringLiteral("total_pnl_e8")));
    out.insert(QStringLiteral("totalPnlText"), e8DisplayString(jsonIntValue(row, QStringLiteral("total_pnl_e8"))));
    out.insert(QStringLiteral("curve"), curve);
    return out;
}

QVariantList sweepCurvesFromJsonl(const QString& path) {
    QVariantList out;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.isEmpty()) continue;
        QJsonParseError error{};
        const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) continue;
        out.push_back(sweepCurveMap(doc.object()));
    }
    std::sort(out.begin(), out.end(), [](const QVariant& lhs, const QVariant& rhs) {
        return lhs.toMap().value(QStringLiteral("totalPnlE8")).toLongLong() > rhs.toMap().value(QStringLiteral("totalPnlE8")).toLongLong();
    });
    return out;
}

QVariantList sweepRowsFromJsonl(const QString& path, const QString& metricKey) {
    QVariantList out;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.isEmpty()) continue;
        QJsonParseError error{};
        const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) continue;
        out.push_back(sweepRowMap(doc.object(), metricKey));
    }
    std::sort(out.begin(), out.end(), [](const QVariant& lhs, const QVariant& rhs) {
        return lhs.toMap().value(QStringLiteral("metricRaw")).toLongLong() > rhs.toMap().value(QStringLiteral("metricRaw")).toLongLong();
    });
    return out;
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
            skipSection = section == QStringLiteral("strategy") || section.startsWith(QStringLiteral("venue."));
            if (skipSection) continue;
            stream << line << "\n";
            continue;
        }
        if (skipSection) continue;
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
        const QDir backtestsDir(QDir(path).absoluteFilePath(QStringLiteral("backtests")));
        int backtestCount = 0;
        if (backtestsDir.exists()) {
            const QFileInfoList runDirs = backtestsDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo& runDir : runDirs) {
                if (runDir.fileName() == QStringLiteral("sweeps")) continue;
                if (QFileInfo::exists(QDir(runDir.absoluteFilePath()).absoluteFilePath(QStringLiteral("manifest.json")))) ++backtestCount;
            }
            const QDir sweepsDir(backtestsDir.absoluteFilePath(QStringLiteral("sweeps")));
            const QFileInfoList sweepDirs = sweepsDir.exists() ? sweepsDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot) : QFileInfoList{};
            for (const QFileInfo& sweepDir : sweepDirs) {
                if (QFileInfo::exists(QDir(sweepDir.absoluteFilePath()).absoluteFilePath(QStringLiteral("manifest.json")))) ++backtestCount;
            }
        }
        QVariantMap row;
        row.insert(QStringLiteral("id"), entry);
        row.insert(QStringLiteral("label"), entry);
        row.insert(QStringLiteral("path"), path);
        row.insert(QStringLiteral("hasManifest"), QFileInfo::exists(QDir(path).absoluteFilePath(QStringLiteral("manifest.json"))));
        row.insert(QStringLiteral("hasBacktests"), backtestsDir.exists());
        row.insert(QStringLiteral("backtestCount"), backtestCount);
        row.insert(QStringLiteral("rightText"), backtestCount > 0 ? QString::number(backtestCount) : QString{});
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
QVariantList BacktestViewModel::strategyChoices() const {
    QVariantList out;
    const std::size_t count = hft_backtest::strategyMetadataCount();
    for (std::size_t i = 0; i < count; ++i) {
        const hft_backtest::StrategyMetadata* metadata = hft_backtest::strategyMetadataAt(i);
        if (metadata == nullptr || metadata->id == nullptr) continue;
        QVariantMap row;
        const QString id = qString(metadata->id);
        row.insert(QStringLiteral("id"), id);
        row.insert(QStringLiteral("label"), id);
        out.push_back(row);
    }
    return out;
}


QVariantList BacktestViewModel::indicatorProfileChoices() const {
    QVariantList out;
    const hft_backtest::StrategyMetadata* metadata = metadataForStrategy(selectedStrategy_);
    if (metadata == nullptr) return out;
    for (std::size_t i = 0; i < metadata->indicatorCount && i < hft_backtest::kStrategyMetadataMaxIndicators; ++i) {
        const hft_backtest::StrategyIndicatorMetadata& indicator = metadata->indicators[i];
        if (indicator.id == nullptr || indicator.id[0] == '\0') continue;
        out.push_back(indicatorChoice(indicator));
    }
    return out;
}
QVariantList BacktestViewModel::configModeChoices() const {
    QVariantList out;
    QVariantMap fixed;
    fixed.insert(QStringLiteral("id"), QStringLiteral("fixed"));
    fixed.insert(QStringLiteral("label"), QStringLiteral("Fixed bps"));
    out.push_back(fixed);
    return out;
}

QVariantList BacktestViewModel::sweepCurveLimitChoices() const {
    return sweepCurveLimitChoiceRows();
}

QVariantList BacktestViewModel::strategyParameters() const {
    QVariantList out;
    const hft_backtest::StrategyMetadata* metadata = metadataForStrategy(selectedStrategy_);
    if (metadata == nullptr) return out;
    std::vector<std::uint8_t> emittedGroups;
    for (const QString& key : paramOrder_) {
        const hft_backtest::StrategyParamMetadata* param = paramMetadataFor(selectedStrategy_, key);
        if (param == nullptr) continue;
        const std::uint8_t group = param->exclusiveGroup;
        if (group != 0u) {
            if (std::find(emittedGroups.begin(), emittedGroups.end(), group) == emittedGroups.end()) {
                emittedGroups.push_back(group);
                QVariantMap choiceRow;
                choiceRow.insert(QStringLiteral("key"), paramGroupKey(*metadata, group));
                choiceRow.insert(QStringLiteral("label"), paramGroupKey(*metadata, group));
                choiceRow.insert(QStringLiteral("value"), activeParamByGroup_.value(static_cast<int>(group)));
                choiceRow.insert(QStringLiteral("isChoice"), true);
                choiceRow.insert(QStringLiteral("group"), static_cast<int>(group));
                choiceRow.insert(QStringLiteral("choices"), paramGroupChoices(*metadata, group));
                out.push_back(choiceRow);
            }
            if (activeParamByGroup_.value(static_cast<int>(group)) != key) continue;
        }
        QVariantMap row;
        row.insert(QStringLiteral("key"), key);
        row.insert(QStringLiteral("label"), key);
        row.insert(QStringLiteral("description"), QStringLiteral("Strategy runtime parameter."));
        row.insert(QStringLiteral("value"), paramValues_.value(key));
        row.insert(QStringLiteral("mode"), paramModes_.value(key, QStringLiteral("fixed")));
        row.insert(QStringLiteral("min"), paramMinValues_.value(key));
        row.insert(QStringLiteral("max"), paramMaxValues_.value(key));
        row.insert(QStringLiteral("step"), paramStepValues_.value(key));
        row.insert(QStringLiteral("modeChoices"), sweepParamModeChoices());
        row.insert(QStringLiteral("isChoice"), false);
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
        row.insert(QStringLiteral("pnlText"), record.pnlText);
        row.insert(QStringLiteral("pnlPositive"), record.totalPnlE8 > 0);
        row.insert(QStringLiteral("pnlNegative"), record.totalPnlE8 < 0);
        row.insert(QStringLiteral("rightText"), record.pnlText);
        row.insert(QStringLiteral("filePath"), record.filePath);
        row.insert(QStringLiteral("fileName"), record.fileName);
        row.insert(QStringLiteral("modifiedText"), QDateTime::fromMSecsSinceEpoch(record.modifiedMs).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
        row.insert(QStringLiteral("errorCount"), record.errorCount);
        row.insert(QStringLiteral("valid"), record.valid);
        row.insert(QStringLiteral("sweep"), record.sweep);
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

QVariantList BacktestViewModel::selectedSweepRows() const {
    const auto* record = selectedRecord_();
    if (record == nullptr || !record->sweep) return {};
    QVariantList out;
    for (const QVariant& value : record->sweepRows) {
        QVariantMap row = value.toMap();
        const qint64 raw = row.value(QStringLiteral("totalPnlE8")).toLongLong();
        row.insert(QStringLiteral("metricKey"), QStringLiteral("total_pnl_e8"));
        row.insert(QStringLiteral("metricRaw"), raw);
        row.insert(QStringLiteral("metricText"), e8DisplayString(raw));
        out.push_back(row);
    }
    std::sort(out.begin(), out.end(), [](const QVariant& lhs, const QVariant& rhs) {
        return lhs.toMap().value(QStringLiteral("metricRaw")).toLongLong() > rhs.toMap().value(QStringLiteral("metricRaw")).toLongLong();
    });
    return out;
}

QVariantList BacktestViewModel::selectedSweepCurves() const {
    const auto* record = selectedRecord_();
    if (record == nullptr || !record->sweep) return {};
    const int limit = selectedSweepCurveLimit_ == QStringLiteral("all") ? record->sweepCurves.size() : selectedSweepCurveLimit_.toInt();
    QVariantList out;
    for (int i = 0; i < record->sweepCurves.size() && i < limit; ++i) out.push_back(record->sweepCurves.at(i));
    return out;
}

bool BacktestViewModel::selectedIsSweep() const {
    const auto* record = selectedRecord_();
    return record != nullptr && record->sweep;
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
    configMode_ = QStringLiteral("fixed");
    loadStrategyDefaults_();
    loadSavedParameterValues_();
    selectedIndicatorProfile_ = settings_.value(QStringLiteral("backtests/indicator_profile/%1").arg(selectedStrategy_), defaultIndicatorProfileForStrategy(selectedStrategy_)).toString().trimmed();
    if (!indicatorProfileAllowedForStrategy(selectedStrategy_, selectedIndicatorProfile_)) selectedIndicatorProfile_ = defaultIndicatorProfileForStrategy(selectedStrategy_);
    savePersistentConfig_();
    emit selectedStrategyChanged();
    emit indicatorProfileChanged();
    emit configChanged();
    emit strategyParametersChanged();
    emit canRunChanged();
}

void BacktestViewModel::setSelectedIndicatorProfile(const QString& profile) {
    QString next = profile.trimmed();
    if (!indicatorProfileAllowedForStrategy(selectedStrategy_, next)) next = defaultIndicatorProfileForStrategy(selectedStrategy_);
    if (selectedIndicatorProfile_ == next) return;
    selectedIndicatorProfile_ = next;
    savePersistentConfig_();
    emit indicatorProfileChanged();
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
    if (metadata == nullptr || !metadataHasParam(*metadata, normalizedKey)) return;
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
    setLimitOrderLatencyUs(value);
}

void BacktestViewModel::setCancelLatencyUs(const QString& value) {
    setLimitOrderLatencyUs(value);
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
    out << "order_latency_us=" << marketOrderLatencyUs_ << "\n";
    out << "amend_latency_us=" << limitOrderLatencyUs_ << "\n";
    out << "cancel_latency_us=" << limitOrderLatencyUs_ << "\n";
    out << "initial_balance_usdt=" << initialBalanceUsdt_ << "\n";
    out << "maker_fee_bps=" << makerFeeBps_ << "\n";
    out << "taker_fee_bps=" << takerFeeBps_ << "\n";
    out << "sweep_budget=" << sweepBudget_ << "\n";
    out << "sweep_seed=" << sweepSeed_ << "\n";
    out << "config_mode=" << configMode_ << "\n\n";
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
    const QString initialBalance = iniValue(text, QStringLiteral("backtest"), QStringLiteral("initial_balance_usdt"));
    const QString makerFee = iniValue(text, QStringLiteral("backtest"), QStringLiteral("maker_fee_bps"));
    const QString takerFee = iniValue(text, QStringLiteral("backtest"), QStringLiteral("taker_fee_bps"));
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
    if (!initialBalance.isEmpty()) initialBalanceUsdt_ = initialBalance;
    if (!makerFee.isEmpty()) makerFeeBps_ = makerFee;
    if (!takerFee.isEmpty()) takerFeeBps_ = takerFee;
    if (!sweepBudget.isEmpty()) sweepBudget_ = sweepBudget;
    if (!sweepSeed.isEmpty()) sweepSeed_ = sweepSeed;
    if (!mode.isEmpty()) configMode_ = normalizeConfigMode(mode);
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
    emit sweepConfigChanged();
    emit configChanged();
    emit strategyParametersChanged();
}

void BacktestViewModel::refresh() {
    updateWatcher_();
    std::vector<RunRecord> next;
    const QString dirPath = backtestsDirectory();
    QStringList filesToWatch;
    if (!dirPath.isEmpty()) {
        auto addRunDir = [&](const QFileInfo& runDir) {
            const QDir candidateDir(runDir.absoluteFilePath());
            const QString manifestPath = candidateDir.absoluteFilePath(QStringLiteral("manifest.json"));
            if (!QFileInfo::exists(manifestPath)) return;
            filesToWatch.push_back(manifestPath);
            filesToWatch.push_back(candidateDir.absoluteFilePath(QStringLiteral("equity.jsonl")));
            filesToWatch.push_back(candidateDir.absoluteFilePath(QStringLiteral("sweep_results.jsonl")));
            next.push_back(loadRecord_(runDir.absoluteFilePath()));
        };
        QDir dir(dirPath);
        const QFileInfoList dirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time | QDir::Name);
        const QDir sweepsDir(dir.absoluteFilePath(QStringLiteral("sweeps")));
        const QFileInfoList sweepDirs = sweepsDir.exists() ? sweepsDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time | QDir::Name) : QFileInfoList{};
        next.reserve(static_cast<std::size_t>(dirs.size() + sweepDirs.size()));
        for (const QFileInfo& runDir : dirs) {
            if (runDir.fileName() == QStringLiteral("sweeps")) continue;
            addRunDir(runDir);
        }
        for (const QFileInfo& sweepDir : sweepDirs) addRunDir(sweepDir);
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

bool BacktestViewModel::deleteSelectedRun() {
    const RunRecord* record = selectedRecord_();
    if (record == nullptr || running_) return false;

    const QDir backtestsDir(backtestsDirectory());
    const QString backtestsPath = QDir::cleanPath(backtestsDir.absolutePath());
    const QString runPath = QDir::cleanPath(QFileInfo(record->filePath).absoluteFilePath());
    const QString backtestsPrefix = backtestsPath.endsWith(QLatin1Char('/')) ? backtestsPath : backtestsPath + QLatin1Char('/');
    if (backtestsPath.isEmpty() || runPath == backtestsPath || !runPath.startsWith(backtestsPrefix)) {
        setStatusText_(QStringLiteral("Refusing to delete backtest outside selected session"));
        return false;
    }

    const QString deletedRunId = selectedRunId_;
    if (!QDir(runPath).removeRecursively()) {
        setStatusText_(QStringLiteral("Failed to delete backtest %1").arg(deletedRunId));
        return false;
    }

    selectedRunId_.clear();
    refresh();
    setStatusText_(QStringLiteral("Deleted backtest %1").arg(deletedRunId));
    return true;
}

void BacktestViewModel::startBacktest() {
    startBacktestWithOverrides_({}, QString{});
}

void BacktestViewModel::startBacktestWithOverrides_(const QHash<QString, QString>& overrides, const QString& suffix) {
    if (!canRun()) return;
    stopWorker_();
    cancelRequested_.store(false, std::memory_order_release);
    setRunning_(true);
    setProgress_(0, QStringLiteral("Starting"));
    setStatusText_(QStringLiteral("Backtest running"));

    const QString sessionPath = selectedSessionPath();
    const QString strategy = selectedStrategy_;
    QString runId = runId_();
    if (!suffix.trimmed().isEmpty()) runId += QStringLiteral("-") + cleanRunSlugPart(suffix);
    const QString configPath = writeRunConfig_(runId, overrides, false);
    if (configPath.isEmpty()) {
        setRunning_(false);
        setStatusText_(QStringLiteral("Failed to write backtest config"));
        return;
    }
    const quint64 pingLatency = latencyValue_(pingLatencyUs_, 1000);
    const quint64 latencySeed = latencyValue_(latencySeed_, 0);
    const quint64 marketDataLatency = latencyValue_(marketDataLatencyUs_, 250);
    const quint64 marketDataJitter = latencyValue_(marketDataJitterUs_, 100);
    const quint64 marketOrderLatency = latencyValue_(marketOrderLatencyUs_, pingLatency);
    const quint64 marketOrderJitter = latencyValue_(marketOrderJitterUs_, 0);
    const quint64 limitOrderLatency = latencyValue_(limitOrderLatencyUs_, pingLatency);
    const quint64 limitOrderJitter = latencyValue_(limitOrderJitterUs_, 0);
    const quint64 orderLatency = marketOrderLatency;
    const quint64 amendLatency = limitOrderLatency;
    const quint64 cancelLatency = limitOrderLatency;
    const qint64 initialBalance = decimalE8Value_(initialBalanceUsdt_, 0);
    const qint64 makerFee = decimalE8Value_(makerFeeBps_, 0);
    const qint64 takerFee = decimalE8Value_(takerFeeBps_, 0);
    const QString indicatorProfile = selectedIndicatorProfile_;
    worker_ = std::thread([this, sessionPath, strategy, runId, configPath, indicatorProfile, latencySeed, marketDataLatency, marketDataJitter, marketOrderLatency, marketOrderJitter, limitOrderLatency, limitOrderJitter, orderLatency, amendLatency, cancelLatency, initialBalance, makerFee, takerFee] {
        hft_backtest::BacktestRunRequest request{};
        request.sessionPath = sessionPath.toStdString();
        request.configPath = configPath.toStdString();
        request.strategy = strategy.toStdString();
        request.indicatorProfile = indicatorProfile.toStdString();
        request.runId = runId.toStdString();
        request.requestId = runId.toStdString();
        request.latencySeed = latencySeed;
        request.marketDataLatency.baseUs = marketDataLatency;
        request.marketDataLatency.jitterUs = marketDataJitter;
        request.marketOrderLatency.baseUs = marketOrderLatency;
        request.marketOrderLatency.jitterUs = marketOrderJitter;
        request.limitOrderLatency.baseUs = limitOrderLatency;
        request.limitOrderLatency.jitterUs = limitOrderJitter;
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

void BacktestViewModel::startSweep() {
    if (!canRun()) return;

    std::vector<hft_backtest::BacktestSweepParamRange> ranges;
    for (const QString& key : paramOrder_) {
        const hft_backtest::StrategyParamMetadata* param = paramMetadataFor(selectedStrategy_, key);
        if (param != nullptr && param->exclusiveGroup != 0u && activeParamByGroup_.value(static_cast<int>(param->exclusiveGroup)) != key) continue;
        const QString mode = normalizedParamMode(paramModes_.value(key, QStringLiteral("fixed")));
        if (mode == QStringLiteral("fixed")) continue;
        bool minOk = false;
        bool maxOk = false;
        bool stepOk = false;
        const qint64 minRaw = paramMinValues_.value(key).trimmed().toLongLong(&minOk);
        const qint64 maxRaw = paramMaxValues_.value(key).trimmed().toLongLong(&maxOk);
        const qint64 stepRaw = paramStepValues_.value(key).trimmed().toLongLong(&stepOk);
        if (!minOk || !maxOk || !stepOk || stepRaw <= 0 || maxRaw < minRaw) {
            setStatusText_(QStringLiteral("Invalid sweep range for %1").arg(key));
            return;
        }
        hft_backtest::BacktestSweepParamRange range{};
        range.key = key.toStdString();
        range.minRaw = minRaw;
        range.maxRaw = maxRaw;
        range.stepRaw = stepRaw;
        range.mode = hft_backtest::BacktestSweepParamMode::Grid;
        ranges.push_back(std::move(range));
    }
    if (ranges.empty()) {
        setStatusText_(QStringLiteral("Choose at least one Sweep parameter"));
        return;
    }

    stopWorker_();
    cancelRequested_.store(false, std::memory_order_release);
    setRunning_(true);
    setProgress_(0, QStringLiteral("Starting sweep"));
    setStatusText_(QStringLiteral("Sweep running"));

    const QString sessionPath = selectedSessionPath();
    const QString strategy = selectedStrategy_;
    const QString runId = QStringLiteral("sweep-") + runId_();
    const QString configPath = writeRunConfig_(QStringLiteral("sweeps/%1").arg(runId), {}, true);
    if (configPath.isEmpty()) {
        setRunning_(false);
        setStatusText_(QStringLiteral("Failed to write sweep config"));
        return;
    }
    const quint64 pingLatency = latencyValue_(pingLatencyUs_, 1000);
    const quint64 latencySeed = latencyValue_(latencySeed_, 0);
    const quint64 searchSeed = latencyValue_(sweepSeed_, 0);
    const quint64 runBudget = latencyValue_(sweepBudget_, 64);
    const quint64 marketDataLatency = latencyValue_(marketDataLatencyUs_, 250);
    const quint64 marketDataJitter = latencyValue_(marketDataJitterUs_, 100);
    const quint64 marketOrderLatency = latencyValue_(marketOrderLatencyUs_, pingLatency);
    const quint64 marketOrderJitter = latencyValue_(marketOrderJitterUs_, 0);
    const quint64 limitOrderLatency = latencyValue_(limitOrderLatencyUs_, pingLatency);
    const quint64 limitOrderJitter = latencyValue_(limitOrderJitterUs_, 0);
    const qint64 initialBalance = decimalE8Value_(initialBalanceUsdt_, 0);
    const qint64 makerFee = decimalE8Value_(makerFeeBps_, 0);
    const qint64 takerFee = decimalE8Value_(takerFeeBps_, 0);
    const QString indicatorProfile = selectedIndicatorProfile_;

    worker_ = std::thread([this, sessionPath, strategy, runId, configPath, indicatorProfile, latencySeed, searchSeed, runBudget, marketDataLatency, marketDataJitter, marketOrderLatency, marketOrderJitter, limitOrderLatency, limitOrderJitter, initialBalance, makerFee, takerFee, ranges = std::move(ranges)] {
        hft_backtest::BacktestSweepRequest request{};
        request.baseRun.sessionPath = sessionPath.toStdString();
        request.baseRun.configPath = configPath.toStdString();
        request.baseRun.strategy = strategy.toStdString();
        request.baseRun.indicatorProfile = indicatorProfile.toStdString();
        request.baseRun.latencySeed = latencySeed;
        request.baseRun.marketDataLatency.baseUs = marketDataLatency;
        request.baseRun.marketDataLatency.jitterUs = marketDataJitter;
        request.baseRun.marketOrderLatency.baseUs = marketOrderLatency;
        request.baseRun.marketOrderLatency.jitterUs = marketOrderJitter;
        request.baseRun.limitOrderLatency.baseUs = limitOrderLatency;
        request.baseRun.limitOrderLatency.jitterUs = limitOrderJitter;
        request.baseRun.orderLatencyUs = marketOrderLatency;
        request.baseRun.amendLatencyUs = limitOrderLatency;
        request.baseRun.cancelLatencyUs = limitOrderLatency;
        request.baseRun.initialBalanceE8 = initialBalance;
        request.baseRun.makerFeeBpsE8 = makerFee;
        request.baseRun.takerFeeBpsE8 = takerFee;
        request.baseRun.writeArtifacts = false;
        request.sweepId = runId.toStdString();
        request.runBudget = runBudget;
        request.searchSeed = searchSeed;
        request.outputPath = (QDir(sessionPath).absoluteFilePath(QStringLiteral("backtests/sweeps/%1").arg(runId))).toStdString();
        request.ranges = ranges;

        const auto result = hft_backtest::runBacktestSweep(request, progressCallback, this);
        const QString status = sweepStatusTextFor(result.status);
        const QString selected = QString::fromStdString(result.sweepId);
        QMetaObject::invokeMethod(this, [this, status, selected] {
            setRunning_(false);
            setProgress_(100, status);
            setStatusText_(status);
            refresh();
            if (!selected.isEmpty()) selectRun(selected);
        }, Qt::QueuedConnection);
    });
}

void BacktestViewModel::applySweepPoint(int rowIndex) {
    const QVariantList rows = selectedSweepRows();
    if (rowIndex < 0 || rowIndex >= rows.size()) return;
    const QVariantMap params = rows.at(rowIndex).toMap().value(QStringLiteral("params")).toMap();
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        const QString key = it.key().trimmed().toLower();
        if (!paramOrder_.contains(key)) continue;
        paramValues_.insert(key, QString::number(it.value().toLongLong()));
        paramModes_.insert(key, QStringLiteral("fixed"));
    }
    savePersistentConfig_();
    emit strategyParametersChanged();
}

void BacktestViewModel::applySweepPointById(int pointId) {
    const auto* record = selectedRecord_();
    if (record == nullptr || !record->sweep) return;
    for (const QVariant& value : record->sweepCurves) {
        const QVariantMap curve = value.toMap();
        if (curve.value(QStringLiteral("pointId")).toInt() != pointId) continue;
        const QVariantMap params = curve.value(QStringLiteral("params")).toMap();
        for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
            const QString key = it.key().trimmed().toLower();
            if (!paramOrder_.contains(key)) continue;
            paramValues_.insert(key, QString::number(it.value().toLongLong()));
            paramModes_.insert(key, QStringLiteral("fixed"));
        }
        savePersistentConfig_();
        emit strategyParametersChanged();
        return;
    }
}

void BacktestViewModel::startDetailedRunFromSweepPoint(int rowIndex) {
    const QVariantList rows = selectedSweepRows();
    if (rowIndex < 0 || rowIndex >= rows.size()) return;
    const QVariantMap params = rows.at(rowIndex).toMap().value(QStringLiteral("params")).toMap();
    QHash<QString, QString> overrides;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) overrides.insert(it.key().trimmed().toLower(), QString::number(it.value().toLongLong()));
    startBacktestWithOverrides_(overrides, QStringLiteral("detail"));
}

void BacktestViewModel::startDetailedRunFromSweepPointById(int pointId) {
    const auto* record = selectedRecord_();
    if (record == nullptr || !record->sweep) return;
    for (const QVariant& value : record->sweepCurves) {
        const QVariantMap curve = value.toMap();
        if (curve.value(QStringLiteral("pointId")).toInt() != pointId) continue;
        const QVariantMap params = curve.value(QStringLiteral("params")).toMap();
        QHash<QString, QString> overrides;
        for (auto it = params.constBegin(); it != params.constEnd(); ++it) overrides.insert(it.key().trimmed().toLower(), QString::number(it.value().toLongLong()));
        startBacktestWithOverrides_(overrides, QStringLiteral("detail"));
        return;
    }
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
    if (jsonValueString(object, QStringLiteral("type")) == QStringLiteral("sweep.result.v1")) {
        const QString sweepId = jsonValueString(object, QStringLiteral("sweep_id"));
        if (!sweepId.trimmed().isEmpty()) record.runId = sweepId.trimmed();
        record.sweep = true;
        record.status = errorCount(object.value(QStringLiteral("errors"))) == 0 ? QStringLiteral("complete") : QStringLiteral("error");
        record.strategy = jsonValueString(object, QStringLiteral("strategy"));
        record.displayName = QStringLiteral("Sweep %1").arg(record.strategy).trimmed();
        record.configText = QStringLiteral("budget=%1 seed=%2 points=%3")
            .arg(jsonValueString(object, QStringLiteral("budget")),
                 jsonValueString(object, QStringLiteral("search_seed")),
                 jsonValueString(object, QStringLiteral("points_evaluated")));
        const QJsonObject rowsObject = object.value(QStringLiteral("rows")).toObject();
        QString rowsPath = rowsObject.value(QStringLiteral("path")).toString(QStringLiteral("sweep_results.jsonl"));
        const QFileInfo rowsInfo(rowsPath);
        if (rowsInfo.isRelative()) rowsPath = runDir.absoluteFilePath(rowsPath);
        record.sweepRows = sweepRowsFromJsonl(rowsPath, QStringLiteral("total_pnl_e8"));
        const QJsonObject curvesObject = object.value(QStringLiteral("curves")).toObject();
        QString curvesPath = curvesObject.value(QStringLiteral("path")).toString(QStringLiteral("sweep_curves.jsonl"));
        const QFileInfo curvesInfo(curvesPath);
        if (curvesInfo.isRelative()) curvesPath = runDir.absoluteFilePath(curvesPath);
        record.sweepCurves = sweepCurvesFromJsonl(curvesPath);
        if (!record.sweepRows.empty()) record.totalPnlE8 = record.sweepRows.front().toMap().value(QStringLiteral("totalPnlE8")).toLongLong();
        if (!record.sweepCurves.empty()) {
            const QVariantMap first = record.sweepCurves.front().toMap();
            record.initialBalanceE8 = first.value(QStringLiteral("initialBalanceE8")).toLongLong();
            record.totalPnlE8 = first.value(QStringLiteral("totalPnlE8")).toLongLong();
        }
        record.errorCount = errorCount(object.value(QStringLiteral("errors")));
        record.summaryJson = humanSummaryJson(object);
        record.rawJson = QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
        return record;
    }
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
    record.totalPnlE8 = summary.value(QStringLiteral("total_pnl_e8")).toInteger();
    record.pnlText = pnlPercentText(record.totalPnlE8, record.initialBalanceE8);
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
        const hft_backtest::StrategyParamMetadata* param = paramMetadataFor(selectedStrategy_, key);
        if (param != nullptr && param->exclusiveGroup != 0u && activeParamByGroup_.value(static_cast<int>(param->exclusiveGroup)) != key) continue;
        const QString value = paramValues_.value(key).trimmed();
        if (!value.isEmpty()) parts.push_back(QStringLiteral("%1=%2").arg(key, value));
        if (parts.size() >= 3) break;
    }
    QString summary = configMode_.trimmed();
    if (!parts.empty()) summary += QStringLiteral(": ") + parts.join(QStringLiteral(", "));
    if (!selectedIndicatorProfile_.isEmpty()) summary += QStringLiteral(" | indicator=%1").arg(selectedIndicatorProfile_);
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
    for (std::size_t i = 0; i < metadata->paramCount && i < hft_backtest::kStrategyMetadataMaxParams; ++i) {
        const hft_backtest::StrategyParamMetadata& param = metadata->params[i];
        if (param.key == nullptr || param.key[0] == '\0') continue;
        const QString key = qString(param.key).trimmed().toLower();
        if (key.isEmpty() || paramOrder_.contains(key)) continue;
        const QString value = qString(param.defaultValue);
        paramOrder_.push_back(key);
        paramValues_.insert(key, value);
        paramModes_.insert(key, isSweepKey(key) ? QStringLiteral("sweep") : QStringLiteral("fixed"));
        paramMinValues_.insert(key, defaultRangeMin(key, value));
        paramMaxValues_.insert(key, defaultRangeMax(key, value));
        paramStepValues_.insert(key, defaultRangeStep(key));
        if (param.exclusiveGroup != 0u && (param.defaultActive || !activeParamByGroup_.contains(static_cast<int>(param.exclusiveGroup)))) {
            activeParamByGroup_.insert(static_cast<int>(param.exclusiveGroup), key);
        }
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
    initialBalanceUsdt_ = settings_.value(QStringLiteral("backtests/initial_balance_usdt"), initialBalanceUsdt_).toString().trimmed();
    if (initialBalanceUsdt_.isEmpty()) initialBalanceUsdt_ = QStringLiteral("1000");
    makerFeeBps_ = settings_.value(QStringLiteral("backtests/maker_fee_bps"), makerFeeBps_).toString().trimmed();
    if (makerFeeBps_.isEmpty()) makerFeeBps_ = QStringLiteral("0");
    takerFeeBps_ = settings_.value(QStringLiteral("backtests/taker_fee_bps"), takerFeeBps_).toString().trimmed();
    if (takerFeeBps_.isEmpty()) takerFeeBps_ = QStringLiteral("0");
    sweepBudget_ = settings_.value(QStringLiteral("backtests/sweep_budget"), sweepBudget_).toString().trimmed();
    if (sweepBudget_.isEmpty()) sweepBudget_ = QStringLiteral("64");
    sweepSeed_ = settings_.value(QStringLiteral("backtests/sweep_seed"), sweepSeed_).toString().trimmed();
    if (sweepSeed_.isEmpty()) sweepSeed_ = QStringLiteral("0");
    loadStrategyDefaults_();
    loadSavedParameterValues_();
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
    settings_.setValue(QStringLiteral("backtests/initial_balance_usdt"), initialBalanceUsdt_);
    settings_.setValue(QStringLiteral("backtests/maker_fee_bps"), makerFeeBps_);
    settings_.setValue(QStringLiteral("backtests/taker_fee_bps"), takerFeeBps_);
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

QString BacktestViewModel::writeRunConfig_(const QString& runId, const QHash<QString, QString>& overrides, bool fixedOnly) {
    const QString templatePath = configTemplatePathForStrategy(selectedStrategy_);
    const QString base = templatePath.isEmpty() ? QString{} : readTextFile(templatePath);
    if (!templatePath.isEmpty() && base.isEmpty()) return {};
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
    const QString filteredBase = filteredBaseConfig(base);
    out << filteredBase;
    if (!filteredBase.endsWith(QLatin1Char('\n'))) out << "\n";
    out << "\n# recorder backtest overrides\n";
    out << "[strategy]\n";
    out << "type=" << selectedStrategy_ << "\n";
    for (const QString& key : paramOrder_) {
        const hft_backtest::StrategyParamMetadata* param = paramMetadataFor(selectedStrategy_, key);
        if (param != nullptr && param->exclusiveGroup != 0u && activeParamByGroup_.value(static_cast<int>(param->exclusiveGroup)) != key) continue;
        if (fixedOnly && paramModes_.value(key, QStringLiteral("fixed")) != QStringLiteral("fixed")) continue;
        const QString value = overrides.value(key, paramValues_.value(key)).trimmed();
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
