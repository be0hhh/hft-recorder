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
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>
#include <QVariantMap>
#include <QTimer>

#include <algorithm>
#include <array>
#include <exception>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hft_backtest/backtest.hpp"
#include "hft_backtest/backtest_sweep.hpp"
#include "core/common/Status.hpp"
#include "gui/models/BacktestSessionSummary.hpp"

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

QString percentRatioText(qint64 numerator, qint64 denominator) {
    if (denominator <= 0) return QStringLiteral("n/a");
    const qint64 bps = (numerator * 10000) / denominator;
    const QString sign = bps < 0 ? QStringLiteral("-") : QString{};
    const qint64 absBps = bps < 0 ? -bps : bps;
    return QStringLiteral("%1%2.%3%")
        .arg(sign, QString::number(absBps / 100), QString::number(absBps % 100).rightJustified(2, QLatin1Char('0')));
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

QString resolveRecordingsRoot() {
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString candidate = appDir.absoluteFilePath(QStringLiteral("../../recordings"));
    return QDir::cleanPath(QFileInfo(candidate).absoluteFilePath());
}

QString sessionSourceSummary(const QString& sessionPath, const BacktestLegCounts& backtestCounts) {
    QFile file(QDir(sessionPath).absoluteFilePath(QStringLiteral("manifest.json")));
    if (!file.open(QIODevice::ReadOnly)) return sessionBacktestSummaryText(0, backtestCounts, 0);
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    const QJsonObject manifest = doc.object();
    const QJsonObject bookTicker = manifest.value(QStringLiteral("channels")).toObject().value(QStringLiteral("bookticker")).toObject();
    return sessionBacktestSummaryText(bookTicker.value(QStringLiteral("declared_event_count")).toInt(),
                                      backtestCounts,
                                      manifest.value(QStringLiteral("capture")).toObject().value(QStringLiteral("started_at_ns")).toInteger());
}

QString statusTextFor(hft_backtest::Status status, const std::string& error = {}, const std::vector<std::string>& warnings = {}) {
    if (status == hft_backtest::Status::Ok) {
        if (!warnings.empty()) return QStringLiteral("Backtest complete: warning: ") + QString::fromStdString(warnings.front());
        return QStringLiteral("Backtest complete");
    }
    if (status == hft_backtest::Status::Cancelled) return QStringLiteral("Backtest cancelled");
    const std::string_view statusText = hft_backtest::statusToString(status);
    QString message = QStringLiteral("Backtest failed: %1")
        .arg(QString::fromUtf8(statusText.data(), static_cast<qsizetype>(statusText.size())));
    if (!error.empty()) message += QStringLiteral(": ") + QString::fromStdString(error);
    return message;
}

QString sweepStatusTextFor(hft_backtest::Status status, const std::string& error = {}) {
    if (status == hft_backtest::Status::Ok) return QStringLiteral("Sweep complete");
    if (status == hft_backtest::Status::Cancelled) return QStringLiteral("Sweep cancelled");
    const std::string_view statusText = hft_backtest::statusToString(status);
    QString message = QStringLiteral("Sweep failed: %1")
        .arg(QString::fromUtf8(statusText.data(), static_cast<qsizetype>(statusText.size())));
    if (!error.empty()) message += QStringLiteral(": ") + QString::fromStdString(error);
    return message;
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
    if (exchange == QStringLiteral("binance") && market == QStringLiteral("spot")) return QStringLiteral("binance_spot");
    if (exchange == QStringLiteral("binance")) return QStringLiteral("binance_futures");
    if (exchange == QStringLiteral("bybit") && market == QStringLiteral("spot")) return QStringLiteral("bybit_spot");
    if (exchange == QStringLiteral("bybit")) return QStringLiteral("bybit_futures");
    if (exchange == QStringLiteral("kucoin") && market == QStringLiteral("spot")) return QStringLiteral("kucoin_spot");
    if (exchange == QStringLiteral("kucoin")) return QStringLiteral("kucoin_futures");
    if (exchange == QStringLiteral("gate") && market == QStringLiteral("spot")) return QStringLiteral("gate_spot");
    if (exchange == QStringLiteral("gate")) return QStringLiteral("gate_futures");
    if (exchange == QStringLiteral("bitget") && market == QStringLiteral("spot")) return QStringLiteral("bitget_spot");
    if (exchange == QStringLiteral("bitget") && market == QStringLiteral("inverse")) return QStringLiteral("bitget_inverse");
    if (exchange == QStringLiteral("bitget") && market == QStringLiteral("swap")) return QStringLiteral("bitget_swap");
    if (exchange == QStringLiteral("bitget")) return QStringLiteral("bitget_futures");
    if (exchange == QStringLiteral("aster") && market == QStringLiteral("spot")) return QStringLiteral("aster_spot");
    if (exchange == QStringLiteral("aster")) return QStringLiteral("aster_futures");
    if (exchange == QStringLiteral("okx") && market == QStringLiteral("spot")) return QStringLiteral("okx_spot");
    if (exchange == QStringLiteral("okx")) return QStringLiteral("okx_futures");
    return QStringLiteral("binance_futures");
}

QString venueSectionForSession(const QString& sessionPath) {
    return venueSectionFor(manifestValue(sessionPath, QStringLiteral("exchange")),
                           manifestValue(sessionPath, QStringLiteral("market")));
}

QString symbolForSessionPath(const QString& sessionPath) {
    const QString fromManifest = manifestValue(sessionPath, QStringLiteral("symbols")).trimmed().toUpper();
    if (!fromManifest.isEmpty()) return fromManifest;
    return symbolFromSessionId(QFileInfo(sessionPath).fileName()).toUpper();
}

QString sessionPathFromToken(const QString& recordingsRoot, const QString& token) {
    const QString trimmed = token.trimmed();
    if (trimmed.isEmpty()) return {};
    const QFileInfo info(trimmed);
    if (info.isAbsolute()) return QDir::cleanPath(info.absoluteFilePath());
    return QDir(recordingsRoot).absoluteFilePath(trimmed);
}

QString normalizedFeeMarket(QString market) {
    market = market.trimmed().toLower();
    if (market == QStringLiteral("usdt") || market == QStringLiteral("linear")) return QStringLiteral("futures_usdt");
    if (market == QStringLiteral("usdc")) return QStringLiteral("futures_usdc");
    return market;
}

QString venueExecutionKey(const QString& sessionPath) {
    const QString exchange = manifestValue(sessionPath, QStringLiteral("exchange")).trimmed().toLower();
    const QString market = normalizedFeeMarket(manifestValue(sessionPath, QStringLiteral("market")));
    if (exchange.isEmpty() || market.isEmpty()) return {};
    return exchange + QLatin1Char('|') + market;
}

QString venueExecutionSettingKey(QString venueKey) {
    venueKey.replace(QLatin1Char('|'), QStringLiteral("__"));
    venueKey.replace(QLatin1Char('/'), QLatin1Char('_'));
    venueKey.replace(QLatin1Char('\\'), QLatin1Char('_'));
    return venueKey;
}

QString venueExecutionMapKey(const QString& venueKey, const QString& field) {
    return venueKey + QLatin1Char('|') + field.trimmed().toLower();
}

bool isVenueExecutionField(const QString& field) {
    return field == QStringLiteral("initial_balance_usdt") ||
           field == QStringLiteral("maker_fee_bps") ||
           field == QStringLiteral("taker_fee_bps") ||
           field == QStringLiteral("market_data_latency_us") ||
           field == QStringLiteral("market_data_jitter_us") ||
           field == QStringLiteral("market_order_latency_us") ||
           field == QStringLiteral("market_order_jitter_us") ||
           field == QStringLiteral("limit_order_latency_us") ||
           field == QStringLiteral("limit_order_jitter_us") ||
           field == QStringLiteral("cancel_order_latency_us") ||
           field == QStringLiteral("cancel_order_jitter_us") ||
           field == QStringLiteral("user_data_latency_us") ||
           field == QStringLiteral("user_data_jitter_us");
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
    return key == QStringLiteral("distance_bps") || key == QStringLiteral("trigger_bps") ||
           key == QStringLiteral("trigger_edge_bps") || key == QStringLiteral("close_delay_us");
}

QString defaultRangeMin(const QString& key, const QString& value) {
    if (key == QStringLiteral("distance_bps")) return QStringLiteral("10");
    if (key == QStringLiteral("trigger_bps") || key == QStringLiteral("trigger_edge_bps")) return QStringLiteral("1");
    if (key == QStringLiteral("close_delay_us")) return QStringLiteral("0");
    return value;
}

QString defaultRangeMax(const QString& key, const QString& value) {
    if (key == QStringLiteral("distance_bps")) return QStringLiteral("1000");
    if (key == QStringLiteral("trigger_bps") || key == QStringLiteral("trigger_edge_bps")) return QStringLiteral("100");
    if (key == QStringLiteral("close_delay_us")) return QStringLiteral("10000000");
    return value;
}

QString defaultRangeStep(const QString& key) {
    if (key == QStringLiteral("distance_bps")) return QStringLiteral("10");
    if (key == QStringLiteral("trigger_bps") || key == QStringLiteral("trigger_edge_bps")) return QStringLiteral("1");
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

QVariantList sweepViewChoiceRows() {
    QVariantList out;
    for (const auto& pair : {std::pair{QStringLiteral("curves"), QStringLiteral("PnL ranking")},
                             std::pair{QStringLiteral("distribution"), QStringLiteral("Distribution")}}) {
        QVariantMap row;
        row.insert(QStringLiteral("id"), pair.first);
        row.insert(QStringLiteral("label"), pair.second);
        out.push_back(row);
    }
    return out;
}

QVariantList sweepMetricChoiceRows() {
    QVariantList out;
    QVariantMap row;
    row.insert(QStringLiteral("id"), QStringLiteral("total_pnl_e8"));
    row.insert(QStringLiteral("label"), QStringLiteral("Total"));
    out.push_back(row);
    return out;
}

qint64 jsonIntValue(const QJsonObject& object, const QString& key) {
    return object.value(key).toInteger();
}

QString sweepLegMetricKey(int legIndex) {
    return QStringLiteral("leg_%1_total_pnl_e8").arg(legIndex);
}

int sweepMetricLegIndex(const QString& metricKey) {
    static const QRegularExpression re(QStringLiteral("^leg_(\\d+)_total_pnl_e8$"));
    const QRegularExpressionMatch match = re.match(metricKey);
    if (!match.hasMatch()) return -1;
    bool ok = false;
    const int index = match.captured(1).toInt(&ok);
    return ok ? index : -1;
}

QVariantList sweepCurveValues(const QJsonObject& row, const QString& key) {
    QVariantList curve;
    const QJsonArray values = row.value(key).toArray();
    for (const QJsonValue& value : values) curve.push_back(value.toInteger());
    return curve;
}

QString sweepLegLabel(int legIndex, const QJsonObject& leg) {
    QStringList parts;
    const QString exchange = leg.value(QStringLiteral("exchange")).toString().trimmed();
    const QString symbol = leg.value(QStringLiteral("symbol")).toString().trimmed();
    if (!exchange.isEmpty()) parts.push_back(exchange);
    if (!symbol.isEmpty()) parts.push_back(symbol);
    const QString suffix = parts.join(QLatin1Char(' '));
    return suffix.isEmpty() ? QStringLiteral("Leg %1").arg(legIndex + 1) : QStringLiteral("Leg %1 %2").arg(legIndex + 1).arg(suffix);
}

QVariantMap sweepLegMap(const QJsonObject& leg, int fallbackIndex, bool includeCurve) {
    const int legIndex = static_cast<int>(leg.value(QStringLiteral("leg_index")).toInteger(fallbackIndex));
    QVariantMap out;
    out.insert(QStringLiteral("legIndex"), legIndex);
    out.insert(QStringLiteral("metricKey"), sweepLegMetricKey(legIndex));
    out.insert(QStringLiteral("label"), sweepLegLabel(legIndex, leg));
    out.insert(QStringLiteral("exchange"), leg.value(QStringLiteral("exchange")).toString());
    out.insert(QStringLiteral("market"), leg.value(QStringLiteral("market")).toString());
    out.insert(QStringLiteral("symbol"), leg.value(QStringLiteral("symbol")).toString());
    out.insert(QStringLiteral("initialBalanceE8"), jsonIntValue(leg, QStringLiteral("initial_balance_e8")));
    out.insert(QStringLiteral("walletBalanceE8"), jsonIntValue(leg, QStringLiteral("wallet_balance_e8")));
    out.insert(QStringLiteral("totalPnlE8"), jsonIntValue(leg, QStringLiteral("total_pnl_e8")));
    if (includeCurve) {
        QVariantList curve = sweepCurveValues(leg, QStringLiteral("curve_e8"));
        if (curve.empty()) curve.push_back(jsonIntValue(leg, QStringLiteral("total_pnl_e8")));
        out.insert(QStringLiteral("curve"), curve);
    }
    return out;
}

QVariantList sweepLegsFromRow(const QJsonObject& row, bool includeCurve) {
    QVariantList out;
    const QJsonArray legs = row.value(QStringLiteral("legs")).toArray();
    for (int i = 0; i < legs.size(); ++i) {
        const QJsonObject leg = legs.at(i).toObject();
        if (leg.isEmpty()) continue;
        out.push_back(sweepLegMap(leg, i, includeCurve));
    }
    return out;
}

QVariantMap sweepMapForMetric(QVariantMap out, const QString& metricKey) {
    const int legIndex = sweepMetricLegIndex(metricKey);
    qint64 metricRaw = out.value(QStringLiteral("totalPnlE8")).toLongLong();
    qint64 initialBalance = out.value(QStringLiteral("initialBalanceE8")).toLongLong();
    QString metricLabel = QStringLiteral("Total PnL");
    if (legIndex >= 0) {
        const QVariantList legs = out.value(QStringLiteral("legs")).toList();
        for (const QVariant& value : legs) {
            const QVariantMap leg = value.toMap();
            if (leg.value(QStringLiteral("legIndex")).toInt() != legIndex) continue;
            metricRaw = leg.value(QStringLiteral("totalPnlE8")).toLongLong();
            initialBalance = leg.value(QStringLiteral("initialBalanceE8")).toLongLong();
            metricLabel = leg.value(QStringLiteral("label")).toString();
            if (leg.contains(QStringLiteral("curve"))) out.insert(QStringLiteral("curve"), leg.value(QStringLiteral("curve")).toList());
            break;
        }
    }
    out.insert(QStringLiteral("metricKey"), metricKey);
    out.insert(QStringLiteral("metricRaw"), metricRaw);
    out.insert(QStringLiteral("metricText"), e8DisplayString(metricRaw));
    out.insert(QStringLiteral("metricLabel"), metricLabel);
    out.insert(QStringLiteral("initialBalanceE8"), initialBalance);
    out.insert(QStringLiteral("totalPnlE8"), metricRaw);
    out.insert(QStringLiteral("totalPnlText"), e8DisplayString(metricRaw));
    return out;
}

QVariantMap sweepRowMap(const QJsonObject& row, const QString& metricKey) {
    QVariantMap out;
    const QJsonObject params = row.value(QStringLiteral("params")).toObject();
    QStringList parts;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) parts.push_back(QStringLiteral("%1=%2").arg(it.key(), QString::number(it.value().toInteger())));
    out.insert(QStringLiteral("pointId"), jsonIntValue(row, QStringLiteral("point_id")));
    out.insert(QStringLiteral("label"), parts.join(QStringLiteral(", ")));
    out.insert(QStringLiteral("params"), params.toVariantMap());
    out.insert(QStringLiteral("initialBalanceE8"), jsonIntValue(row, QStringLiteral("initial_balance_e8")));
    out.insert(QStringLiteral("totalPnlE8"), jsonIntValue(row, QStringLiteral("total_pnl_e8")));
    out.insert(QStringLiteral("legs"), sweepLegsFromRow(row, false));
    out.insert(QStringLiteral("status"), row.value(QStringLiteral("status")).toString());
    return sweepMapForMetric(out, metricKey);
}

QStringList sweepParamKeysFromManifest(const QJsonObject& object) {
    QStringList out;
    const QJsonArray ranges = object.value(QStringLiteral("ranges")).toArray();
    for (const QJsonValue& value : ranges) {
        const QString key = value.toObject().value(QStringLiteral("key")).toString().trimmed();
        if (!key.isEmpty() && !out.contains(key)) out.push_back(key);
    }
    return out;
}

void appendSweepParamKeysFromRows(const QVariantList& rows, QStringList& keys) {
    for (const QVariant& value : rows) {
        const QVariantMap params = value.toMap().value(QStringLiteral("params")).toMap();
        for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
            if (!keys.contains(it.key())) keys.push_back(it.key());
        }
    }
}

