#include "core/tui/RecorderTuiLaunch.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace hftrec::tui {

namespace {

constexpr std::array<LaunchChannel, 8> kLaunchChannels{
    LaunchChannel::Trades,
    LaunchChannel::Liquidations,
    LaunchChannel::BookTicker,
    LaunchChannel::Orderbook,
    LaunchChannel::MarkPrice,
    LaunchChannel::IndexPrice,
    LaunchChannel::Funding,
    LaunchChannel::PriceLimit,
};

std::string lowerAscii(std::string text) {
    for (char& ch : text) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return text;
}

std::int64_t lastStartForExchange(const std::vector<std::pair<std::string, std::int64_t>>& starts,
                                  const std::string& exchange) noexcept {
    for (const auto& item : starts) {
        if (item.first == exchange) return item.second;
    }
    return -1;
}

void setLastStartForExchange(std::vector<std::pair<std::string, std::int64_t>>& starts,
                             std::string exchange,
                             std::int64_t startMs) {
    for (auto& item : starts) {
        if (item.first == exchange) {
            item.second = startMs;
            return;
        }
    }
    starts.emplace_back(std::move(exchange), startMs);
}

RecorderTuiLaunchJob makePlannedJob(const RecorderTuiJob& source, std::size_t originalIndex) {
    RecorderTuiLaunchJob out{};
    out.job = source;
    out.originalIndex = originalIndex;
    return out;
}

RecorderTuiLaunchJob filterJobChannels(RecorderTuiLaunchJob out,
                                       RecorderTuiChannelAvailabilityFn availability,
                                       void* userData) {
    if (availability != nullptr) {
        for (LaunchChannel channel : kLaunchChannels) {
            if (!launchChannelSelected(out.job.channels, channel)) continue;
            if (availability(out.job, channel, userData)) continue;
            setLaunchChannel(out.job.channels, channel, false);
            setLaunchChannel(out.skippedChannels, channel, true);
        }
    }
    if (!anyLaunchChannelSelected(out.job.channels)) {
        out.skipJob = true;
        out.skipReason = "no supported channels";
    }
    return out;
}

std::int64_t earliestStartMs(const RecorderTuiLaunchJob& job,
                             std::size_t launchedCount,
                             int waveSize,
                             int staggerMs,
                             int sameExchangeCooldownMs,
                             const std::vector<std::pair<std::string, std::int64_t>>& exchangeStarts) noexcept {
    const std::int64_t waveStart = static_cast<std::int64_t>(launchedCount / static_cast<std::size_t>(waveSize)) *
                                  static_cast<std::int64_t>(staggerMs);
    const std::string exchange = lowerAscii(job.job.exchange);
    const std::int64_t lastStart = lastStartForExchange(exchangeStarts, exchange);
    if (lastStart < 0) return waveStart;
    return std::max(waveStart, lastStart + static_cast<std::int64_t>(sameExchangeCooldownMs));
}

void scheduleRunnableJobs(std::vector<RecorderTuiLaunchJob>& runnable,
                          int waveSize,
                          int staggerMs,
                          int sameExchangeCooldownMs,
                          std::vector<RecorderTuiLaunchJob>& scheduled) {
    std::vector<std::pair<std::string, std::int64_t>> exchangeStarts;
    scheduled.reserve(scheduled.size() + runnable.size());

    std::size_t launched = 0;
    while (!runnable.empty()) {
        std::size_t best = 0;
        std::int64_t bestStart = earliestStartMs(runnable.front(), launched, waveSize, staggerMs,
                                                 sameExchangeCooldownMs, exchangeStarts);
        for (std::size_t i = 1; i < runnable.size(); ++i) {
            const std::int64_t candidateStart = earliestStartMs(runnable[i], launched, waveSize, staggerMs,
                                                                sameExchangeCooldownMs, exchangeStarts);
            if (candidateStart < bestStart ||
                (candidateStart == bestStart && runnable[i].originalIndex < runnable[best].originalIndex)) {
                best = i;
                bestStart = candidateStart;
            }
        }

        RecorderTuiLaunchJob next = std::move(runnable[best]);
        runnable.erase(runnable.begin() + static_cast<std::ptrdiff_t>(best));
        next.scheduledStartMs = bestStart;
        setLastStartForExchange(exchangeStarts, lowerAscii(next.job.exchange), bestStart);
        scheduled.push_back(std::move(next));
        ++launched;
    }
}

}  // namespace

