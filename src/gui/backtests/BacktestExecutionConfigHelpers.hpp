#pragma once

#include "hft_backtest/backtest.hpp"

#include <QString>
#include <QTextStream>
#include <QVariantMap>

#include <cstdint>
#include <vector>

namespace hftrec::gui {

class SessionManifestSnapshot;

struct BacktestExecutionPolicy {
    std::uint64_t latencySeed{0};
    hft_backtest::BacktestLatencyProfile marketDataLatency{};
    hft_backtest::BacktestLatencyProfile marketOrderLatency{};
    hft_backtest::BacktestLatencyProfile limitOrderLatency{};
    hft_backtest::BacktestLatencyProfile cancelOrderLatency{};
    hft_backtest::BacktestLatencyProfile userDataLatency{};
    std::uint64_t orderLatencyUs{1000};
    std::uint64_t cancelLatencyUs{1000};
    std::int64_t initialBalanceE8{0};
    bool rateLimitsEnabled{true};
    bool strictRateLimitsEnabled{false};
    std::vector<std::int64_t> legInitialBalancesE8{};
    std::vector<hft_backtest::BacktestFeeSchedule> feeSchedules{};
    std::vector<hft_backtest::BacktestLatencySchedule> latencySchedules{};
    std::vector<hft_backtest::BacktestRateLimitSchedule> rateLimitSchedules{};
};

struct BacktestPreparedSession {
    QString path{};
    QString exchange{};
    QString market{};
    QString venue{};
    QString symbol{};
    QString configSymbol{};
};

struct BacktestPreparedSessions {
    std::vector<BacktestPreparedSession> sessions{};
    QString error{};

    [[nodiscard]] bool ready() const noexcept {
        return error.isEmpty() && !sessions.empty();
    }
};

void applyBacktestExecutionPolicy(hft_backtest::BacktestRunRequest& request,
                                  const BacktestExecutionPolicy& policy);

// Execution-latency sweep parameters target the base request profiles. Per-venue
// profiles must be omitted in that mode or they take precedence over the sweep.
[[nodiscard]] constexpr bool usePerVenueLatencySchedules(bool executionLatencySweep) noexcept {
    return !executionLatencySweep;
}

QString normalizedFeeMarket(QString market);
QString venueExecutionKey(const SessionManifestSnapshot& manifest);
QString venueExecutionKey(const QString& sessionPath);
QString venueExecutionSettingKey(QString venueKey);
QString venueExecutionMapKey(const QString& venueKey, const QString& field);
bool isVenueExecutionField(const QString& field);
QString exchangeExecutionPresetSummary(const QString& exchange, const QString& market, bool rateLimitsEnabled = true);
hft_backtest::BacktestFeeSchedule feeScheduleFromVenueRow(const QVariantMap& row);
hft_backtest::BacktestRateLimitSchedule rateLimitScheduleFromVenueRow(const QVariantMap& row);
void writeBacktestRateLimitConfig(QTextStream& out, bool enabled, bool strictRejects = false);
void writeRuntimeRateLimitConfig(QTextStream& out, const QVariantMap& execution);

}  // namespace hftrec::gui