QVariantMap sweepCurveMap(const QJsonObject& row) {
    QVariantMap out;
    const QJsonObject params = row.value(QStringLiteral("params")).toObject();
    QStringList parts;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) parts.push_back(QStringLiteral("%1=%2").arg(it.key(), QString::number(it.value().toInteger())));
    QVariantList curve = sweepCurveValues(row, QStringLiteral("curve_e8"));
    if (curve.empty()) curve.push_back(row.value(QStringLiteral("total_pnl_e8")).toInteger());
    out.insert(QStringLiteral("pointId"), jsonIntValue(row, QStringLiteral("point_id")));
    out.insert(QStringLiteral("label"), parts.join(QStringLiteral(", ")));
    out.insert(QStringLiteral("params"), params.toVariantMap());
    out.insert(QStringLiteral("status"), row.value(QStringLiteral("status")).toString());
    out.insert(QStringLiteral("initialBalanceE8"), jsonIntValue(row, QStringLiteral("initial_balance_e8")));
    out.insert(QStringLiteral("totalPnlE8"), jsonIntValue(row, QStringLiteral("total_pnl_e8")));
    out.insert(QStringLiteral("totalPnlText"), e8DisplayString(jsonIntValue(row, QStringLiteral("total_pnl_e8"))));
    out.insert(QStringLiteral("curve"), curve);
    out.insert(QStringLiteral("legs"), sweepLegsFromRow(row, true));
    out.insert(QStringLiteral("metricKey"), QStringLiteral("total_pnl_e8"));
    out.insert(QStringLiteral("metricRaw"), jsonIntValue(row, QStringLiteral("total_pnl_e8")));
    out.insert(QStringLiteral("metricLabel"), QStringLiteral("Total PnL"));
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
            skipSection = section == QStringLiteral("strategy") || section.startsWith(QStringLiteral("venue.")) ||
                          section.startsWith(QStringLiteral("portfolio."));
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
    refreshTimer_.setSingleShot(true);
    refreshTimer_.setInterval(200);
    connect(&refreshTimer_, &QTimer::timeout, this, &BacktestViewModel::refresh);
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, [this]() { scheduleRefresh_(); });
    connect(&watcher_, &QFileSystemWatcher::fileChanged, this, [this]() { scheduleRefresh_(); });
    loadPersistentConfig_();
    sessions_ = loadSessions_();
    if (!sessions_.empty()) selectedSessionId_ = sessions_.front().toMap().value(QStringLiteral("id")).toString();
    ensureSelectedStrategySupportsSessionCount_();
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
    return sessions_;
}

QVariantList BacktestViewModel::loadSessions_() const {
    QVariantList out;
    QDir recordingsDir(recordingsRoot());
    if (!recordingsDir.exists()) return out;

    const auto entries = recordingsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time | QDir::Reversed);
    for (const auto& entry : entries) {
        if (entry == QStringLiteral("backtest_profiles")) continue;
        const QString path = recordingsDir.absoluteFilePath(entry);
        const QDir backtestsDir(QDir(path).absoluteFilePath(QStringLiteral("backtests")));
        const BacktestLegCounts backtestCounts = backtestLegCountsForSession(recordingsRoot(), entry);
        QVariantMap row;
        row.insert(QStringLiteral("id"), entry);
        row.insert(QStringLiteral("label"), entry);
        row.insert(QStringLiteral("path"), path);
        row.insert(QStringLiteral("hasManifest"), QFileInfo::exists(QDir(path).absoluteFilePath(QStringLiteral("manifest.json"))));
        row.insert(QStringLiteral("hasBacktests"), backtestsDir.exists());
        row.insert(QStringLiteral("backtestCount"), backtestCounts.firstLeg);
        row.insert(QStringLiteral("firstLegBacktestCount"), backtestCounts.firstLeg);
        row.insert(QStringLiteral("secondLegBacktestCount"), backtestCounts.secondLeg);
        QString rightText = sessionSourceSummary(path, backtestCounts);
        row.insert(QStringLiteral("rightText"), rightText);
        out.push_back(row);
    }
    return out;
}

QString BacktestViewModel::selectedSessionPath() const {
    if (!manualSessionPath_.trimmed().isEmpty()) return manualSessionPath_;
    if (selectedSessionId_.trimmed().isEmpty()) return {};
    return QDir(recordingsRoot()).absoluteFilePath(selectedSessionId_);
}

QStringList BacktestViewModel::selectedSessionPaths_() const {
    QStringList out;
    const QString primary = selectedSessionPath();
    if (!primary.trimmed().isEmpty()) out.push_back(primary);
    const QStringList tokens = extraSessionIds_.split(QRegularExpression(QStringLiteral("[,;\\n]+")), Qt::SkipEmptyParts);
    const QString root = recordingsRoot();
    for (const QString& token : tokens) {
        const QString path = sessionPathFromToken(root, token);
        if (path.isEmpty() || out.contains(path)) continue;
        out.push_back(path);
        if (out.size() >= 8) break;
    }
    return out;
}

QVariantList BacktestViewModel::selectedSessionLegs() const {
    QVariantList out;
    const QStringList paths = selectedSessionPaths_();
    for (int i = 0; i < paths.size(); ++i) {
        QVariantMap row;
        const QString path = paths.at(i);
        const QString venueKey = venueExecutionKey(path);
        row.insert(QStringLiteral("index"), i);
        row.insert(QStringLiteral("path"), path);
        row.insert(QStringLiteral("id"), sessionIdFromPath_(path));
        row.insert(QStringLiteral("symbol"), symbolForSessionPath(path));
        row.insert(QStringLiteral("venue"), venueSectionForSession(path));
        row.insert(QStringLiteral("venueKey"), venueKey);
        row.insert(QStringLiteral("exchange"), manifestValue(path, QStringLiteral("exchange")).trimmed().toLower());
        row.insert(QStringLiteral("market"), normalizedFeeMarket(manifestValue(path, QStringLiteral("market"))));
        row.insert(QStringLiteral("initialBalanceUsdt"), venueExecutionValue_(venueKey, QStringLiteral("initial_balance_usdt"), initialBalanceUsdt_));
        row.insert(QStringLiteral("makerFeeBps"), venueExecutionValue_(venueKey, QStringLiteral("maker_fee_bps"), makerFeeBps_));
        row.insert(QStringLiteral("takerFeeBps"), venueExecutionValue_(venueKey, QStringLiteral("taker_fee_bps"), takerFeeBps_));
        row.insert(QStringLiteral("marketDataLatencyUs"), venueExecutionValue_(venueKey, QStringLiteral("market_data_latency_us"), marketDataLatencyUs_));
        row.insert(QStringLiteral("marketDataJitterUs"), venueExecutionValue_(venueKey, QStringLiteral("market_data_jitter_us"), marketDataJitterUs_));
        row.insert(QStringLiteral("marketOrderLatencyUs"), venueExecutionValue_(venueKey, QStringLiteral("market_order_latency_us"), marketOrderLatencyUs_));
        row.insert(QStringLiteral("marketOrderJitterUs"), venueExecutionValue_(venueKey, QStringLiteral("market_order_jitter_us"), marketOrderJitterUs_));
        row.insert(QStringLiteral("limitOrderLatencyUs"), venueExecutionValue_(venueKey, QStringLiteral("limit_order_latency_us"), limitOrderLatencyUs_));
        row.insert(QStringLiteral("limitOrderJitterUs"), venueExecutionValue_(venueKey, QStringLiteral("limit_order_jitter_us"), limitOrderJitterUs_));
        row.insert(QStringLiteral("cancelOrderLatencyUs"), venueExecutionValue_(venueKey, QStringLiteral("cancel_order_latency_us"), cancelOrderLatencyUs_));
        row.insert(QStringLiteral("cancelOrderJitterUs"), venueExecutionValue_(venueKey, QStringLiteral("cancel_order_jitter_us"), cancelOrderJitterUs_));
        row.insert(QStringLiteral("userDataLatencyUs"), venueExecutionValue_(venueKey, QStringLiteral("user_data_latency_us"), userDataLatencyUs_));
        row.insert(QStringLiteral("userDataJitterUs"), venueExecutionValue_(venueKey, QStringLiteral("user_data_jitter_us"), userDataJitterUs_));
        row.insert(QStringLiteral("label"), QStringLiteral("%1: %2 %3")
            .arg(i + 1)
            .arg(venueSectionForSession(path), symbolForSessionPath(path)));
        out.push_back(row);
    }
    return out;
}

