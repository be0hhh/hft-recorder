#include "gui/backtests/BacktestExecutionConfigHelpers.hpp"

#include "gui/backtests/BacktestSessionHelpers.hpp"

#include "hft_trader/core/fees/FeePresets.hpp"
#include "hft_trader/core/rate_limit/RateLimit.hpp"
#include "hft_trader/core/rate_limit/RateLimitPresets.hpp"

#include <cstdint>
#include <limits>
#include <QStringList>

namespace hftrec::gui {
namespace {

std::int64_t positiveInt64Value(const QVariantMap& row, const QString& key) {
    bool ok = false;
    const qlonglong parsed = row.value(key).toString().trimmed().toLongLong(&ok);
    if (!ok || parsed <= 0) return 0;
    return static_cast<std::int64_t>(parsed);
}

bool nonNegativeInt64Value(const QVariantMap& row, const QString& key, std::int64_t& out) {
    bool ok = false;
    const qlonglong parsed = row.value(key).toString().trimmed().toLongLong(&ok);
    if (!ok || parsed < 0) return false;
    out = static_cast<std::int64_t>(parsed);
    return true;
}

std::uint64_t positiveUInt64Value(const QVariantMap& row, const QString& key) {
    bool ok = false;
    const qulonglong parsed = row.value(key).toString().trimmed().toULongLong(&ok);
    if (!ok || parsed == 0u) return 0u;
    return static_cast<std::uint64_t>(parsed);
}

void addRateLimitBucket(hft_backtest::BacktestRateLimitSchedule& schedule,
                        const QVariantMap& row,
                        const QString& limitKey,
                        const QString& intervalKey,
                        hft_trader::core::RateLimitBucketKind kind) {
    const std::int64_t limit = positiveInt64Value(row, limitKey);
    const std::uint64_t intervalMs = positiveUInt64Value(row, intervalKey);
    constexpr std::uint64_t kNsPerMs = 1000000ull;
    if (limit <= 0 || intervalMs == 0u || intervalMs > (std::numeric_limits<std::uint64_t>::max() / kNsPerMs)) return;
    hft_trader::core::RateLimitBucketConfig bucket{};
    bucket.kind = kind;
    bucket.limit = limit;
    bucket.intervalNs = intervalMs * kNsPerMs;
    bucket.enabled = true;
    schedule.buckets.push_back(bucket);
}

void addRateLimitActionCost(hft_backtest::BacktestRateLimitSchedule& schedule,
                            const QVariantMap& row,
                            const QString& costKey,
                            hft_trader::core::RateLimitActionKind action,
                            hft_trader::core::RateLimitBucketKind bucket) {
    std::int64_t cost = 0;
    if (!nonNegativeInt64Value(row, costKey, cost)) return;
    for (hft_trader::core::RateLimitActionConfig& existing : schedule.actions) {
        if (existing.action != action) continue;
        if (cost == 0) return;
        if (existing.costCount >= existing.costs.size()) return;
        hft_trader::core::RateLimitCost& slot = existing.costs[existing.costCount++];
        slot.bucket = bucket;
        slot.cost = cost;
        return;
    }
    hft_trader::core::RateLimitActionConfig config{};
    config.action = action;
    config.enabled = true;
    if (cost > 0) {
        config.costCount = 1u;
        config.costs[0].bucket = bucket;
        config.costs[0].cost = cost;
    }
    schedule.actions.push_back(config);
}

bool rowHasPositiveValue(const QVariantMap& row, const QString& key) {
    return positiveInt64Value(row, key) > 0;
}

std::int64_t decimalE8Value(const QVariantMap& row, const QString& key, bool& ok) {
    ok = false;
    const QString text = row.value(key).toString().trimmed();
    if (text.isEmpty()) return 0;
    qsizetype pos = 0;
    bool negative = false;
    if (text.at(pos) == QLatin1Char('-')) {
        negative = true;
        ++pos;
    }
    qint64 whole = 0;
    while (pos < text.size() && text.at(pos).isDigit()) {
        whole = whole * 10 + text.at(pos).digitValue();
        ++pos;
    }
    qint64 frac = 0;
    qint64 scale = 100000000ll;
    if (pos < text.size() && text.at(pos) == QLatin1Char('.')) {
        ++pos;
        while (pos < text.size() && text.at(pos).isDigit() && scale > 1) {
            scale /= 10;
            frac += text.at(pos).digitValue() * scale;
            ++pos;
        }
        while (pos < text.size() && text.at(pos).isDigit()) ++pos;
    }
    if (pos != text.size()) return 0;
    ok = true;
    const qint64 value = whole * 100000000ll + frac;
    return negative ? -value : value;
}

ExchangeId exchangeFromText(QString value) noexcept {
    value = value.trimmed().toLower();
    if (value == QStringLiteral("binance")) return canon::kExchangeIdBinance;
    if (value == QStringLiteral("bybit")) return canon::kExchangeIdBybit;
    if (value == QStringLiteral("kucoin")) return canon::kExchangeIdKucoin;
    if (value == QStringLiteral("gate")) return canon::kExchangeIdGate;
    if (value == QStringLiteral("bitget")) return canon::kExchangeIdBitget;
    if (value == QStringLiteral("aster")) return canon::kExchangeIdAster;
    if (value == QStringLiteral("okx")) return canon::kExchangeIdOkx;
    if (value == QStringLiteral("mexc")) return canon::kExchangeIdMexc;
    return canon::kExchangeIdUnknown;
}

canon::MarketType marketFromText(QString value) noexcept {
    value = normalizedFeeMarket(value);
    if (value == QStringLiteral("spot")) return canon::kMarketTypeSpot;
    if (value == QStringLiteral("margin")) return canon::kMarketTypeMargin;
    if (value == QStringLiteral("futures") ||
        value == QStringLiteral("futures_usd") ||
        value == QStringLiteral("futures_usdt") ||
        value == QStringLiteral("futures_usdc")) return canon::kMarketTypeFutures;
    if (value == QStringLiteral("swap")) return canon::kMarketTypeSwap;
    if (value == QStringLiteral("inverse")) return canon::kMarketTypeInverse;
    return canon::kMarketTypeUnknown;
}

QString formatBpsE8(std::int64_t bpsE8) {
    const bool negative = bpsE8 < 0;
    std::int64_t value = negative ? -bpsE8 : bpsE8;
    const std::int64_t whole = value / 100000000ll;
    std::int64_t frac = value % 100000000ll;
    QString out = QString::number(whole);
    if (frac != 0) {
        QString tail = QString::number(frac).rightJustified(8, QLatin1Char('0'));
        while (tail.endsWith(QLatin1Char('0'))) tail.chop(1);
        out += QLatin1Char('.');
        out += tail;
    }
    if (negative) out.prepend(QLatin1Char('-'));
    return out;
}

QString bucketKindName(hft_trader::core::RateLimitBucketKind kind) {
    using hft_trader::core::RateLimitBucketKind;
    switch (kind) {
        case RateLimitBucketKind::RequestWeight: return QStringLiteral("weight");
        case RateLimitBucketKind::Orders: return QStringLiteral("orders");
        case RateLimitBucketKind::Requests: return QStringLiteral("requests");
        case RateLimitBucketKind::CancelOrders: return QStringLiteral("cancel");
        case RateLimitBucketKind::ReduceOnlyOrders: return QStringLiteral("reduce-only");
        default: return QStringLiteral("unknown");
    }
}

QString actionKindName(hft_trader::core::RateLimitActionKind kind) {
    using hft_trader::core::RateLimitActionKind;
    switch (kind) {
        case RateLimitActionKind::LimitOrder: return QStringLiteral("limit");
        case RateLimitActionKind::MarketOrder: return QStringLiteral("market");
        case RateLimitActionKind::CancelOrder: return QStringLiteral("cancel");
        case RateLimitActionKind::ReduceOnlyLimitOrder: return QStringLiteral("RO limit");
        case RateLimitActionKind::ReduceOnlyMarketOrder: return QStringLiteral("RO market");
        case RateLimitActionKind::WsSubscribe: return QStringLiteral("WS sub");
        case RateLimitActionKind::RestOrderQuery: return QStringLiteral("query");
        default: return QStringLiteral("action");
    }
}

QString intervalText(std::uint64_t intervalNs) {
    constexpr std::uint64_t kNsPerMs = 1000000ull;
    constexpr std::uint64_t kNsPerSecond = 1000000000ull;
    constexpr std::uint64_t kNsPerMinute = 60000000000ull;
    if (intervalNs != 0u && intervalNs % kNsPerMinute == 0u) {
        return QStringLiteral("%1m").arg(static_cast<qulonglong>(intervalNs / kNsPerMinute));
    }
    if (intervalNs != 0u && intervalNs % kNsPerSecond == 0u) {
        return QStringLiteral("%1s").arg(static_cast<qulonglong>(intervalNs / kNsPerSecond));
    }
    if (intervalNs != 0u && intervalNs % kNsPerMs == 0u) {
        return QStringLiteral("%1ms").arg(static_cast<qulonglong>(intervalNs / kNsPerMs));
    }
    return QStringLiteral("%1ns").arg(static_cast<qulonglong>(intervalNs));
}

QString costSummary(const hft_trader::core::RateLimitActionConfig& action) {
    if (!action.enabled || action.costCount == 0u) return {};
    QStringList costs;
    for (std::uint8_t i = 0u; i < action.costCount && i < action.costs.size(); ++i) {
        const auto& cost = action.costs[i];
        if (cost.cost <= 0) continue;
        costs.push_back(QStringLiteral("%1 %2").arg(cost.cost).arg(bucketKindName(cost.bucket)));
    }
    if (costs.isEmpty()) return {};
    return QStringLiteral("%1: %2").arg(actionKindName(action.action), costs.join(QStringLiteral("+")));
}

}  // namespace

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

hft_backtest::BacktestExecutionPipeline guiBacktestExecutionPipeline() noexcept {
    return hft_backtest::BacktestExecutionPipeline::Inline;
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
           field == QStringLiteral("user_data_jitter_us") ||
           field == QStringLiteral("rate_limit_orders_limit") ||
           field == QStringLiteral("rate_limit_orders_interval_ms") ||
           field == QStringLiteral("rate_limit_cancel_orders_limit") ||
           field == QStringLiteral("rate_limit_cancel_orders_interval_ms") ||
           field == QStringLiteral("rate_limit_reduce_only_orders_limit") ||
           field == QStringLiteral("rate_limit_reduce_only_orders_interval_ms") ||
           field == QStringLiteral("rate_limit_limit_order_cost") ||
           field == QStringLiteral("rate_limit_market_order_cost") ||
           field == QStringLiteral("rate_limit_cancel_order_cost") ||
           field == QStringLiteral("rate_limit_reduce_only_limit_order_cost") ||
           field == QStringLiteral("rate_limit_reduce_only_market_order_cost");
}

QString exchangeExecutionPresetSummary(const QString& exchange, const QString& market, bool rateLimitsEnabled) {
    const ExchangeId exchangeId = exchangeFromText(exchange);
    const canon::MarketType marketType = marketFromText(market);
    const hft_trader::core::ExchangeFeePreset fee =
        hft_trader::core::defaultExchangeFeePreset(exchangeId, marketType);
    hft_trader::core::RateLimitPreset rate =
        hft_trader::core::defaultExchangeRateLimitPreset(exchangeId, marketType);
    (void)hft_trader::core::addDefaultRateLimitActions(rate);

    QStringList parts;
    if (fee.available) {
        parts.push_back(QStringLiteral("Fees M/T %1/%2 bps")
                            .arg(formatBpsE8(fee.makerFeeBpsE8), formatBpsE8(fee.takerFeeBpsE8)));
    } else {
        parts.push_back(QStringLiteral("Fees preset missing"));
    }

    if (rateLimitsEnabled) {
        QStringList buckets;
        for (std::uint8_t i = 0u; i < rate.bucketCount; ++i) {
            const auto& bucket = rate.buckets[i];
            if (!bucket.enabled || bucket.limit <= 0) continue;
            buckets.push_back(QStringLiteral("%1 %2/%3")
                                  .arg(bucketKindName(bucket.kind))
                                  .arg(bucket.limit)
                                  .arg(intervalText(bucket.intervalNs)));
        }
        parts.push_back(buckets.isEmpty()
                            ? QStringLiteral("RL preset missing")
                            : QStringLiteral("RL %1").arg(buckets.join(QStringLiteral(", "))));

        QStringList actions;
        for (std::uint8_t i = 0u; i < rate.actionCount; ++i) {
            const QString text = costSummary(rate.actions[i]);
            if (!text.isEmpty()) actions.push_back(text);
        }
        if (!actions.isEmpty()) parts.push_back(QStringLiteral("Costs %1").arg(actions.join(QStringLiteral(", "))));
    } else {
        parts.push_back(QStringLiteral("RL off"));
    }
    return parts.join(QStringLiteral("; "));
}

hft_backtest::BacktestFeeSchedule feeScheduleFromVenueRow(const QVariantMap& row) {
    hft_backtest::BacktestFeeSchedule schedule{};
    const QString exchange = row.value(QStringLiteral("exchange")).toString();
    const QString market = row.value(QStringLiteral("market")).toString();
    schedule.exchange = exchange.toStdString();
    schedule.market = market.toStdString();
    const hft_trader::core::ExchangeFeePreset preset =
        hft_trader::core::defaultExchangeFeePreset(exchangeFromText(exchange), marketFromText(market));
    if (preset.available) {
        schedule.makerFeeBpsE8 = preset.makerFeeBpsE8;
        schedule.takerFeeBpsE8 = preset.takerFeeBpsE8;
    }
    bool makerOk = false;
    const std::int64_t maker = decimalE8Value(row, QStringLiteral("makerFeeBps"), makerOk);
    if (makerOk) schedule.makerFeeBpsE8 = maker;
    bool takerOk = false;
    const std::int64_t taker = decimalE8Value(row, QStringLiteral("takerFeeBps"), takerOk);
    if (takerOk) schedule.takerFeeBpsE8 = taker;
    return schedule;
}

hft_backtest::BacktestRateLimitSchedule rateLimitScheduleFromVenueRow(const QVariantMap& row) {
    hft_backtest::BacktestRateLimitSchedule schedule{};
    schedule.exchange = row.value(QStringLiteral("exchange")).toString().toStdString();
    schedule.market = row.value(QStringLiteral("market")).toString().toStdString();
    addRateLimitBucket(schedule,
                       row,
                       QStringLiteral("rateLimitOrdersLimit"),
                       QStringLiteral("rateLimitOrdersIntervalMs"),
                       hft_trader::core::RateLimitBucketKind::Orders);
    addRateLimitBucket(schedule,
                       row,
                       QStringLiteral("rateLimitCancelOrdersLimit"),
                       QStringLiteral("rateLimitCancelOrdersIntervalMs"),
                       hft_trader::core::RateLimitBucketKind::CancelOrders);
    addRateLimitBucket(schedule,
                       row,
                       QStringLiteral("rateLimitReduceOnlyOrdersLimit"),
                       QStringLiteral("rateLimitReduceOnlyOrdersIntervalMs"),
                       hft_trader::core::RateLimitBucketKind::ReduceOnlyOrders);

    const bool hasOrdersBucket = rowHasPositiveValue(row, QStringLiteral("rateLimitOrdersLimit"));
    const bool hasCancelBucket = rowHasPositiveValue(row, QStringLiteral("rateLimitCancelOrdersLimit"));
    const bool hasReduceOnlyBucket = rowHasPositiveValue(row, QStringLiteral("rateLimitReduceOnlyOrdersLimit"));
    if (hasOrdersBucket) {
        addRateLimitActionCost(schedule,
                               row,
                               QStringLiteral("rateLimitLimitOrderCost"),
                               hft_trader::core::RateLimitActionKind::LimitOrder,
                               hft_trader::core::RateLimitBucketKind::Orders);
        addRateLimitActionCost(schedule,
                               row,
                               QStringLiteral("rateLimitMarketOrderCost"),
                               hft_trader::core::RateLimitActionKind::MarketOrder,
                               hft_trader::core::RateLimitBucketKind::Orders);
    }
    if (hasCancelBucket || hasOrdersBucket) {
        addRateLimitActionCost(schedule,
                               row,
                               QStringLiteral("rateLimitCancelOrderCost"),
                               hft_trader::core::RateLimitActionKind::CancelOrder,
                               hasCancelBucket ? hft_trader::core::RateLimitBucketKind::CancelOrders
                                               : hft_trader::core::RateLimitBucketKind::Orders);
    }
    if (hasReduceOnlyBucket || hasOrdersBucket) {
        const auto bucket = hasReduceOnlyBucket ? hft_trader::core::RateLimitBucketKind::ReduceOnlyOrders
                                                : hft_trader::core::RateLimitBucketKind::Orders;
        addRateLimitActionCost(schedule,
                               row,
                               QStringLiteral("rateLimitReduceOnlyLimitOrderCost"),
                               hft_trader::core::RateLimitActionKind::ReduceOnlyLimitOrder,
                               bucket);
        addRateLimitActionCost(schedule,
                               row,
                               QStringLiteral("rateLimitReduceOnlyMarketOrderCost"),
                               hft_trader::core::RateLimitActionKind::ReduceOnlyMarketOrder,
                               bucket);
    }
    return schedule;
}

void writeBacktestRateLimitConfig(QTextStream& out, bool enabled, bool strictRejects) {
    out << "rate_limits_enabled=" << (enabled ? "true" : "false") << "\n";
    out << "strict_rate_limits=" << (enabled && strictRejects ? "true" : "false") << "\n";
}

void writeRuntimeRateLimitConfig(QTextStream& out, const QVariantMap& execution) {
    const QString ordersLimit = execution.value(QStringLiteral("rateLimitOrdersLimit")).toString().trimmed();
    const QString ordersInterval = execution.value(QStringLiteral("rateLimitOrdersIntervalMs")).toString().trimmed();
    const QString cancelLimit = execution.value(QStringLiteral("rateLimitCancelOrdersLimit")).toString().trimmed();
    const QString cancelInterval = execution.value(QStringLiteral("rateLimitCancelOrdersIntervalMs")).toString().trimmed();
    const QString reduceOnlyLimit = execution.value(QStringLiteral("rateLimitReduceOnlyOrdersLimit")).toString().trimmed();
    const QString reduceOnlyInterval = execution.value(QStringLiteral("rateLimitReduceOnlyOrdersIntervalMs")).toString().trimmed();
    const bool hasOrders = positiveInt64Value(execution, QStringLiteral("rateLimitOrdersLimit")) > 0 &&
                           positiveUInt64Value(execution, QStringLiteral("rateLimitOrdersIntervalMs")) > 0u;
    const bool hasCancel = positiveInt64Value(execution, QStringLiteral("rateLimitCancelOrdersLimit")) > 0 &&
                           positiveUInt64Value(execution, QStringLiteral("rateLimitCancelOrdersIntervalMs")) > 0u;
    const bool hasReduceOnly = positiveInt64Value(execution, QStringLiteral("rateLimitReduceOnlyOrdersLimit")) > 0 &&
                               positiveUInt64Value(execution, QStringLiteral("rateLimitReduceOnlyOrdersIntervalMs")) > 0u;
    if (hasOrders) {
        out << "rate_limit_bucket_orders_limit=" << ordersLimit << "\n";
        out << "rate_limit_bucket_orders_interval_ms=" << ordersInterval << "\n";
        out << "rate_limit_action_limit_order_orders=" << execution.value(QStringLiteral("rateLimitLimitOrderCost"), QStringLiteral("1")).toString() << "\n";
        out << "rate_limit_action_market_order_orders=" << execution.value(QStringLiteral("rateLimitMarketOrderCost"), QStringLiteral("1")).toString() << "\n";
    }
    if (hasCancel) {
        out << "rate_limit_bucket_cancel_orders_limit=" << cancelLimit << "\n";
        out << "rate_limit_bucket_cancel_orders_interval_ms=" << cancelInterval << "\n";
        out << "rate_limit_action_cancel_order_cancel_orders=" << execution.value(QStringLiteral("rateLimitCancelOrderCost"), QStringLiteral("1")).toString() << "\n";
    } else if (hasOrders) {
        out << "rate_limit_action_cancel_order_orders=" << execution.value(QStringLiteral("rateLimitCancelOrderCost"), QStringLiteral("1")).toString() << "\n";
    }
    if (hasReduceOnly) {
        out << "rate_limit_bucket_reduce_only_orders_limit=" << reduceOnlyLimit << "\n";
        out << "rate_limit_bucket_reduce_only_orders_interval_ms=" << reduceOnlyInterval << "\n";
        out << "rate_limit_action_reduce_only_limit_order_reduce_only_orders=" << execution.value(QStringLiteral("rateLimitReduceOnlyLimitOrderCost"), QStringLiteral("1")).toString() << "\n";
        out << "rate_limit_action_reduce_only_market_order_reduce_only_orders=" << execution.value(QStringLiteral("rateLimitReduceOnlyMarketOrderCost"), QStringLiteral("1")).toString() << "\n";
    } else if (hasOrders) {
        out << "rate_limit_action_reduce_only_limit_order_orders=" << execution.value(QStringLiteral("rateLimitReduceOnlyLimitOrderCost"), QStringLiteral("1")).toString() << "\n";
        out << "rate_limit_action_reduce_only_market_order_orders=" << execution.value(QStringLiteral("rateLimitReduceOnlyMarketOrderCost"), QStringLiteral("1")).toString() << "\n";
    }
}

}  // namespace hftrec::gui
