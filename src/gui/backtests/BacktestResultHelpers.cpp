#include "gui/backtests/BacktestResultHelpers.hpp"

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QList>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <cstddef>

namespace hftrec::gui {
namespace {

constexpr qsizetype kMaxDisplayEquityPoints = 4096;

QJsonObject humanSummaryObject(const QJsonObject& object);

QString prettyJson(const QJsonValue& value) {
    if (value.isUndefined()) return {};
    QJsonDocument doc;
    if (value.isObject()) doc = QJsonDocument(value.toObject());
    else if (value.isArray()) doc = QJsonDocument(value.toArray());
    else return QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("value"), value}}).toJson(QJsonDocument::Indented));
    return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
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

QString metricDisplayValue(const QString& key, const QJsonValue& value) {
    if (isE8Key(key) && value.isDouble()) return e8DisplayString(value.toInteger());
    if (value.isBool()) return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (value.isDouble()) return QString::number(value.toInteger());
    if (value.isString()) return value.toString();
    return prettyJson(value).trimmed();
}

QString percentRatioText(qint64 numerator, qint64 denominator) {
    if (denominator <= 0) return QStringLiteral("n/a");
    const qint64 bps = (numerator * 10000) / denominator;
    const QString sign = bps < 0 ? QStringLiteral("-") : QString{};
    const qint64 absBps = bps < 0 ? -bps : bps;
    return QStringLiteral("%1%2.%3%")
        .arg(sign, QString::number(absBps / 100), QString::number(absBps % 100).rightJustified(2, QLatin1Char('0')));
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

qint64 pointInt(const QVariantMap& point, const QString& key) {
    return point.value(key).toLongLong();
}

void appendSyntheticEquityPoint(QVariantList& out,
                                qint64 ts,
                                qint64 grossRealized,
                                qint64 realized,
                                qint64 unrealized,
                                qint64 grossTotal,
                                qint64 netTotal,
                                qint64 total,
                                qint64 fees,
                                qint64 wallet,
                                qint64 position,
                                qint64 fillCount,
                                qint64& minPnl,
                                qint64& maxPnl,
                                bool& hasPoint) {
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
}

}  // namespace

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

