#include "gui/backtests/BacktestSweepHelpers.hpp"

#include "gui/backtests/BacktestResultHelpers.hpp"

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>

#include <algorithm>
#include <utility>

namespace hftrec::gui {
namespace {

qint64 jsonIntValue(const QJsonObject& object, const QString& key) {
    return object.value(key).toInteger();
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

}  // namespace

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

QString sweepLegMetricKey(int legIndex) {
    return QStringLiteral("leg_%1_total_pnl_e8").arg(legIndex);
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

}  // namespace hftrec::gui
