#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVariantList>

#include <optional>
#include <vector>

namespace hftrec::gui {

enum class BacktestRunSummaryStatus {
    Ready,
    MissingSummary,
    UnsupportedSchema,
    MissingTotalPnl,
    InvalidTotalPnl,
};

struct BacktestRunSummary {
    BacktestRunSummaryStatus status{BacktestRunSummaryStatus::MissingSummary};
    QJsonObject values{};
    qint64 initialBalanceE8{0};
    qint64 totalPnlE8{0};
    QString error{};
    bool canonicalRunResult{false};

    [[nodiscard]] bool ready() const noexcept {
        return status == BacktestRunSummaryStatus::Ready;
    }
};

BacktestRunSummary decodeBacktestRunSummary(const QJsonObject& root);

QString jsonValueString(const QJsonObject& object, const QString& key);
QString manifestObjectValue(const QJsonObject& object, const QString& key);
bool isE8Key(const QString& key);
QString e8DisplayString(qint64 value);
QString humanSummaryJson(const QJsonValue& value);
int errorCount(const QJsonValue& value);
QVariantList resultMetrics(const QJsonObject& root, const QJsonObject& summary);
QString pnlPercentText(qint64 pnlE8, qint64 initialBalanceE8);
QVariantList equityPointsFromJsonl(const QString& path,
                                   const QJsonObject& summary,
                                   qint64 totalRows,
                                   qint64& minPnl,
                                   qint64& maxPnl,
                                   std::optional<qint64> summaryTotalPnlE8 = std::nullopt);
QVariantList synthesizePortfolioEquityPoints(const std::vector<QVariantList>& legSeries,
                                             const std::vector<qint64>& legInitialBalancesE8,
                                             qint64& minPnl,
                                             qint64& maxPnl);

}  // namespace hftrec::gui