bool isE8Key(const QString& key) {
    return key.size() > 3 && key.at(key.size() - 3) == QLatin1Char('_') && key.at(key.size() - 2) == QLatin1Char('e') && key.at(key.size() - 1) == QLatin1Char('8');
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

QString humanSummaryJson(const QJsonValue& value) {
    if (!value.isObject()) return prettyJson(value);
    return prettyJson(humanSummaryObject(value.toObject()));
}

int errorCount(const QJsonValue& value) {
    return value.isArray() ? value.toArray().size() : 0;
}

QVariantList resultMetrics(const QJsonObject& root, const QJsonObject& summary) {
    (void)root;
    QVariantList out;
    auto appendMetric = [&](const QString& key,
                            const QString& label,
                            const QString& value,
                            const QString& group,
                            bool primary,
                            bool hasSeries,
                            const QString& description,
                            const QString& why,
                            const QString& interpretation) {
        if (value.trimmed().isEmpty()) return;
        QVariantMap row;
        row.insert(QStringLiteral("key"), key);
        row.insert(QStringLiteral("label"), label);
        row.insert(QStringLiteral("value"), value);
        row.insert(QStringLiteral("group"), group);
        row.insert(QStringLiteral("primary"), primary);
        row.insert(QStringLiteral("hasSeries"), hasSeries);
        row.insert(QStringLiteral("description"), description);
        row.insert(QStringLiteral("why"), why);
        row.insert(QStringLiteral("interpretation"), interpretation);
        out.push_back(row);
    };
    auto appendSummary = [&](const char* rawKey,
                             const QString& label,
                             const QString& group,
                             bool primary,
                             bool hasSeries,
                             const QString& description,
                             const QString& why,
                             const QString& interpretation) {
        const QString key = QString::fromUtf8(rawKey);
        if (!summary.contains(key)) return;
        appendMetric(key, label, metricDisplayValue(key, summary.value(key)), group, primary, hasSeries, description, why, interpretation);
    };
    const char* realizedPnlKey = summary.contains(QStringLiteral("net_realized_pnl_e8")) ? "net_realized_pnl_e8" : "realized_pnl_e8";
    appendSummary(realizedPnlKey,
                  QStringLiteral("Realized PnL"),
                  QStringLiteral("Return"),
                  true,
                  true,
                  QStringLiteral("Итоговый реализованный PnL после комиссий, без нереализованной позиции."),
                  QStringLiteral("По графику видно, когда именно результат набирался или проседал."),
                  QStringLiteral("Плавный рост лучше резких скачков. Долгая плоская линия значит, что стратегия мало торговала или не находила сигнал."));
    appendSummary("total_pnl_e8",
                  QStringLiteral("Total PnL"),
                  QStringLiteral("Return"),
                  false,
                  true,
                  QStringLiteral("Итоговый PnL с учетом нереализованной позиции, если она была открыта."),
                  QStringLiteral("Нужен для сверки с engine summary, но основной экран смотрит realized PnL."),
                  QStringLiteral("Если отличается от Realized PnL, значит в конце backtest оставалась открытая позиция."));
    appendSummary("fees_paid_e8",
                  QStringLiteral("Fees paid"),
                  QStringLiteral("Return"),
                  true,
                  true,
                  QStringLiteral("Накопленная комиссия за исполнения."),
                  QStringLiteral("Показывает, съедает ли оборот прибыль. Можно делить PnL на fees, чтобы увидеть окупаемость комиссии."),
                  QStringLiteral("Если fees растут быстрее PnL, стратегия слишком много платит за входы/выходы."));
    appendSummary("wallet_balance_e8",
                  QStringLiteral("Final balance"),
                  QStringLiteral("Return"),
                  false,
                  true,
                  QStringLiteral("Баланс после всех реализованных PnL и комиссий."),
                  QStringLiteral("Помогает сверить итог backtest с начальным балансом и PnL."),
                  QStringLiteral("Финальный баланс должен совпадать с логикой Total PnL относительно initial balance."));
    appendSummary("max_drawdown_e8",
                  QStringLiteral("Max DD"),
                  QStringLiteral("Risk"),
                  true,
                  true,
                  QStringLiteral("Максимальная просадка от локального пика equity."),
                  QStringLiteral("Показывает худший момент, который пришлось бы пережить во время бектеста."),
                  QStringLiteral("Большой DD при маленьком PnL значит плохой risk/reward или нестабильное поведение."));
    appendSummary("risk_stopped",
                  QStringLiteral("Risk stop"),
                  QStringLiteral("Risk"),
                  true,
                  false,
                  QStringLiteral("Флаг срабатывания трейдерского risk guard."),
                  QStringLiteral("Показывает, остановил ли backtest новые заявки по тому же порогу, что использует trader."),
                  QStringLiteral("true значит, что дальнейшая торговля была остановлена, а позиция не была принудительно обнулена backtest-ликвидацией."));
    appendSummary("risk_stop_equity_e8",
                  QStringLiteral("Risk stop equity"),
                  QStringLiteral("Risk"),
                  false,
                  false,
                  QStringLiteral("Equity в момент срабатывания трейдерского risk guard."),
                  QStringLiteral("Значение включает нереализованный PnL по открытой позиции."),
                  QStringLiteral("Сверяй его с risk_stop_threshold_equity_e8, чтобы понять, на каком уровне произошла остановка."));
    appendSummary("risk_stop_threshold_equity_e8",
                  QStringLiteral("Risk threshold"),
                  QStringLiteral("Risk"),
                  false,
                  false,
                  QStringLiteral("Абсолютный equity-порог, рассчитанный из min_equity_pct и стартового баланса."),
                  QStringLiteral("Это фактическая граница, ниже которой trader risk guard запрещает новые заявки."),
                  QStringLiteral("Если initial_balance_e8 равен 200 USDT и min_equity_pct равен 60, порог будет 120 USDT."));
    appendSummary("mfe_e8",
                  QStringLiteral("MFE"),
                  QStringLiteral("Risk"),
                  false,
                  true,
                  QStringLiteral("Лучший достигнутый PnL по ходу прогона."),
                  QStringLiteral("Показывает, сколько стратегия могла иметь в лучшей точке до финала."),
                  QStringLiteral("Если MFE сильно выше финального PnL, стратегия отдает прибыль назад."));
    appendSummary("mae_e8",
                  QStringLiteral("MAE"),
                  QStringLiteral("Risk"),
                  false,
                  true,
                  QStringLiteral("Худший достигнутый PnL по ходу прогона."),
                  QStringLiteral("Показывает глубину неблагоприятного движения equity."),
                  QStringLiteral("Глубокий MAE говорит, что стратегия долго была в минусе или набирала риск против себя."));
    appendSummary("orders",
                  QStringLiteral("Orders"),
                  QStringLiteral("Execution"),
                  false,
                  false,
                  QStringLiteral("Всего отправленных заявок."),
                  QStringLiteral("Оценка активности алгоритма: как часто он пытался действовать."),
                  QStringLiteral("Много orders при малом fills может означать агрессивные перестановки или плохую достижимость цены."));
    appendSummary("fills",
                  QStringLiteral("Fills"),
                  QStringLiteral("Execution"),
                  true,
                  true,
                  QStringLiteral("Всего исполнений."),
                  QStringLiteral("Показывает реальные сделки, а не только намерения алгоритма."),
                  QStringLiteral("Скачки fills на графике помогают связать изменения PnL с моментами торговли."));
    const qint64 orders = summary.value(QStringLiteral("orders")).toInteger();
    const qint64 fills = summary.value(QStringLiteral("fills")).toInteger();
    if (summary.contains(QStringLiteral("orders")) && summary.contains(QStringLiteral("fills")) && orders > 0) {
        appendMetric(QStringLiteral("fill_rate"),
                     QStringLiteral("Fill rate"),
                     percentRatioText(fills, orders),
                     QStringLiteral("Execution"),
                     true,
                     false,
                     QStringLiteral("Доля заявок, которые дали исполнение."),
                     QStringLiteral("Показывает, насколько часто выставленные заявки реально становятся сделками."),
                     QStringLiteral("Слишком низкий fill rate может значить, что стратегия стоит далеко от рынка или слишком быстро отменяет заявки."));
    }
    appendSummary("reduce_only_fills",
                  QStringLiteral("Reduce-only fills"),
                  QStringLiteral("Execution"),
                  false,
                  false,
                  QStringLiteral("Исполнения, которые только уменьшали позицию."),
                  QStringLiteral("Отделяет закрытия/снижение риска от обычного набора позиции."),
                  QStringLiteral("Если reduce-only fills много, стратегия часто разгружалась или закрывала остатки."));
    return out;
}

QString pnlPercentText(qint64 pnlE8, qint64 initialBalanceE8) {
    if (initialBalanceE8 <= 0) return {};
    const qint64 bps = (pnlE8 * 10000) / initialBalanceE8;
    const QString sign = bps > 0 ? QStringLiteral("+") : (bps < 0 ? QStringLiteral("-") : QString{});
    const qint64 absBps = bps < 0 ? -bps : bps;
    return QStringLiteral("%1%2.%3%").arg(sign, QString::number(absBps / 100), QString::number(absBps % 100).rightJustified(2, QLatin1Char('0')));
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

QVariantList synthesizePortfolioEquityPoints(const std::vector<QVariantList>& legSeries, qint64& minPnl, qint64& maxPnl) {
    std::vector<qint64> timestamps;
    for (const QVariantList& series : legSeries) {
        timestamps.reserve(timestamps.size() + static_cast<std::size_t>(series.size()));
        for (const QVariant& value : series) timestamps.push_back(value.toMap().value(QStringLiteral("tsNs")).toLongLong());
    }
    std::sort(timestamps.begin(), timestamps.end());
    timestamps.erase(std::unique(timestamps.begin(), timestamps.end()), timestamps.end());
    if (timestamps.empty()) {
        minPnl = 0;
        maxPnl = 0;
        return {};
    }

    QVariantList out;
    out.reserve(static_cast<qsizetype>(timestamps.size()));
    std::vector<qsizetype> cursors(legSeries.size(), 0);
    bool hasPoint = false;
    for (qint64 ts : timestamps) {
        qint64 grossRealized = 0;
        qint64 realized = 0;
        qint64 unrealized = 0;
        qint64 grossTotal = 0;
        qint64 netTotal = 0;
        qint64 total = 0;
        qint64 fees = 0;
        qint64 wallet = 0;
        qint64 position = 0;
        qint64 fillCount = 0;
        for (std::size_t leg = 0; leg < legSeries.size(); ++leg) {
            const QVariantList& series = legSeries[leg];
            if (series.empty()) continue;
            qsizetype& cursor = cursors[leg];
            while (cursor + 1 < series.size() && series.at(cursor + 1).toMap().value(QStringLiteral("tsNs")).toLongLong() <= ts) ++cursor;
            const QVariantMap point = series.at(cursor).toMap();
            grossRealized += pointInt(point, QStringLiteral("grossRealizedPnlE8"));
            realized += pointInt(point, QStringLiteral("realizedPnlE8"));
            unrealized += pointInt(point, QStringLiteral("unrealizedPnlE8"));
            grossTotal += pointInt(point, QStringLiteral("grossTotalPnlE8"));
            netTotal += pointInt(point, QStringLiteral("netTotalPnlE8"));
            total += pointInt(point, QStringLiteral("totalPnlE8"));
            fees += pointInt(point, QStringLiteral("feesPaidE8"));
            wallet += pointInt(point, QStringLiteral("walletBalanceE8"));
            position += pointInt(point, QStringLiteral("positionQtyE8"));
            fillCount += pointInt(point, QStringLiteral("fillCount"));
        }
        appendSyntheticEquityPoint(out, ts, grossRealized, realized, unrealized, grossTotal, netTotal, total, fees, wallet, position, fillCount, minPnl, maxPnl, hasPoint);
    }
    return out;
}

}  // namespace hftrec::gui
