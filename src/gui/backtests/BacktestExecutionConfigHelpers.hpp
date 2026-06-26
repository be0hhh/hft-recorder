#pragma once

#include "hft_backtest/backtest.hpp"

#include <QString>
#include <QTextStream>
#include <QVariantMap>

namespace hftrec::gui {

QString normalizedFeeMarket(QString market);
QString venueExecutionKey(const QString& sessionPath);
QString venueExecutionSettingKey(QString venueKey);
QString venueExecutionMapKey(const QString& venueKey, const QString& field);
bool isVenueExecutionField(const QString& field);
QString exchangeExecutionPresetSummary(const QString& exchange, const QString& market, bool rateLimitsEnabled = true);
hft_backtest::BacktestFeeSchedule feeScheduleFromVenueRow(const QVariantMap& row);
hft_backtest::BacktestRateLimitSchedule rateLimitScheduleFromVenueRow(const QVariantMap& row);
void writeBacktestRateLimitConfig(QTextStream& out, bool enabled);
void writeRuntimeRateLimitConfig(QTextStream& out, const QVariantMap& execution);

}  // namespace hftrec::gui
