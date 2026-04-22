#pragma once

#include <core/market_data/MarketDataIngress.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "core/common/Status.hpp"
#include "core/replay/EventRows.hpp"

namespace hftrec::storage {
class IEventSource;
}

namespace hftrec::gui::viewer {

struct LiveDataBatch {
    std::uint64_t id{0};
    std::vector<hftrec::replay::TradeRow> trades{};
    std::vector<hftrec::replay::BookTickerRow> bookTickers{};
    std::vector<hftrec::replay::DepthRow> depths{};
    std::vector<hftrec::replay::SnapshotDocument> snapshots{};
};

struct LiveDataCache {
    LiveDataBatch stableRows{};
    LiveDataBatch overlayRows{};
    std::uint64_t version{0};
};

struct LiveDataRangeRequest {
    std::string symbol{};
    std::int64_t tsMin{0};
    std::int64_t tsMax{0};
};

struct LiveDataStats {
    std::uint64_t tradesTotal{0};
    std::uint64_t bookTickersTotal{0};
    std::uint64_t depthsTotal{0};
    std::uint64_t snapshotsTotal{0};
    std::uint64_t version{0};
};

struct LiveDataProviderConfig {
    std::filesystem::path sessionDir{};
    std::string symbol{};
    std::string sourceId{};
};

struct LiveDataPollResult {
    LiveDataBatch batch{};
    bool appendedRows{false};
    bool reloadRequired{false};
    Status failureStatus{Status::Ok};
    std::string failureDetail{};
};

class ILiveDataProvider {
  public:
    ILiveDataProvider() = default;
    ILiveDataProvider(const ILiveDataProvider&) = delete;
    ILiveDataProvider& operator=(const ILiveDataProvider&) = delete;
    virtual ~ILiveDataProvider() = default;

    virtual void start(const LiveDataProviderConfig& config) = 0;
    virtual void stop() noexcept = 0;
    virtual LiveDataPollResult pollHot(std::uint64_t nextBatchId) = 0;
    virtual LiveDataBatch materializeRange(const LiveDataRangeRequest& request,
                                           std::uint64_t batchId) const = 0;
    virtual LiveDataStats stats() const noexcept = 0;
};

class JsonTailLiveDataProvider final : public ILiveDataProvider {
  public:
    struct TailFile {
        std::filesystem::path path{};
        std::uintmax_t offset{0};
        std::string pending{};
    };

    void start(const LiveDataProviderConfig& config) override;
    void stop() noexcept override;
    LiveDataPollResult pollHot(std::uint64_t nextBatchId) override;
    LiveDataBatch materializeRange(const LiveDataRangeRequest& request,
                                   std::uint64_t batchId) const override;
    LiveDataStats stats() const noexcept override;

  private:
    void syncTailOffset_(TailFile& file) noexcept;
    std::filesystem::path findLatestSnapshotPath_() const;

    std::filesystem::path sessionDir_{};
    TailFile trades_{};
    TailFile bookTicker_{};
    TailFile depth_{};
    std::filesystem::path snapshotPath_{};
    bool snapshotLoaded_{false};
    hftrec::replay::SnapshotDocument snapshot_{};
    std::vector<hftrec::replay::TradeRow> tradesHistory_{};
    std::vector<hftrec::replay::BookTickerRow> bookTickerHistory_{};
    std::vector<hftrec::replay::DepthRow> depthHistory_{};
    std::uint64_t version_{0};
};

class InMemoryLiveDataProvider final : public ILiveDataProvider {
  public:
    struct SourceRef {
        std::string sourceId{};
        std::string exchange{};
        std::string market{};
        std::string symbol{};
        const hftrec::storage::IEventSource* source{nullptr};
    };

    explicit InMemoryLiveDataProvider(std::vector<SourceRef> sources);

    void start(const LiveDataProviderConfig& config) override;
    void stop() noexcept override;
    LiveDataPollResult pollHot(std::uint64_t nextBatchId) override;
    LiveDataBatch materializeRange(const LiveDataRangeRequest& request,
                                   std::uint64_t batchId) const override;
    LiveDataStats stats() const noexcept override;

  private:
    struct SourceState {
        SourceRef ref{};
        std::size_t seenTrades{0};
        std::size_t seenBookTickers{0};
        std::size_t seenDepths{0};
        std::size_t seenSnapshots{0};
    };

    bool sourceMatches_(const SourceState& state, std::string_view sourceId, std::string_view symbol) const noexcept;

    std::vector<SourceState> sources_{};
    std::string activeSourceId_{};
    std::string activeSymbol_{};
    std::uint64_t version_{0};
    LiveDataStats cachedStats_{};
};

class LiveDataRegistry {
  public:
    struct RegisteredSource {
        std::string viewerSourceId{};
        std::string exchange{};
        std::string market{};
        std::string symbol{};
        std::string sessionId{};
        const hftrec::market_data::IMarketDataIngress* ingress{nullptr};
    };

    static LiveDataRegistry& instance() noexcept;

    void setSources(std::vector<RegisteredSource> sources);
    void clear() noexcept;
    std::unique_ptr<ILiveDataProvider> makeProvider(std::string_view sourceId) const;
    bool hasSource(std::string_view sourceId) const noexcept;
    bool hasSources() const noexcept;
    std::vector<RegisteredSource> snapshotSources() const;

  private:
    mutable std::mutex mutex_{};
    std::vector<RegisteredSource> sources_{};
};

}  // namespace hftrec::gui::viewer