const char* launchChannelName(LaunchChannel channel) noexcept {
    switch (channel) {
        case LaunchChannel::Trades: return "trades";
        case LaunchChannel::Liquidations: return "liquidations";
        case LaunchChannel::BookTicker: return "bookticker";
        case LaunchChannel::Orderbook: return "orderbook";
        case LaunchChannel::MarkPrice: return "mark_price";
        case LaunchChannel::IndexPrice: return "index_price";
        case LaunchChannel::Funding: return "funding";
        case LaunchChannel::PriceLimit: return "price_limit";
    }
    return "";
}

bool launchChannelSelected(const ChannelSelection& channels, LaunchChannel channel) noexcept {
    switch (channel) {
        case LaunchChannel::Trades: return channels.trades;
        case LaunchChannel::Liquidations: return channels.liquidations;
        case LaunchChannel::BookTicker: return channels.bookTicker;
        case LaunchChannel::Orderbook: return channels.orderbook;
        case LaunchChannel::MarkPrice: return channels.markPrice;
        case LaunchChannel::IndexPrice: return channels.indexPrice;
        case LaunchChannel::Funding: return channels.funding;
        case LaunchChannel::PriceLimit: return channels.priceLimit;
    }
    return false;
}

void setLaunchChannel(ChannelSelection& channels, LaunchChannel channel, bool enabled) noexcept {
    switch (channel) {
        case LaunchChannel::Trades: channels.trades = enabled; break;
        case LaunchChannel::Liquidations: channels.liquidations = enabled; break;
        case LaunchChannel::BookTicker: channels.bookTicker = enabled; break;
        case LaunchChannel::Orderbook: channels.orderbook = enabled; break;
        case LaunchChannel::MarkPrice: channels.markPrice = enabled; break;
        case LaunchChannel::IndexPrice: channels.indexPrice = enabled; break;
        case LaunchChannel::Funding: channels.funding = enabled; break;
        case LaunchChannel::PriceLimit: channels.priceLimit = enabled; break;
    }
}

bool anyLaunchChannelSelected(const ChannelSelection& channels) noexcept {
    return channels.trades || channels.liquidations || channels.bookTicker || channels.orderbook ||
           channels.markPrice || channels.indexPrice || channels.funding || channels.priceLimit;
}

bool requiresExclusiveMarketDataSession(const RecorderTuiJob& job) {
    return !exclusiveMarketDataSessionKey(job).empty();
}

std::string exclusiveMarketDataSessionKey(const RecorderTuiJob& job) {
    const std::string exchange = lowerAscii(job.exchange);
    const std::string market = lowerAscii(job.market);
    if (exchange == "binance" && market == "spot" &&
        (job.channels.trades || job.channels.bookTicker || job.channels.orderbook)) {
        return "binance|spot|market_data_fix";
    }
    return {};
}

RecorderTuiLaunchJob filterLaunchJobChannels(const RecorderTuiLaunchJob& planned,
                                             RecorderTuiChannelAvailabilityFn availability,
                                             void* userData) {
    return filterJobChannels(planned, availability, userData);
}

RecorderTuiLaunchPlan buildLaunchPlan(const RecorderTuiPreset& preset,
                                      RecorderTuiChannelAvailabilityFn availability,
                                      void* userData) {
    RecorderTuiLaunchPlan plan{};
    plan.launchWaveSize = std::max(1, preset.launchWaveSize);
    plan.launchStaggerMs = std::max(0, preset.launchStaggerMs);
    plan.sameExchangeCooldownMs = std::max(0, preset.sameExchangeCooldownMs);
    plan.maxActiveJobs = std::max(1, preset.maxActiveJobs);

    std::vector<RecorderTuiLaunchJob> runnable;
    std::vector<RecorderTuiLaunchJob> skipped;
    runnable.reserve(preset.jobs.size());
    skipped.reserve(preset.jobs.size());

    for (std::size_t i = 0; i < preset.jobs.size(); ++i) {
        RecorderTuiLaunchJob job = filterJobChannels(makePlannedJob(preset.jobs[i], i), availability, userData);
        if (job.skipJob) {
            skipped.push_back(std::move(job));
        } else {
            runnable.push_back(std::move(job));
        }
    }

    plan.runnableJobs = runnable.size();
    plan.skippedJobs = skipped.size();
    plan.jobs.reserve(runnable.size() + skipped.size());
    scheduleRunnableJobs(runnable, plan.launchWaveSize, plan.launchStaggerMs, plan.sameExchangeCooldownMs, plan.jobs);
    for (auto& job : skipped) plan.jobs.push_back(std::move(job));
    return plan;
}

}  // namespace hftrec::tui