int BacktestViewModel::selectedSessionCount() const {
    return selectedSessionPaths_().size();
}

QString BacktestViewModel::venueExecutionValue_(const QString& venueKey, const QString& field, const QString& fallback) const {
    const QString normalizedField = field.trimmed().toLower();
    if (venueKey.isEmpty() || normalizedField.isEmpty()) return fallback;
    const QString mapKey = venueExecutionMapKey(venueKey, normalizedField);
    if (venueExecutionValues_.contains(mapKey)) return venueExecutionValues_.value(mapKey);
    return settings_.value(QStringLiteral("backtests/venue_execution/%1/%2")
                               .arg(venueExecutionSettingKey(venueKey), normalizedField),
                           fallback)
        .toString()
        .trimmed();
}

std::vector<QVariantMap> BacktestViewModel::venueExecutionRows_() const {
    std::vector<QVariantMap> out;
    QSet<QString> emitted;
    const QStringList paths = selectedSessionPaths_();
    out.reserve(static_cast<std::size_t>(paths.size()));
    for (const QString& path : paths) {
        const QString venueKey = venueExecutionKey(path);
        if (venueKey.isEmpty() || emitted.contains(venueKey)) continue;
        emitted.insert(venueKey);
        QVariantMap row;
        row.insert(QStringLiteral("exchange"), manifestValue(path, QStringLiteral("exchange")).trimmed().toLower());
        row.insert(QStringLiteral("market"), normalizedFeeMarket(manifestValue(path, QStringLiteral("market"))));
        row.insert(QStringLiteral("initialBalanceUsdt"), venueExecutionValue_(venueKey, QStringLiteral("initial_balance_usdt"), initialBalanceUsdt_));
        row.insert(QStringLiteral("makerFeeBps"), venueExecutionValue_(venueKey, QStringLiteral("maker_fee_bps"), makerFeeBps_));
        row.insert(QStringLiteral("takerFeeBps"), venueExecutionValue_(venueKey, QStringLiteral("taker_fee_bps"), takerFeeBps_));
        row.insert(QStringLiteral("marketDataLatencyUs"), venueExecutionValue_(venueKey, QStringLiteral("market_data_latency_us"), marketDataLatencyUs_));
        row.insert(QStringLiteral("marketDataJitterUs"), venueExecutionValue_(venueKey, QStringLiteral("market_data_jitter_us"), marketDataJitterUs_));
        row.insert(QStringLiteral("marketOrderLatencyUs"), venueExecutionValue_(venueKey, QStringLiteral("market_order_latency_us"), marketOrderLatencyUs_));
        row.insert(QStringLiteral("marketOrderJitterUs"), venueExecutionValue_(venueKey, QStringLiteral("market_order_jitter_us"), marketOrderJitterUs_));
        row.insert(QStringLiteral("limitOrderLatencyUs"), venueExecutionValue_(venueKey, QStringLiteral("limit_order_latency_us"), limitOrderLatencyUs_));
        row.insert(QStringLiteral("limitOrderJitterUs"), venueExecutionValue_(venueKey, QStringLiteral("limit_order_jitter_us"), limitOrderJitterUs_));
        row.insert(QStringLiteral("cancelOrderLatencyUs"), venueExecutionValue_(venueKey, QStringLiteral("cancel_order_latency_us"), cancelOrderLatencyUs_));
        row.insert(QStringLiteral("cancelOrderJitterUs"), venueExecutionValue_(venueKey, QStringLiteral("cancel_order_jitter_us"), cancelOrderJitterUs_));
        row.insert(QStringLiteral("userDataLatencyUs"), venueExecutionValue_(venueKey, QStringLiteral("user_data_latency_us"), userDataLatencyUs_));
        row.insert(QStringLiteral("userDataJitterUs"), venueExecutionValue_(venueKey, QStringLiteral("user_data_jitter_us"), userDataJitterUs_));
        out.push_back(std::move(row));
    }
    return out;
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
QVariantList BacktestViewModel::strategyChoices() const {
    QVariantList out;
    const int selectedCount = selectedSessionCount();
    const std::size_t count = hft_backtest::strategyMetadataCount();
    for (std::size_t i = 0; i < count; ++i) {
        const hft_backtest::StrategyMetadata* metadata = hft_backtest::strategyMetadataAt(i);
        if (metadata == nullptr || metadata->id == nullptr) continue;
        if (!strategyMetadataSupportsSessionCount(*metadata, selectedCount)) continue;
        QVariantMap row;
        const QString id = qString(metadata->id);
        row.insert(QStringLiteral("id"), id);
        row.insert(QStringLiteral("label"), id);
        const int minCount = metadata->minSessionCount == 0u ? 1 : static_cast<int>(metadata->minSessionCount);
        const int maxCount = metadata->maxSessionCount == 0u ? 1 : static_cast<int>(metadata->maxSessionCount);
        row.insert(QStringLiteral("minSessions"), minCount);
        row.insert(QStringLiteral("maxSessions"), maxCount);
        row.insert(QStringLiteral("rightText"), strategySessionRangeText(*metadata));
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

QVariantList BacktestViewModel::sweepViewChoices() const {
    return sweepViewChoiceRows();
}

QVariantList BacktestViewModel::sweepMetricChoices() const {
    QVariantList out = sweepMetricChoiceRows();
    const auto* record = selectedRecord_();
    if (record == nullptr || !record->sweep || !record->detailsLoaded) return out;
    QVariantList legs;
    if (!record->sweepCurves.empty()) legs = record->sweepCurves.front().toMap().value(QStringLiteral("legs")).toList();
    if (legs.empty() && !record->sweepRows.empty()) legs = record->sweepRows.front().toMap().value(QStringLiteral("legs")).toList();
    for (const QVariant& value : legs) {
        const QVariantMap leg = value.toMap();
        const int legIndex = leg.value(QStringLiteral("legIndex"), out.size() - 1).toInt();
        const QString id = sweepLegMetricKey(legIndex);
        bool exists = false;
        for (const QVariant& existing : out) {
            if (existing.toMap().value(QStringLiteral("id")).toString() == id) {
                exists = true;
                break;
            }
        }
        if (exists) continue;
        QVariantMap row;
        row.insert(QStringLiteral("id"), id);
        row.insert(QStringLiteral("label"), leg.value(QStringLiteral("label"), QStringLiteral("Leg %1").arg(legIndex + 1)).toString());
        out.push_back(row);
    }
    return out;
}

QVariantList BacktestViewModel::strategyParameters() const {
    QVariantList out;
    const hft_backtest::StrategyMetadata* metadata = metadataForStrategy(selectedStrategy_);
    if (metadata == nullptr) return out;
    std::vector<std::uint8_t> emittedGroups;
    for (const QString& key : paramOrder_) {
        const hft_backtest::StrategyParamMetadata* param = paramMetadataFor(selectedStrategy_, key);
        const std::uint8_t group = param == nullptr ? 0u : param->exclusiveGroup;
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
        row.insert(QStringLiteral("description"), param == nullptr ? QString{} : qString(param->descriptionRu));
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
        row.insert(QStringLiteral("detailsLoaded"), record.detailsLoaded);
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

QString BacktestViewModel::selectedConfigText() const {
    const auto* record = selectedRecord_();
    return record == nullptr ? QString{} : record->configText;
}

QString BacktestViewModel::selectedErrorText() const {
    const auto* record = selectedRecord_();
    return record == nullptr ? QString{} : record->errorText;
}

QVariantList BacktestViewModel::selectedEquityPoints() const {
    const auto* record = selectedRecord_();
    if (record == nullptr) return {};
    const QString scope = effectiveResultScopeId_(*record);
    if (record->scopedEquityPoints.contains(scope)) return record->scopedEquityPoints.value(scope);
    return record->equityPoints;
}

QVariantList BacktestViewModel::resultScopeChoices() const {
    const auto* record = selectedRecord_();
    return record == nullptr ? QVariantList{} : record->resultScopes;
}

QString BacktestViewModel::selectedResultScope() const {
    const auto* record = selectedRecord_();
    return record == nullptr ? QStringLiteral("portfolio") : effectiveResultScopeId_(*record);
}

QVariantList BacktestViewModel::selectedResultMetrics() const {
    const auto* record = selectedRecord_();
    if (record == nullptr) return {};
    const QString scope = effectiveResultScopeId_(*record);
    if (record->scopedResultMetrics.contains(scope)) return record->scopedResultMetrics.value(scope);
    return record->resultMetrics;
}

QString BacktestViewModel::selectedResultMetricKey() const {
    const auto* record = selectedRecord_();
    const QVariantList metrics = selectedResultMetrics();
    if (record == nullptr || metrics.empty()) return selectedResultMetricKey_;
    for (const QVariant& value : metrics) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("key")).toString() == selectedResultMetricKey_) return selectedResultMetricKey_;
    }
    return metrics.front().toMap().value(QStringLiteral("key")).toString();
}

QVariantList BacktestViewModel::selectedResultMetricSeries() const {
    const auto* record = selectedRecord_();
    const QVariantList points = selectedEquityPoints();
    if (record == nullptr || !record->detailsLoaded || points.size() < 2) return {};

    const QString metricKey = selectedResultMetricKey();
    const QString ratioKey = selectedResultMetricRatioKey_;
    QVariantList out;
    qint64 peakPnl = std::numeric_limits<qint64>::min();
    qint64 bestPnl = std::numeric_limits<qint64>::min();
    qint64 worstPnl = std::numeric_limits<qint64>::max();

    auto valueFor = [](const QString& key, const QVariantMap& point, qint64 drawdown, qint64 mfe, qint64 mae, bool& ok) -> qint64 {
        ok = true;
        if (key == QStringLiteral("net_realized_pnl_e8") || key == QStringLiteral("realized_pnl_e8")) return point.value(QStringLiteral("realizedPnlE8")).toLongLong();
        if (key == QStringLiteral("total_pnl_e8")) return point.value(QStringLiteral("totalPnlE8")).toLongLong();
        if (key == QStringLiteral("fees_paid_e8")) return point.value(QStringLiteral("feesPaidE8")).toLongLong();
        if (key == QStringLiteral("wallet_balance_e8")) return point.value(QStringLiteral("walletBalanceE8")).toLongLong();
        if (key == QStringLiteral("max_drawdown_e8")) return drawdown;
        if (key == QStringLiteral("mfe_e8")) return mfe;
        if (key == QStringLiteral("mae_e8")) return mae;
        if (key == QStringLiteral("fills")) return point.value(QStringLiteral("fillCount")).toLongLong();
        ok = false;
        return 0;
    };

    for (const QVariant& value : points) {
        const QVariantMap point = value.toMap();
        const QVariant totalPnlValue = point.value(QStringLiteral("totalPnlE8"));
        const qint64 pnl = totalPnlValue.isValid()
                                ? totalPnlValue.toLongLong()
                                : point.value(QStringLiteral("netTotalPnlE8")).toLongLong();
        peakPnl = peakPnl == std::numeric_limits<qint64>::min() ? pnl : std::max(peakPnl, pnl);
        bestPnl = bestPnl == std::numeric_limits<qint64>::min() ? pnl : std::max(bestPnl, pnl);
        worstPnl = worstPnl == std::numeric_limits<qint64>::max() ? pnl : std::min(worstPnl, pnl);
        const qint64 drawdown = peakPnl - pnl;

        bool hasValue = false;
        const qint64 raw = valueFor(metricKey, point, drawdown, bestPnl, worstPnl, hasValue);
        if (!hasValue) return {};

        QVariantMap row;
        row.insert(QStringLiteral("index"), out.size());
        row.insert(QStringLiteral("tsNs"), point.value(QStringLiteral("tsNs")));
        row.insert(QStringLiteral("valueRaw"), raw);
        row.insert(QStringLiteral("valueText"), isE8Key(metricKey) ? e8DisplayString(raw) : QString::number(raw));
        if (!ratioKey.isEmpty() && ratioKey != metricKey) {
            bool hasDenominator = false;
            const qint64 denominator = valueFor(ratioKey, point, drawdown, bestPnl, worstPnl, hasDenominator);
            if (hasDenominator && denominator != 0) {
                row.insert(QStringLiteral("denominatorRaw"), denominator);
                row.insert(QStringLiteral("hasRatio"), true);
            }
        }
        out.push_back(row);
    }
    return out;
}

QVariantList BacktestViewModel::resultMetricRatioChoices() const {
    QVariantList out;
    QVariantMap none;
    none.insert(QStringLiteral("id"), QString{});
    none.insert(QStringLiteral("label"), QStringLiteral("None"));
    out.push_back(none);
    const QVariantList metrics = selectedResultMetrics();
    for (const QVariant& value : metrics) {
        const QVariantMap metric = value.toMap();
        if (!metric.value(QStringLiteral("hasSeries")).toBool()) continue;
        QVariantMap row;
        row.insert(QStringLiteral("id"), metric.value(QStringLiteral("key")).toString());
        row.insert(QStringLiteral("label"), metric.value(QStringLiteral("label")).toString());
        out.push_back(row);
    }
    return out;
}

QVariantList BacktestViewModel::selectedSweepRows() const {
    const auto* record = selectedRecord_();
    if (record == nullptr || !record->sweep || !record->detailsLoaded) return {};
    QVariantList out;
    out.reserve(record->sweepRows.size());
    const QString metricKey = selectedSweepMetric_.isEmpty() ? QStringLiteral("total_pnl_e8") : selectedSweepMetric_;
    for (const QVariant& value : record->sweepRows) out.push_back(sweepMapForMetric(value.toMap(), metricKey));
    std::sort(out.begin(), out.end(), [](const QVariant& lhs, const QVariant& rhs) {
        return lhs.toMap().value(QStringLiteral("metricRaw")).toLongLong() > rhs.toMap().value(QStringLiteral("metricRaw")).toLongLong();
    });
    return out;
}

QVariantList BacktestViewModel::selectedSweepDistributionParamChoices() const {
    const auto* record = selectedRecord_();
    if (record == nullptr || !record->sweep || !record->detailsLoaded) return {};
    QStringList keys = record->sweepParamKeys;
    appendSweepParamKeysFromRows(record->sweepRows, keys);
    QVariantList out;
    for (const QString& key : keys) {
        QVariantMap row;
        row.insert(QStringLiteral("id"), key);
        row.insert(QStringLiteral("label"), key);
        out.push_back(row);
    }
    return out;
}

QString BacktestViewModel::selectedSweepDistributionParam() const {
    const auto* record = selectedRecord_();
    if (record == nullptr || !record->sweep || !record->detailsLoaded) return {};
    for (const QString& key : record->sweepParamKeys) {
        if (key == selectedSweepDistributionParam_) return key;
    }
    return record->sweepParamKeys.empty() ? QString{} : record->sweepParamKeys.front();
}

QVariantList BacktestViewModel::selectedSweepCurves() const {
    const auto* record = selectedRecord_();
    if (record == nullptr || !record->sweep || !record->detailsLoaded) return {};
    QVariantList out;
    out.reserve(record->sweepCurves.size());
    const QString metricKey = selectedSweepMetric_.isEmpty() ? QStringLiteral("total_pnl_e8") : selectedSweepMetric_;
    for (const QVariant& value : record->sweepCurves) out.push_back(sweepMapForMetric(value.toMap(), metricKey));
    std::sort(out.begin(), out.end(), [](const QVariant& lhs, const QVariant& rhs) {
        return lhs.toMap().value(QStringLiteral("metricRaw")).toLongLong() > rhs.toMap().value(QStringLiteral("metricRaw")).toLongLong();
    });
    const int limit = selectedSweepCurveLimit_ == QStringLiteral("all") ? out.size() : selectedSweepCurveLimit_.toInt();
    while (out.size() > limit) out.removeLast();
    return out;
}

QVariantList BacktestViewModel::selectedSweepDistributionBars() const {
    const auto* record = selectedRecord_();
    if (record == nullptr || !record->sweep || !record->detailsLoaded) return {};
    const QString paramKey = selectedSweepDistributionParam();
    if (paramKey.isEmpty()) return {};
    const QString metricKey = selectedSweepMetric_.isEmpty() ? QStringLiteral("total_pnl_e8") : selectedSweepMetric_;
    QVariantList out;
    const QVariantList rows = selectedSweepRows();
    for (const QVariant& value : rows) {
        const QVariantMap row = value.toMap();
        const QVariantMap params = row.value(QStringLiteral("params")).toMap();
        if (!params.contains(paramKey)) continue;
        QVariantMap bar = row;
        const qint64 metricRaw = row.value(QStringLiteral("metricRaw")).toLongLong();
        const qint64 paramRaw = params.value(paramKey).toLongLong();
        bar.insert(QStringLiteral("paramKey"), paramKey);
        bar.insert(QStringLiteral("paramRaw"), paramRaw);
        bar.insert(QStringLiteral("paramText"), QString::number(paramRaw));
        bar.insert(QStringLiteral("metricKey"), metricKey);
        bar.insert(QStringLiteral("metricRaw"), metricRaw);
        bar.insert(QStringLiteral("metricText"), e8DisplayString(metricRaw));
        out.push_back(bar);
    }
    std::sort(out.begin(), out.end(), [](const QVariant& lhs, const QVariant& rhs) {
        const QVariantMap left = lhs.toMap();
        const QVariantMap right = rhs.toMap();
        const qint64 leftParam = left.value(QStringLiteral("paramRaw")).toLongLong();
        const qint64 rightParam = right.value(QStringLiteral("paramRaw")).toLongLong();
        if (leftParam != rightParam) return leftParam < rightParam;
        const qint64 leftMetric = left.value(QStringLiteral("metricRaw")).toLongLong();
        const qint64 rightMetric = right.value(QStringLiteral("metricRaw")).toLongLong();
        if (leftMetric != rightMetric) return leftMetric > rightMetric;
        return left.value(QStringLiteral("pointId")).toLongLong() < right.value(QStringLiteral("pointId")).toLongLong();
    });
    return out;
}

bool BacktestViewModel::selectedIsSweep() const {
    const auto* record = selectedRecord_();
    return record != nullptr && record->sweep;
}

bool BacktestViewModel::hasEquityPoints() const {
    return !selectedEquityPoints().empty();
}

bool BacktestViewModel::selectedPreviewLoading() const {
    return previewLoading_ && !selectedRunId_.isEmpty() && selectedRunId_ == previewLoadingRunId_;
}

bool BacktestViewModel::selectedDetailsLoaded() const {
    const auto* record = selectedRecord_();
    return record != nullptr && record->detailsLoaded;
}

bool BacktestViewModel::selectedDetailsLoading() const {
    return detailsLoading_ && !selectedRunId_.isEmpty() && selectedRunId_ == detailsLoadingRunId_;
}

qint64 BacktestViewModel::selectedInitialBalanceE8() const {
    const auto* record = selectedRecord_();
    if (record == nullptr) return 0;
    const QString scope = effectiveResultScopeId_(*record);
    return record->scopedInitialBalanceE8.value(scope, record->initialBalanceE8);
}

qint64 BacktestViewModel::selectedPnlMinE8() const {
    const auto* record = selectedRecord_();
    if (record == nullptr) return 0;
    const QString scope = effectiveResultScopeId_(*record);
    return record->scopedPnlMinE8.value(scope, record->pnlMinE8);
}

qint64 BacktestViewModel::selectedPnlMaxE8() const {
    const auto* record = selectedRecord_();
    if (record == nullptr) return 0;
    const QString scope = effectiveResultScopeId_(*record);
    return record->scopedPnlMaxE8.value(scope, record->pnlMaxE8);
}

bool BacktestViewModel::canRun() const {
    return !running_ && !selectedSessionPath().trimmed().isEmpty() && !selectedStrategy_.trimmed().isEmpty() && strategySupportsSelectedSessionCount_();
}

void BacktestViewModel::reloadSessions() {
    sessions_ = loadSessions_();
    const bool strategyChanged = ensureSelectedStrategySupportsSessionCount_();
    emit sessionsChanged();
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

void BacktestViewModel::setRiskMinEquityPct(const QString& value) {
    const QString next = value.trimmed();
    if (riskMinEquityPct_ == next) return;
    riskMinEquityPct_ = next;
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
    out << "risk_min_equity_pct=" << riskMinEquityPct_ << "\n";
    out << "maker_fee_bps=" << makerFeeBps_ << "\n";
    out << "taker_fee_bps=" << takerFeeBps_ << "\n";
    out << "sweep_budget=" << sweepBudget_ << "\n";
    out << "sweep_seed=" << sweepSeed_ << "\n";
    out << "config_mode=" << configMode_ << "\n\n";
    QSet<QString> savedVenueKeys;
    for (const QString& sessionPath : selectedSessionPaths_()) {
        const QString venueKey = venueExecutionKey(sessionPath);
        if (venueKey.isEmpty() || savedVenueKeys.contains(venueKey)) continue;
        savedVenueKeys.insert(venueKey);
        out << "[venue_execution." << venueExecutionSettingKey(venueKey) << "]\n";
        out << "initial_balance_usdt=" << venueExecutionValue_(venueKey, QStringLiteral("initial_balance_usdt"), initialBalanceUsdt_) << "\n";
        out << "maker_fee_bps=" << venueExecutionValue_(venueKey, QStringLiteral("maker_fee_bps"), makerFeeBps_) << "\n";
        out << "taker_fee_bps=" << venueExecutionValue_(venueKey, QStringLiteral("taker_fee_bps"), takerFeeBps_) << "\n";
        out << "market_data_latency_us=" << venueExecutionValue_(venueKey, QStringLiteral("market_data_latency_us"), marketDataLatencyUs_) << "\n";
        out << "market_data_jitter_us=" << venueExecutionValue_(venueKey, QStringLiteral("market_data_jitter_us"), marketDataJitterUs_) << "\n";
        out << "market_order_latency_us=" << venueExecutionValue_(venueKey, QStringLiteral("market_order_latency_us"), marketOrderLatencyUs_) << "\n";
        out << "market_order_jitter_us=" << venueExecutionValue_(venueKey, QStringLiteral("market_order_jitter_us"), marketOrderJitterUs_) << "\n";
        out << "limit_order_latency_us=" << venueExecutionValue_(venueKey, QStringLiteral("limit_order_latency_us"), limitOrderLatencyUs_) << "\n";
        out << "limit_order_jitter_us=" << venueExecutionValue_(venueKey, QStringLiteral("limit_order_jitter_us"), limitOrderJitterUs_) << "\n";
        out << "cancel_order_latency_us=" << venueExecutionValue_(venueKey, QStringLiteral("cancel_order_latency_us"), cancelOrderLatencyUs_) << "\n";
        out << "cancel_order_jitter_us=" << venueExecutionValue_(venueKey, QStringLiteral("cancel_order_jitter_us"), cancelOrderJitterUs_) << "\n";
        out << "user_data_latency_us=" << venueExecutionValue_(venueKey, QStringLiteral("user_data_latency_us"), userDataLatencyUs_) << "\n";
        out << "user_data_jitter_us=" << venueExecutionValue_(venueKey, QStringLiteral("user_data_jitter_us"), userDataJitterUs_) << "\n\n";
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
    const QString riskMinEquity = iniValue(text, QStringLiteral("backtest"), QStringLiteral("risk_min_equity_pct"));
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
    if (!cancelOrderLatency.isEmpty()) cancelOrderLatencyUs_ = cancelOrderLatency;
    else if (!cancelLatency.isEmpty()) cancelOrderLatencyUs_ = cancelLatency;
    else cancelOrderLatencyUs_ = limitOrderLatencyUs_;
    if (!cancelOrderJitter.isEmpty()) cancelOrderJitterUs_ = cancelOrderJitter;
    else cancelOrderJitterUs_ = limitOrderJitterUs_;
    if (!userDataLatency.isEmpty()) userDataLatencyUs_ = userDataLatency;
    if (!userDataJitter.isEmpty()) userDataJitterUs_ = userDataJitter;
    if (!initialBalance.isEmpty()) initialBalanceUsdt_ = initialBalance;
    riskMinEquityPct_ = riskMinEquity;
    if (!makerFee.isEmpty()) makerFeeBps_ = makerFee;
    if (!takerFee.isEmpty()) takerFeeBps_ = takerFee;
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
    emit multiSessionChanged();
    emit sweepConfigChanged();
    emit configChanged();
    emit strategyParametersChanged();
}

void BacktestViewModel::refresh() {
    refreshTimer_.stop();
    ++previewLoadGeneration_;
    setPreviewLoading_(false);
    if (!pendingDetailsRunId_.isEmpty()) {
        pendingDetailsRunId_.clear();
        setDetailsLoading_(false);
    }
    updateWatcher_();
    std::vector<RunRecord> next;
    const QString dirPath = backtestsDirectory();
    QStringList filesToWatch;
    if (!dirPath.isEmpty()) {
        const QStringList selectedPaths = selectedSessionPaths_();
        const QString primarySessionId = selectedPaths.empty() ? QString{} : sessionIdFromPath_(selectedPaths.at(0));
        const QString secondarySessionId = selectedPaths.size() > 1 ? sessionIdFromPath_(selectedPaths.at(1)) : QString{};
        auto addRunDir = [&](const QFileInfo& runDir) {
            const QDir candidateDir(runDir.absoluteFilePath());
            const QString manifestPath = candidateDir.absoluteFilePath(QStringLiteral("manifest.json"));
            if (!QFileInfo::exists(manifestPath)) return;
            if (!backtestManifestMatchesLegs(manifestPath, primarySessionId, secondarySessionId)) return;
            const RunRecord* cached = recordForPath_(runDir.absoluteFilePath());
            RunRecord record = loadRecord_(runDir.absoluteFilePath(), RecordLoadMode::MetadataOnly);
            if (!record.strategy.isEmpty() && record.strategy != selectedStrategy_) return;
            const bool metadataMatches = cached != nullptr
                && cached->manifestPath == manifestPath
                && fileStampMatches_(manifestPath, cached->manifestModifiedMs, cached->manifestSize);
            const bool equityMatches = metadataMatches
                && record.equityPath == cached->equityPath
                && fileStampMatches_(cached->equityPath, cached->equityModifiedMs, cached->equitySize);
            if (equityMatches) {
                record.equityPoints = cached->equityPoints;
                record.resultScopes = cached->resultScopes;
                record.scopedEquityPoints = cached->scopedEquityPoints;
                record.scopedResultMetrics = cached->scopedResultMetrics;
                record.scopedInitialBalanceE8 = cached->scopedInitialBalanceE8;
                record.scopedPnlMinE8 = cached->scopedPnlMinE8;
                record.scopedPnlMaxE8 = cached->scopedPnlMaxE8;
                record.pnlMinE8 = cached->pnlMinE8;
                record.pnlMaxE8 = cached->pnlMaxE8;
            }
            const bool sweepMatches = metadataMatches
                && cached->detailsLoaded
                && record.sweepRowsPath == cached->sweepRowsPath
                && record.sweepCurvesPath == cached->sweepCurvesPath
                && fileStampMatches_(cached->sweepRowsPath, cached->sweepRowsModifiedMs, cached->sweepRowsSize)
                && fileStampMatches_(cached->sweepCurvesPath, cached->sweepCurvesModifiedMs, cached->sweepCurvesSize);
            if (cached != nullptr && cached->detailsLoaded && ((!record.sweep && equityMatches) || (record.sweep && sweepMatches))) {
                record.sweepRows = cached->sweepRows;
                record.sweepCurves = cached->sweepCurves;
                record.sweepParamKeys = cached->sweepParamKeys;
                record.detailsErrorText = cached->detailsErrorText;
                if (!cached->sweepRows.empty() || !cached->sweepCurves.empty()) {
                    record.initialBalanceE8 = cached->initialBalanceE8;
                    record.totalPnlE8 = cached->totalPnlE8;
                    record.pnlText = cached->pnlText;
                }
                record.detailsLoaded = true;
            }
            if (!record.manifestPath.isEmpty()) filesToWatch.push_back(record.manifestPath);
            next.push_back(std::move(record));
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
    if (const RunRecord* selected = selectedRecord_()) {
        if (!selected->equityPath.isEmpty()) filesToWatch.push_back(selected->equityPath);
        if (selected->detailsLoaded) {
            if (!selected->sweepRowsPath.isEmpty()) filesToWatch.push_back(selected->sweepRowsPath);
            if (!selected->sweepCurvesPath.isEmpty()) filesToWatch.push_back(selected->sweepCurvesPath);
        }
    }

    if (!running_) {
        if (selectedSessionPath().isEmpty()) setStatusText_(QStringLiteral("Select a session and strategy"));
        else setStatusText_(QStringLiteral("Watching %1 result%2").arg(static_cast<qulonglong>(records_.size())).arg(records_.size() == 1u ? QString{} : QStringLiteral("s")));
    }
    const QStringList watchedFiles = watcher_.files();
    if (!watchedFiles.empty()) {
        QStringList filesToRemove;
        for (const QString& file : watchedFiles) {
            if (!filesToWatch.contains(file)) filesToRemove.push_back(file);
        }
        if (!filesToRemove.empty()) (void)watcher_.removePaths(filesToRemove);
    }
    if (!filesToWatch.empty()) {
        const QStringList currentFiles = watcher_.files();
        QStringList filesToAdd;
        for (const QString& file : filesToWatch) {
            if (!QFileInfo::exists(file) || currentFiles.contains(file)) continue;
            filesToAdd.push_back(file);
        }
        if (!filesToAdd.empty()) (void)watcher_.addPaths(filesToAdd);
    }
    emit runsChanged();
    emit selectionChanged();
    emit selectedResultMetricChanged();
    ensureSelectedPreviewLoaded_();
}

void BacktestViewModel::selectRun(const QString& runId) {
    if (selectedRunId_ == runId) return;
    if (RunRecord* oldRecord = mutableRecordForRunId_(selectedRunId_)) clearRecordDetails_(*oldRecord);
    ++previewLoadGeneration_;
    ++detailsLoadGeneration_;
    pendingDetailsRunId_.clear();
    selectedDetailsErrorText_.clear();
    setPreviewLoading_(false);
    setDetailsLoading_(false);
    selectedRunId_ = runId;
    emit selectionChanged();
    emit selectedResultMetricChanged();
    emit detailsLoadingChanged();
    ensureSelectedPreviewLoaded_();
}

void BacktestViewModel::loadSelectedRunDetails() {
    RunRecord* record = mutableRecordForRunId_(selectedRunId_);
    if (record == nullptr || record->detailsLoaded || selectedDetailsLoading()) return;
    const QString runId = record->runId;
    const QString filePath = record->filePath;
    if (!record->sweep && !record->equityPoints.empty()) {
        record->detailsLoaded = true;
        emit runsChanged();
        emit selectionChanged();
        emit selectedResultMetricChanged();
        return;
    }
    if (!record->sweep && selectedPreviewLoading()) {
        pendingDetailsRunId_ = runId;
        setDetailsLoading_(true, runId);
        return;
    }
    const std::uint64_t generation = ++detailsLoadGeneration_;
    selectedDetailsErrorText_.clear();
    setDetailsLoading_(true, runId);

    QPointer<BacktestViewModel> self(this);
    std::thread([self, generation, runId, filePath]() {
        RunRecord loaded = BacktestViewModel::loadRecord_(filePath, RecordLoadMode::Details);
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, generation, runId, loaded = std::move(loaded)]() {
            if (self) self->applyLoadedDetails_(generation, runId, loaded);
        }, Qt::QueuedConnection);
    }).detach();
}

void BacktestViewModel::unloadSelectedRunDetails() {
    RunRecord* record = mutableRecordForRunId_(selectedRunId_);
    if (record == nullptr) return;
    clearRecordDetails_(*record);
    selectedDetailsErrorText_.clear();
    ++detailsLoadGeneration_;
    pendingDetailsRunId_.clear();
    setDetailsLoading_(false);
    emit selectionChanged();
    emit selectedResultMetricChanged();
    emit detailsLoadingChanged();
}

void BacktestViewModel::setSelectedResultScope(const QString& scope) {
    const QString next = scope.trimmed().isEmpty() ? QStringLiteral("portfolio") : scope.trimmed();
    if (selectedResultScope_ == next) return;
    selectedResultScope_ = next;
    if (selectedResultMetricRatioKey_ == selectedResultMetricKey_) selectedResultMetricRatioKey_.clear();
    emit selectedResultScopeChanged();
    emit selectionChanged();
    emit selectedResultMetricChanged();
}

void BacktestViewModel::setSelectedResultMetricKey(const QString& key) {
    const QString next = key.trimmed();
    if (selectedResultMetricKey_ == next) return;
    selectedResultMetricKey_ = next;
    if (selectedResultMetricRatioKey_ == selectedResultMetricKey_) selectedResultMetricRatioKey_.clear();
    emit selectedResultMetricChanged();
}

void BacktestViewModel::setSelectedResultMetricRatioKey(const QString& key) {
    QString next = key.trimmed();
    if (next == selectedResultMetricKey()) next.clear();
    if (selectedResultMetricRatioKey_ == next) return;
    selectedResultMetricRatioKey_ = next;
    emit selectedResultMetricChanged();
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
    reloadSessions();
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
    const QStringList sessionPaths = selectedSessionPaths_();
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
    const quint64 cancelOrderLatency = latencyValue_(cancelOrderLatencyUs_, limitOrderLatency);
    const quint64 cancelOrderJitter = latencyValue_(cancelOrderJitterUs_, limitOrderJitter);
    const quint64 userDataLatency = latencyValue_(userDataLatencyUs_, 0);
    const quint64 userDataJitter = latencyValue_(userDataJitterUs_, 0);
    const quint64 orderLatency = marketOrderLatency;
    const quint64 cancelLatency = cancelOrderLatency;
    const qint64 initialBalance = decimalE8Value_(initialBalanceUsdt_, 0);
    const qint64 makerFee = decimalE8Value_(makerFeeBps_, 0);
    const qint64 takerFee = decimalE8Value_(takerFeeBps_, 0);
    std::vector<std::int64_t> legInitialBalances;
    std::vector<hft_backtest::BacktestFeeSchedule> feeSchedules;
    std::vector<hft_backtest::BacktestLatencySchedule> latencySchedules;
    const std::vector<QVariantMap> venueRows = venueExecutionRows_();
    legInitialBalances.reserve(venueRows.size());
    feeSchedules.reserve(venueRows.size());
    latencySchedules.reserve(venueRows.size());
    for (const QVariantMap& row : venueRows) {
        const QString exchange = row.value(QStringLiteral("exchange")).toString();
        const QString market = row.value(QStringLiteral("market")).toString();
        if (exchange.isEmpty() || market.isEmpty()) continue;
        legInitialBalances.push_back(decimalE8Value_(row.value(QStringLiteral("initialBalanceUsdt")).toString(), initialBalance));
        hft_backtest::BacktestFeeSchedule fee{};
        fee.exchange = exchange.toStdString();
        fee.market = market.toStdString();
        fee.makerFeeBpsE8 = decimalE8Value_(row.value(QStringLiteral("makerFeeBps")).toString(), makerFee);
        fee.takerFeeBpsE8 = decimalE8Value_(row.value(QStringLiteral("takerFeeBps")).toString(), takerFee);
        feeSchedules.push_back(std::move(fee));
        hft_backtest::BacktestLatencySchedule latency{};
        latency.exchange = exchange.toStdString();
        latency.market = market.toStdString();
        latency.marketData.baseUs = latencyValue_(row.value(QStringLiteral("marketDataLatencyUs")).toString(), marketDataLatency);
        latency.marketData.jitterUs = latencyValue_(row.value(QStringLiteral("marketDataJitterUs")).toString(), marketDataJitter);
        latency.marketOrder.baseUs = latencyValue_(row.value(QStringLiteral("marketOrderLatencyUs")).toString(), marketOrderLatency);
        latency.marketOrder.jitterUs = latencyValue_(row.value(QStringLiteral("marketOrderJitterUs")).toString(), marketOrderJitter);
        latency.limitOrder.baseUs = latencyValue_(row.value(QStringLiteral("limitOrderLatencyUs")).toString(), limitOrderLatency);
        latency.limitOrder.jitterUs = latencyValue_(row.value(QStringLiteral("limitOrderJitterUs")).toString(), limitOrderJitter);
        latency.cancelOrder.baseUs = latencyValue_(row.value(QStringLiteral("cancelOrderLatencyUs")).toString(), cancelOrderLatency);
        latency.cancelOrder.jitterUs = latencyValue_(row.value(QStringLiteral("cancelOrderJitterUs")).toString(), cancelOrderJitter);
        latency.userData.baseUs = latencyValue_(row.value(QStringLiteral("userDataLatencyUs")).toString(), userDataLatency);
        latency.userData.jitterUs = latencyValue_(row.value(QStringLiteral("userDataJitterUs")).toString(), userDataJitter);
        latencySchedules.push_back(std::move(latency));
    }
    const QString indicatorProfile = selectedIndicatorProfile_;
    worker_ = std::thread([this, sessionPath, sessionPaths, strategy, runId, configPath, indicatorProfile, latencySeed, marketDataLatency, marketDataJitter, marketOrderLatency, marketOrderJitter, limitOrderLatency, limitOrderJitter, cancelOrderLatency, cancelOrderJitter, userDataLatency, userDataJitter, orderLatency, cancelLatency, initialBalance, makerFee, takerFee, legInitialBalances = std::move(legInitialBalances), feeSchedules = std::move(feeSchedules), latencySchedules = std::move(latencySchedules)] {
        try {
        hft_backtest::BacktestRunRequest request{};
        request.sessionPath = sessionPath.toStdString();
        if (sessionPaths.size() > 1) {
            request.sessions.reserve(static_cast<std::size_t>(sessionPaths.size() - 1));
            for (qsizetype i = 1; i < sessionPaths.size(); ++i) {
                const QString& path = sessionPaths.at(i);
                hft_backtest::BacktestSessionRequest leg{};
                leg.path = path.toStdString();
                leg.venue = venueSectionForSession(path).toStdString();
                leg.symbol = symbolForSessionPath(path).toStdString();
                request.sessions.push_back(std::move(leg));
            }
        }
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
        request.cancelOrderLatency.baseUs = cancelOrderLatency;
        request.cancelOrderLatency.jitterUs = cancelOrderJitter;
        request.userDataLatency.baseUs = userDataLatency;
        request.userDataLatency.jitterUs = userDataJitter;
        request.orderLatencyUs = orderLatency;
        request.cancelLatencyUs = cancelLatency;
        request.initialBalanceE8 = initialBalance;
        request.legInitialBalancesE8 = legInitialBalances;
        request.makerFeeBpsE8 = makerFee;
        request.takerFeeBpsE8 = takerFee;
        request.feeSchedules = feeSchedules;
        request.latencySchedules = latencySchedules;
        request.outputPath = (QDir(sessionPath).absoluteFilePath(QStringLiteral("backtests/%1").arg(runId))).toStdString();

        const auto result = hft_backtest::runBacktest(request, progressCallback, this);
        const QString status = statusTextFor(result.status, result.error, result.warnings);
        const QString selected = QString::fromStdString(result.runId);
        QMetaObject::invokeMethod(this, [this, status, selected] {
            setRunning_(false);
            setProgress_(100, status);
            setStatusText_(status);
            if (!selected.isEmpty()) selectedRunId_ = selected;
            refresh();
            reloadSessions();
        }, Qt::QueuedConnection);
        } catch (const std::exception& ex) {
            const QString status = QStringLiteral("Backtest crashed: ") + QString::fromUtf8(ex.what());
            QMetaObject::invokeMethod(this, [this, status] {
                setRunning_(false);
                setProgress_(100, status);
                setStatusText_(status);
                refresh();
            }, Qt::QueuedConnection);
        } catch (...) {
            const QString status = QStringLiteral("Backtest crashed: unknown exception");
            QMetaObject::invokeMethod(this, [this, status] {
                setRunning_(false);
                setProgress_(100, status);
                setStatusText_(status);
                refresh();
            }, Qt::QueuedConnection);
        }
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
    const QStringList sessionPaths = selectedSessionPaths_();
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
    const quint64 cancelOrderLatency = latencyValue_(cancelOrderLatencyUs_, limitOrderLatency);
    const quint64 cancelOrderJitter = latencyValue_(cancelOrderJitterUs_, limitOrderJitter);
    const quint64 userDataLatency = latencyValue_(userDataLatencyUs_, 0);
    const quint64 userDataJitter = latencyValue_(userDataJitterUs_, 0);
    const qint64 initialBalance = decimalE8Value_(initialBalanceUsdt_, 0);
    const qint64 makerFee = decimalE8Value_(makerFeeBps_, 0);
    const qint64 takerFee = decimalE8Value_(takerFeeBps_, 0);
    std::vector<std::int64_t> legInitialBalances;
    std::vector<hft_backtest::BacktestFeeSchedule> feeSchedules;
    std::vector<hft_backtest::BacktestLatencySchedule> latencySchedules;
    const std::vector<QVariantMap> venueRows = venueExecutionRows_();
    legInitialBalances.reserve(venueRows.size());
    feeSchedules.reserve(venueRows.size());
    latencySchedules.reserve(venueRows.size());
    for (const QVariantMap& row : venueRows) {
        const QString exchange = row.value(QStringLiteral("exchange")).toString();
        const QString market = row.value(QStringLiteral("market")).toString();
        if (exchange.isEmpty() || market.isEmpty()) continue;
        legInitialBalances.push_back(decimalE8Value_(row.value(QStringLiteral("initialBalanceUsdt")).toString(), initialBalance));
        hft_backtest::BacktestFeeSchedule fee{};
        fee.exchange = exchange.toStdString();
        fee.market = market.toStdString();
        fee.makerFeeBpsE8 = decimalE8Value_(row.value(QStringLiteral("makerFeeBps")).toString(), makerFee);
        fee.takerFeeBpsE8 = decimalE8Value_(row.value(QStringLiteral("takerFeeBps")).toString(), takerFee);
        feeSchedules.push_back(std::move(fee));
        hft_backtest::BacktestLatencySchedule latency{};
        latency.exchange = exchange.toStdString();
        latency.market = market.toStdString();
        latency.marketData.baseUs = latencyValue_(row.value(QStringLiteral("marketDataLatencyUs")).toString(), marketDataLatency);
        latency.marketData.jitterUs = latencyValue_(row.value(QStringLiteral("marketDataJitterUs")).toString(), marketDataJitter);
        latency.marketOrder.baseUs = latencyValue_(row.value(QStringLiteral("marketOrderLatencyUs")).toString(), marketOrderLatency);
        latency.marketOrder.jitterUs = latencyValue_(row.value(QStringLiteral("marketOrderJitterUs")).toString(), marketOrderJitter);
        latency.limitOrder.baseUs = latencyValue_(row.value(QStringLiteral("limitOrderLatencyUs")).toString(), limitOrderLatency);
        latency.limitOrder.jitterUs = latencyValue_(row.value(QStringLiteral("limitOrderJitterUs")).toString(), limitOrderJitter);
        latency.cancelOrder.baseUs = latencyValue_(row.value(QStringLiteral("cancelOrderLatencyUs")).toString(), cancelOrderLatency);
        latency.cancelOrder.jitterUs = latencyValue_(row.value(QStringLiteral("cancelOrderJitterUs")).toString(), cancelOrderJitter);
        latency.userData.baseUs = latencyValue_(row.value(QStringLiteral("userDataLatencyUs")).toString(), userDataLatency);
        latency.userData.jitterUs = latencyValue_(row.value(QStringLiteral("userDataJitterUs")).toString(), userDataJitter);
        latencySchedules.push_back(std::move(latency));
    }
    const QString indicatorProfile = selectedIndicatorProfile_;

    worker_ = std::thread([this, sessionPath, sessionPaths, strategy, runId, configPath, indicatorProfile, latencySeed, searchSeed, runBudget, marketDataLatency, marketDataJitter, marketOrderLatency, marketOrderJitter, limitOrderLatency, limitOrderJitter, cancelOrderLatency, cancelOrderJitter, userDataLatency, userDataJitter, initialBalance, makerFee, takerFee, legInitialBalances = std::move(legInitialBalances), feeSchedules = std::move(feeSchedules), latencySchedules = std::move(latencySchedules), ranges = std::move(ranges)] {
        try {
        hft_backtest::BacktestSweepRequest request{};
        request.baseRun.sessionPath = sessionPath.toStdString();
        if (sessionPaths.size() > 1) {
            request.baseRun.sessions.reserve(static_cast<std::size_t>(sessionPaths.size() - 1));
            for (qsizetype i = 1; i < sessionPaths.size(); ++i) {
                const QString& path = sessionPaths.at(i);
                hft_backtest::BacktestSessionRequest leg{};
                leg.path = path.toStdString();
                leg.venue = venueSectionForSession(path).toStdString();
                leg.symbol = symbolForSessionPath(path).toStdString();
                request.baseRun.sessions.push_back(std::move(leg));
            }
        }
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
        request.baseRun.cancelOrderLatency.baseUs = cancelOrderLatency;
        request.baseRun.cancelOrderLatency.jitterUs = cancelOrderJitter;
        request.baseRun.userDataLatency.baseUs = userDataLatency;
        request.baseRun.userDataLatency.jitterUs = userDataJitter;
        request.baseRun.orderLatencyUs = marketOrderLatency;
        request.baseRun.cancelLatencyUs = cancelOrderLatency;
        request.baseRun.initialBalanceE8 = initialBalance;
        request.baseRun.legInitialBalancesE8 = legInitialBalances;
        request.baseRun.makerFeeBpsE8 = makerFee;
        request.baseRun.takerFeeBpsE8 = takerFee;
        request.baseRun.feeSchedules = feeSchedules;
        request.baseRun.latencySchedules = latencySchedules;
        request.baseRun.writeArtifacts = false;
        request.sweepId = runId.toStdString();
        request.runBudget = runBudget;
        request.searchSeed = searchSeed;
        request.outputPath = (QDir(sessionPath).absoluteFilePath(QStringLiteral("backtests/sweeps/%1").arg(runId))).toStdString();
        request.ranges = ranges;

        const auto result = hft_backtest::runBacktestSweep(request, progressCallback, this);
        const QString status = sweepStatusTextFor(result.status, result.error);
        const QString selected = QString::fromStdString(result.sweepId);
        QMetaObject::invokeMethod(this, [this, status, selected] {
            setRunning_(false);
            setProgress_(100, status);
            setStatusText_(status);
            if (!selected.isEmpty()) selectedRunId_ = selected;
            refresh();
            reloadSessions();
        }, Qt::QueuedConnection);
        } catch (const std::exception& ex) {
            const QString status = QStringLiteral("Sweep crashed: ") + QString::fromUtf8(ex.what());
            QMetaObject::invokeMethod(this, [this, status] {
                setRunning_(false);
                setProgress_(100, status);
                setStatusText_(status);
                refresh();
            }, Qt::QueuedConnection);
        } catch (...) {
            const QString status = QStringLiteral("Sweep crashed: unknown exception");
            QMetaObject::invokeMethod(this, [this, status] {
                setRunning_(false);
                setProgress_(100, status);
                setStatusText_(status);
                refresh();
            }, Qt::QueuedConnection);
        }
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
        startBacktestWithOverrides_(overrides, QStringLiteral("detail-p%1").arg(pointId));
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

BacktestViewModel::RunRecord BacktestViewModel::loadRecord_(const QString& filePath, RecordLoadMode mode) {
    RunRecord record;
    const QFileInfo info(filePath);
    const QDir runDir(info.absoluteFilePath());
    const QString manifestPath = runDir.absoluteFilePath(QStringLiteral("manifest.json"));
    record.filePath = info.absoluteFilePath();
    record.fileName = info.fileName();
    record.runId = info.fileName();
    record.modifiedMs = info.lastModified().toMSecsSinceEpoch();
    record.manifestPath = manifestPath;
    record.manifestModifiedMs = fileStampMs_(manifestPath, &record.manifestSize);
    record.modifiedMs = std::max(record.modifiedMs, record.manifestModifiedMs);

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
        record.sweepParamKeys = sweepParamKeysFromManifest(object);
        const QJsonObject rowsObject = object.value(QStringLiteral("rows")).toObject();
        QString rowsPath = rowsObject.value(QStringLiteral("path")).toString(QStringLiteral("sweep_results.jsonl"));
        const QFileInfo rowsInfo(rowsPath);
        if (rowsInfo.isRelative()) rowsPath = runDir.absoluteFilePath(rowsPath);
        record.sweepRowsPath = rowsPath;
        record.sweepRowsModifiedMs = fileStampMs_(rowsPath, &record.sweepRowsSize);
        record.modifiedMs = std::max(record.modifiedMs, record.sweepRowsModifiedMs);
        const QJsonObject curvesObject = object.value(QStringLiteral("curves")).toObject();
        QString curvesPath = curvesObject.value(QStringLiteral("path")).toString(QStringLiteral("sweep_curves.jsonl"));
        const QFileInfo curvesInfo(curvesPath);
        if (curvesInfo.isRelative()) curvesPath = runDir.absoluteFilePath(curvesPath);
        record.sweepCurvesPath = curvesPath;
        record.sweepCurvesModifiedMs = fileStampMs_(curvesPath, &record.sweepCurvesSize);
        record.modifiedMs = std::max(record.modifiedMs, record.sweepCurvesModifiedMs);
        if (mode == RecordLoadMode::Details) {
            record.sweepRows = sweepRowsFromJsonl(rowsPath, QStringLiteral("total_pnl_e8"));
            appendSweepParamKeysFromRows(record.sweepRows, record.sweepParamKeys);
            record.sweepCurves = sweepCurvesFromJsonl(curvesPath);
            if (!record.sweepRows.empty()) record.totalPnlE8 = record.sweepRows.front().toMap().value(QStringLiteral("totalPnlE8")).toLongLong();
            if (!record.sweepCurves.empty()) {
                const QVariantMap first = record.sweepCurves.front().toMap();
                record.initialBalanceE8 = first.value(QStringLiteral("initialBalanceE8")).toLongLong();
                record.totalPnlE8 = first.value(QStringLiteral("totalPnlE8")).toLongLong();
            }
            record.detailsLoaded = true;
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
    record.totalPnlE8 = summary.value(QStringLiteral("net_realized_pnl_e8")).toInteger(
        summary.value(QStringLiteral("realized_pnl_e8")).toInteger(summary.value(QStringLiteral("total_pnl_e8")).toInteger()));
    record.pnlText = pnlPercentText(record.totalPnlE8, record.initialBalanceE8);
    record.summaryJson = humanSummaryJson(object.value(QStringLiteral("summary")));
    const QJsonObject streams = object.value(QStringLiteral("streams")).toObject();
    const QJsonObject equityStream = streams.value(QStringLiteral("equity")).toObject();
    const qint64 equityRows = equityStream.value(QStringLiteral("rows")).toInteger();
    record.equityPath = runDir.absoluteFilePath(QStringLiteral("equity.jsonl"));
    record.equityModifiedMs = fileStampMs_(record.equityPath, &record.equitySize);
    record.modifiedMs = std::max(record.modifiedMs, record.equityModifiedMs);
    record.resultMetrics = resultMetrics(object, summary);
    QVariantMap portfolioScope;
    portfolioScope.insert(QStringLiteral("id"), QStringLiteral("portfolio"));
    portfolioScope.insert(QStringLiteral("label"), QStringLiteral("Portfolio"));
    portfolioScope.insert(QStringLiteral("initialBalanceE8"), record.initialBalanceE8);
    portfolioScope.insert(QStringLiteral("totalPnlE8"), record.totalPnlE8);
    record.resultScopes.push_back(portfolioScope);
    record.scopedResultMetrics.insert(QStringLiteral("portfolio"), record.resultMetrics);
    record.scopedInitialBalanceE8.insert(QStringLiteral("portfolio"), record.initialBalanceE8);
    if (mode != RecordLoadMode::MetadataOnly) {
        record.equityPoints = equityPointsFromJsonl(record.equityPath, summary, equityRows, record.pnlMinE8, record.pnlMaxE8);
        record.scopedEquityPoints.insert(QStringLiteral("portfolio"), record.equityPoints);
        record.scopedPnlMinE8.insert(QStringLiteral("portfolio"), record.pnlMinE8);
        record.scopedPnlMaxE8.insert(QStringLiteral("portfolio"), record.pnlMaxE8);
    }
    const QJsonArray legs = object.value(QStringLiteral("legs")).toArray();
    std::vector<QVariantList> legEquitySeries;
    if (mode != RecordLoadMode::MetadataOnly) legEquitySeries.reserve(static_cast<std::size_t>(legs.size()));
    for (int i = 0; i < legs.size(); ++i) {
        if (!legs.at(i).isObject()) continue;
        const QJsonObject leg = legs.at(i).toObject();
        const QString scopeId = QStringLiteral("leg_%1").arg(i);
        const QString exchange = leg.value(QStringLiteral("exchange")).toString();
        const QString market = leg.value(QStringLiteral("market")).toString();
        const QString symbol = leg.value(QStringLiteral("symbol")).toString();
        QVariantMap scope;
        scope.insert(QStringLiteral("id"), scopeId);
        scope.insert(QStringLiteral("label"), QStringLiteral("Leg %1 %2 %3 %4").arg(i + 1).arg(exchange, market, symbol).trimmed());
        scope.insert(QStringLiteral("index"), i);
        scope.insert(QStringLiteral("exchange"), exchange);
        scope.insert(QStringLiteral("market"), market);
        scope.insert(QStringLiteral("symbol"), symbol);
        scope.insert(QStringLiteral("initialBalanceE8"), leg.value(QStringLiteral("initial_balance_e8")).toInteger());
        scope.insert(QStringLiteral("totalPnlE8"), leg.value(QStringLiteral("total_pnl_e8")).toInteger());
        record.resultScopes.push_back(scope);
        record.scopedResultMetrics.insert(scopeId, resultMetrics(object, leg));
        record.scopedInitialBalanceE8.insert(scopeId, leg.value(QStringLiteral("initial_balance_e8")).toInteger());
        if (mode != RecordLoadMode::MetadataOnly) {
            const QJsonObject legEquity = leg.value(QStringLiteral("equity")).toObject();
            QString legEquityPath = legEquity.value(QStringLiteral("path")).toString();
            if (legEquityPath.isEmpty()) legEquityPath = QStringLiteral("legs/%1/equity.jsonl").arg(i);
            if (QFileInfo(legEquityPath).isRelative()) legEquityPath = runDir.absoluteFilePath(legEquityPath);
            qint64 minPnl = 0;
            qint64 maxPnl = 0;
            const QVariantList points = equityPointsFromJsonl(legEquityPath, leg, legEquity.value(QStringLiteral("rows")).toInteger(), minPnl, maxPnl);
            legEquitySeries.push_back(points);
            record.scopedEquityPoints.insert(scopeId, points);
            record.scopedPnlMinE8.insert(scopeId, minPnl);
            record.scopedPnlMaxE8.insert(scopeId, maxPnl);
        }
    }
    if (mode != RecordLoadMode::MetadataOnly && !legEquitySeries.empty()) {
        qint64 minPnl = 0;
        qint64 maxPnl = 0;
        const QVariantList points = synthesizePortfolioEquityPoints(legEquitySeries, minPnl, maxPnl);
        if (points.size() >= 2 && points.size() > record.equityPoints.size()) {
            record.equityPoints = points;
            record.pnlMinE8 = minPnl;
            record.pnlMaxE8 = maxPnl;
            record.scopedEquityPoints.insert(QStringLiteral("portfolio"), record.equityPoints);
            record.scopedPnlMinE8.insert(QStringLiteral("portfolio"), record.pnlMinE8);
            record.scopedPnlMaxE8.insert(QStringLiteral("portfolio"), record.pnlMaxE8);
        }
    }
    if (mode == RecordLoadMode::Details) {
        record.detailsLoaded = true;
    }
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

const BacktestViewModel::RunRecord* BacktestViewModel::recordForPath_(const QString& filePath) const noexcept {
    const QString target = QFileInfo(filePath).absoluteFilePath();
    const auto it = std::find_if(records_.begin(), records_.end(), [&target](const RunRecord& record) {
        return record.filePath == target;
    });
    return it == records_.end() ? nullptr : &(*it);
}

BacktestViewModel::RunRecord* BacktestViewModel::mutableRecordForRunId_(const QString& runId) noexcept {
    const auto it = std::find_if(records_.begin(), records_.end(), [&runId](const RunRecord& record) {
        return record.runId == runId;
    });
    return it == records_.end() ? nullptr : &(*it);
}

QString BacktestViewModel::effectiveResultScopeId_(const RunRecord& record) const {
    const QString requested = selectedResultScope_.trimmed().isEmpty() ? QStringLiteral("portfolio") : selectedResultScope_.trimmed();
    for (const QVariant& value : record.resultScopes) {
        const QString id = value.toMap().value(QStringLiteral("id")).toString();
        if (id == requested) return requested;
    }
    return QStringLiteral("portfolio");
}

void BacktestViewModel::scheduleRefresh_() {
    refreshTimer_.start();
}

qint64 BacktestViewModel::fileStampMs_(const QString& path, qint64* sizeOut) {
    if (sizeOut != nullptr) *sizeOut = -1;
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) return 0;
    const QFileInfo info(trimmed);
    if (!info.exists()) return 0;
    if (sizeOut != nullptr) *sizeOut = info.size();
    return info.lastModified().toMSecsSinceEpoch();
}

bool BacktestViewModel::fileStampMatches_(const QString& path, qint64 modifiedMs, qint64 size) {
    qint64 currentSize = -1;
    return fileStampMs_(path, &currentSize) == modifiedMs && currentSize == size;
}

void BacktestViewModel::updateWatcher_() {
    QStringList desiredDirs;
    const QString dirPath = backtestsDirectory();
    if (!dirPath.isEmpty()) {
        QDir sessionDir(selectedSessionPath());
        if (sessionDir.exists()) {
            sessionDir.mkpath(QStringLiteral("backtests"));
            if (QDir(dirPath).exists()) desiredDirs.push_back(dirPath);
        }
    }

    const QStringList watchedDirs = watcher_.directories();
    if (!watchedDirs.empty()) {
        QStringList dirsToRemove;
        for (const QString& dir : watchedDirs) {
            if (!desiredDirs.contains(dir)) dirsToRemove.push_back(dir);
        }
        if (!dirsToRemove.empty()) (void)watcher_.removePaths(dirsToRemove);
    }
    if (!desiredDirs.empty()) {
        QStringList dirsToAdd;
        for (const QString& dir : desiredDirs) {
            if (!watchedDirs.contains(dir)) dirsToAdd.push_back(dir);
        }
        if (!dirsToAdd.empty()) (void)watcher_.addPaths(dirsToAdd);
    }
}

void BacktestViewModel::setStatusText_(const QString& statusText) {
    if (statusText_ == statusText) return;
    statusText_ = statusText;
    emit statusTextChanged();
}

void BacktestViewModel::refreshSessionGateStatus_() {
    if (!strategySupportsSelectedSessionCount_()) {
        const QString gateText = strategySessionGateText(selectedStrategy_, selectedSessionCount());
        if (!gateText.isEmpty()) setStatusText_(gateText);
        return;
    }
    if (!statusText_.startsWith(QStringLiteral("Selected ")) ||
        !statusText_.contains(QStringLiteral("strategy supports"))) {
        return;
    }
    if (selectedSessionPath().isEmpty()) {
        setStatusText_(QStringLiteral("Select a session and strategy"));
    } else {
        setStatusText_(QStringLiteral("Watching %1 result%2")
                           .arg(static_cast<qulonglong>(records_.size()))
                           .arg(records_.size() == 1u ? QString{} : QStringLiteral("s")));
    }
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

void BacktestViewModel::setPreviewLoading_(bool loading, const QString& runId) {
    if (previewLoading_ == loading && previewLoadingRunId_ == runId) return;
    previewLoading_ = loading;
    previewLoadingRunId_ = loading ? runId : QString{};
    emit previewLoadingChanged();
}

void BacktestViewModel::setDetailsLoading_(bool loading, const QString& runId) {
    if (detailsLoading_ == loading && detailsLoadingRunId_ == runId) return;
    detailsLoading_ = loading;
    detailsLoadingRunId_ = loading ? runId : QString{};
    emit detailsLoadingChanged();
}

void BacktestViewModel::clearRecordDetails_(RunRecord& record) {
    record.sweepRows.clear();
    record.sweepCurves.clear();
    record.scopedEquityPoints.clear();
    record.scopedPnlMinE8.clear();
    record.scopedPnlMaxE8.clear();
    record.detailsErrorText.clear();
    record.detailsLoaded = false;
}

void BacktestViewModel::ensureSelectedPreviewLoaded_() {
    const RunRecord* record = selectedRecord_();
    if (record == nullptr || record->sweep || !record->valid || !record->equityPoints.empty() || selectedPreviewLoading()) return;
    const QString runId = record->runId;
    const QString filePath = record->filePath;
    const std::uint64_t generation = ++previewLoadGeneration_;
    setPreviewLoading_(true, runId);

    QPointer<BacktestViewModel> self(this);
    std::thread([self, generation, runId, filePath]() {
        RunRecord loaded = BacktestViewModel::loadRecord_(filePath, RecordLoadMode::Preview);
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, generation, runId, loaded = std::move(loaded)]() {
            if (self) self->applyLoadedPreview_(generation, runId, loaded);
        }, Qt::QueuedConnection);
    }).detach();
}

void BacktestViewModel::applyLoadedPreview_(std::uint64_t generation, const QString& runId, const RunRecord& loaded) {
    if (generation != previewLoadGeneration_ || selectedRunId_ != runId) return;
    setPreviewLoading_(false);
    RunRecord* record = mutableRecordForRunId_(runId);
    if (record == nullptr || record->filePath != loaded.filePath) return;

    record->equityPoints = loaded.equityPoints;
    record->resultScopes = loaded.resultScopes;
    record->scopedEquityPoints = loaded.scopedEquityPoints;
    record->scopedResultMetrics = loaded.scopedResultMetrics;
    record->scopedInitialBalanceE8 = loaded.scopedInitialBalanceE8;
    record->scopedPnlMinE8 = loaded.scopedPnlMinE8;
    record->scopedPnlMaxE8 = loaded.scopedPnlMaxE8;
    record->pnlMinE8 = loaded.pnlMinE8;
    record->pnlMaxE8 = loaded.pnlMaxE8;
    record->equityModifiedMs = loaded.equityModifiedMs;
    record->equitySize = loaded.equitySize;
    if (pendingDetailsRunId_ == runId) {
        record->detailsLoaded = loaded.valid;
        pendingDetailsRunId_.clear();
        setDetailsLoading_(false);
    }

    emit runsChanged();
    emit selectionChanged();
    emit selectedResultMetricChanged();
    emit detailsLoadingChanged();
}

void BacktestViewModel::applyLoadedDetails_(std::uint64_t generation, const QString& runId, const RunRecord& loaded) {
    if (generation != detailsLoadGeneration_ || selectedRunId_ != runId) return;
    setDetailsLoading_(false);
    RunRecord* record = mutableRecordForRunId_(runId);
    if (record == nullptr || record->filePath != loaded.filePath) return;

    record->rawJson = loaded.rawJson;
    record->summaryJson = loaded.summaryJson;
    record->errorText = loaded.errorText;
    record->detailsErrorText = loaded.detailsErrorText;
    record->equityPoints = loaded.equityPoints;
    record->resultScopes = loaded.resultScopes;
    record->resultMetrics = loaded.resultMetrics;
    record->scopedEquityPoints = loaded.scopedEquityPoints;
    record->scopedResultMetrics = loaded.scopedResultMetrics;
    record->scopedInitialBalanceE8 = loaded.scopedInitialBalanceE8;
    record->scopedPnlMinE8 = loaded.scopedPnlMinE8;
    record->scopedPnlMaxE8 = loaded.scopedPnlMaxE8;
    record->sweepRows = loaded.sweepRows;
    record->sweepCurves = loaded.sweepCurves;
    record->sweepParamKeys = loaded.sweepParamKeys;
    record->pnlMinE8 = loaded.pnlMinE8;
    record->pnlMaxE8 = loaded.pnlMaxE8;
    record->initialBalanceE8 = loaded.initialBalanceE8;
    record->totalPnlE8 = loaded.totalPnlE8;
    record->pnlText = loaded.pnlText;
    record->valid = loaded.valid;
    record->detailsLoaded = loaded.valid;
    selectedDetailsErrorText_ = loaded.valid ? loaded.detailsErrorText : loaded.errorText;

    emit runsChanged();
    emit selectionChanged();
    emit selectedResultMetricChanged();
    emit detailsLoadingChanged();
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
    if (!riskMinEquityPct_.trimmed().isEmpty()) summary += QStringLiteral(" | min_equity=%1%").arg(riskMinEquityPct_.trimmed());
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
    const QString templatePath = configTemplatePathForStrategy(selectedStrategy_);
    const QString templateText = templatePath.isEmpty() ? QString{} : readTextFile(templatePath);
    for (const IniKeyValue& row : iniSectionValues(templateText, QStringLiteral("strategy"))) {
        const QString key = row.key.trimmed().toLower();
        if (!isTemplateStrategyParamKey(key) || paramOrder_.contains(key)) continue;
        const QString value = row.value.trimmed();
        paramOrder_.push_back(key);
        paramValues_.insert(key, value);
        paramModes_.insert(key, isSweepKey(key) ? QStringLiteral("sweep") : QStringLiteral("fixed"));
        paramMinValues_.insert(key, defaultRangeMin(key, value));
        paramMaxValues_.insert(key, defaultRangeMax(key, value));
        paramStepValues_.insert(key, defaultRangeStep(key));
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
    riskMinEquityPct_ = settings_.value(QStringLiteral("backtests/risk_min_equity_pct"), riskMinEquityPct_).toString().trimmed();
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
    settings_.setValue(QStringLiteral("backtests/risk_min_equity_pct"), riskMinEquityPct_);
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
    emit strategyParametersChanged();
    return true;
}

QString BacktestViewModel::writeRunConfig_(const QString& runId, const QHash<QString, QString>& overrides, bool fixedOnly) {
    const QString templatePath = configTemplatePathForStrategy(selectedStrategy_);
    const QString base = templatePath.isEmpty() ? QString{} : readTextFile(templatePath);
    if (!templatePath.isEmpty() && base.isEmpty()) return {};
    const QString session = selectedSessionPath();
    const QStringList sessionPaths = selectedSessionPaths_();
    if (sessionPaths.empty()) return {};
    QStringList legRefs;
    QStringList venueOrder;
    QHash<QString, QStringList> venueSymbols;
    QHash<QString, QString> venueApiSlots;
    QHash<QString, QVariantMap> venueExecutionByVenue;
    for (int i = 0; i < sessionPaths.size(); ++i) {
        const QString path = sessionPaths.at(i);
        const QString venue = venueSectionForSession(path);
        const QString symbol = i == 0 ? selectedSymbol() : symbolForSessionPath(path);
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
            QVariantMap row;
            row.insert(QStringLiteral("makerFeeBps"), venueExecutionValue_(venueKey, QStringLiteral("maker_fee_bps"), makerFeeBps_));
            row.insert(QStringLiteral("takerFeeBps"), venueExecutionValue_(venueKey, QStringLiteral("taker_fee_bps"), takerFeeBps_));
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
    out << "[strategy]\n";
    out << "type=" << selectedStrategy_ << "\n";
    for (const QString& key : paramOrder_) {
        const hft_backtest::StrategyParamMetadata* param = paramMetadataFor(selectedStrategy_, key);
        if (param != nullptr && param->exclusiveGroup != 0u && activeParamByGroup_.value(static_cast<int>(param->exclusiveGroup)) != key) continue;
        if (fixedOnly && paramModes_.value(key, QStringLiteral("fixed")) != QStringLiteral("fixed")) continue;
        const QString value = overrides.value(key, paramValues_.value(key)).trimmed();
        if (!value.isEmpty()) out << key << "=" << value << "\n";
    }
    if (!riskMinEquityPct_.trimmed().isEmpty()) {
        out << "\n[risk]\n";
        out << "enabled=true\n";
        out << "min_equity_pct=" << riskMinEquityPct_.trimmed() << "\n";
    }
    for (const QString& venue : venueOrder) {
        out << "\n[venue." << venue << "]\n";
        out << "api_slot=" << venueApiSlots.value(venue, QStringLiteral("1")) << "\n";
        out << "symbols=" << venueSymbols.value(venue).join(QLatin1Char(',')) << "\n";
        out << "initial_balance_usdt=" << initialBalanceUsdt_ << "\n";
        const QVariantMap execution = venueExecutionByVenue.value(venue);
        out << "maker_fee_bps=" << execution.value(QStringLiteral("makerFeeBps"), makerFeeBps_).toString() << "\n";
        out << "taker_fee_bps=" << execution.value(QStringLiteral("takerFeeBps"), takerFeeBps_).toString() << "\n";
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
