#pragma once

#include <core/market_data/MarketDataIngress.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <cstdint>
#include <atomic>
#include <mutex>

#include "core/capture/ChannelJsonWriter.hpp"
#include "core/capture/SessionManifest.hpp"
#include "core/common/Status.hpp"
#include "core/storage/EventStorage.hpp"
#include "core/storage/JsonSessionStorage.hpp"

namespace cxet {
namespace composite {
struct OrderBookSnapshot;
}  // namespace composite
}  // namespace cxet

namespace hftrec::capture {

struct CaptureConfig {
    std::string exchange{"binance"};
    std::string market{"futures"};
    std::vector<std::string> symbols{};
    std::filesystem::path envPath{".env"};
    std::uint8_t apiSlot{1u};
    std::filesystem::path outputDir{"./recordings"};
    std::int64_t durationSec{0};
    std::int64_t snapshotIntervalSec{60};
    std::int64_t tradesHistoryWarmupSec{3600};
    std::vector<std::string> tradesAliases{};
    std::vector<std::string> liquidationAliases{};
    std::vector<std::string> bookTickerAliases{};
    std::vector<std::string> orderbookAliases{};
    std::string tradesRequestCommand{};
    std::string liquidationRequestCommand{};
    std::string bookTickerRequestCommand{};
    std::string orderbookRequestCommand{};
    std::string detailedCandlesTimeframe{"15m"};
    std::uint32_t detailedCandlesLimit{5000u};
    std::int64_t detailedCandlesEndNs{0};
    std::string detailedCandlesUnderlyingSymbolHint{};
};

class CaptureCoordinator : public market_data::IMarketDataIngress {
  public:
    CaptureCoordinator();
    ~CaptureCoordinator();

    Status ensureSession(const CaptureConfig& config) noexcept;
    Status startTrades(const CaptureConfig& config) noexcept;
    Status requestStopTrades() noexcept;
    Status stopTrades() noexcept;
    Status startLiquidations(const CaptureConfig& config) noexcept;
    Status requestStopLiquidations() noexcept;
    Status stopLiquidations() noexcept;
    Status startBookTicker(const CaptureConfig& config) noexcept;
    Status requestStopBookTicker() noexcept;
    Status stopBookTicker() noexcept;
    Status startOrderbook(const CaptureConfig& config) noexcept;
    Status requestStopOrderbook() noexcept;
    Status stopOrderbook() noexcept;
    Status startMarkPrice(const CaptureConfig& config) noexcept;
    Status requestStopMarkPrice() noexcept;
    Status stopMarkPrice() noexcept;
    Status startIndexPrice(const CaptureConfig& config) noexcept;
    Status requestStopIndexPrice() noexcept;
    Status stopIndexPrice() noexcept;
    Status startFunding(const CaptureConfig& config) noexcept;
    Status requestStopFunding() noexcept;
    Status stopFunding() noexcept;
    Status startPriceLimit(const CaptureConfig& config) noexcept;
    Status requestStopPriceLimit() noexcept;
    Status stopPriceLimit() noexcept;
    Status finalizeSession() noexcept;
    Status captureCandlesOnce(const CaptureConfig& config) noexcept;
    Status captureDetailedCandlesOnce(const CaptureConfig& config) noexcept;
    void reapStoppedThreads() noexcept;

    const SessionManifest& manifest() const noexcept { return manifest_; }
    SessionManifest manifestCopy() const;
    std::filesystem::path sessionDirCopy() const;
    const std::filesystem::path& sessionDir() const noexcept { return sessionDir_; }
    bool tradesRunning() const noexcept { return tradesRunning_.load(std::memory_order_acquire); }
    bool liquidationsRunning() const noexcept { return liquidationsRunning_.load(std::memory_order_acquire); }
    bool bookTickerRunning() const noexcept { return bookTickerRunning_.load(std::memory_order_acquire); }
    bool orderbookRunning() const noexcept { return orderbookRunning_.load(std::memory_order_acquire); }
    bool markPriceRunning() const noexcept { return markPriceRunning_.load(std::memory_order_acquire); }
    bool indexPriceRunning() const noexcept { return indexPriceRunning_.load(std::memory_order_acquire); }
    bool fundingRunning() const noexcept { return fundingRunning_.load(std::memory_order_acquire); }
    bool priceLimitRunning() const noexcept { return priceLimitRunning_.load(std::memory_order_acquire); }
    std::uint64_t tradesCount() const noexcept { return tradesCount_.load(std::memory_order_relaxed); }
    std::uint64_t liquidationsCount() const noexcept { return liquidationsCount_.load(std::memory_order_relaxed); }
    std::uint64_t bookTickerCount() const noexcept { return bookTickerCount_.load(std::memory_order_relaxed); }
    std::uint64_t markPriceCount() const noexcept { return markPriceCount_.load(std::memory_order_relaxed); }
    std::uint64_t indexPriceCount() const noexcept { return indexPriceCount_.load(std::memory_order_relaxed); }
    std::uint64_t fundingCount() const noexcept { return fundingCount_.load(std::memory_order_relaxed); }
    std::uint64_t priceLimitCount() const noexcept { return priceLimitCount_.load(std::memory_order_relaxed); }
    std::uint64_t depthCount() const noexcept { return depthCount_.load(std::memory_order_relaxed); }
    std::uint64_t candlesCount() const noexcept { return candlesCount_.load(std::memory_order_relaxed); }
    std::uint64_t candles2Count() const noexcept { return candles2Count_.load(std::memory_order_relaxed); }
    std::string lastError() const;
    storage::EventBatch liveEventsCopy() const;
    const storage::IEventSource* liveEventSource() const noexcept { return &liveStore_; }
    const storage::IEventSource* eventSource() const noexcept override { return &liveStore_; }
    const storage::IHotEventCache* hotCache() const noexcept override { return &liveStore_; }

  private:
    void resetSessionState() noexcept;
    bool sessionOpen() const noexcept;
    Status writeSnapshotFile(const cxet::composite::OrderBookSnapshot& snapshot,
                             std::uint64_t snapshotIndex,
                             std::string_view snapshotKind,
                             std::string_view source,
                             bool trustedReplayAnchor) noexcept;
    enum class ManagedStreamKind : std::uint8_t {
        Trades,
        BookTicker,
        Orderbook,
        MarkPrice,
        IndexPrice,
        Funding,
        PriceLimit
    };

    Status startManagedMarketData_(const CaptureConfig& config, ManagedStreamKind stream) noexcept;
    void requestStopManagedMarketData_(ManagedStreamKind stream) noexcept;
    void joinManagedMarketDataIfIdle_() noexcept;
    bool anyManagedMarketDataDesired_() const noexcept;
    void marketDataManagerLoop_(CaptureConfig config) noexcept;
    void referenceDataManagerLoop_(CaptureConfig config) noexcept;
    void liquidationsLoop_(CaptureConfig config) noexcept;
    void refreshRecordingManifestLocked_(std::int64_t nowNs) noexcept;
    Status flushRecordingManifestIfDue_(std::int64_t& nextFlushNs) noexcept;
    void syncManifestIntegrityFromReplay_() noexcept;
    Status writeManifestFile_() noexcept;
    Status writeInstrumentMetadataFile() noexcept;
    Status writeSupportArtifacts() noexcept;

    SessionManifest manifest_{};
    std::filesystem::path sessionDir_{};
    ChannelJsonWriter tradesWriter_{};
    ChannelJsonWriter liquidationsWriter_{};
    ChannelJsonWriter bookTickerWriter_{};
    ChannelJsonWriter candlesWriter_{};
    ChannelJsonWriter candles2Writer_{};
    ChannelJsonWriter depthWriter_{};
    storage::LiveEventStore liveStore_{};
    storage::JsonSessionSink jsonSink_{};
    storage::CompositeEventSink eventSink_{};
    CaptureConfig config_{};
    std::atomic<bool> tradesRunning_{false};
    std::atomic<bool> liquidationsRunning_{false};
    std::atomic<bool> bookTickerRunning_{false};
    std::atomic<bool> orderbookRunning_{false};
    std::atomic<bool> markPriceRunning_{false};
    std::atomic<bool> indexPriceRunning_{false};
    std::atomic<bool> fundingRunning_{false};
    std::atomic<bool> priceLimitRunning_{false};
    std::atomic<bool> tradesStop_{false};
    std::atomic<bool> liquidationsStop_{false};
    std::atomic<bool> bookTickerStop_{false};
    std::atomic<bool> orderbookStop_{false};
    std::atomic<bool> markPriceStop_{false};
    std::atomic<bool> indexPriceStop_{false};
    std::atomic<bool> fundingStop_{false};
    std::atomic<bool> priceLimitStop_{false};
    std::atomic<std::uint64_t> tradesCount_{0};
    std::atomic<std::uint64_t> liquidationsCount_{0};
    std::atomic<std::uint64_t> bookTickerCount_{0};
    std::atomic<std::uint64_t> markPriceCount_{0};
    std::atomic<std::uint64_t> indexPriceCount_{0};
    std::atomic<std::uint64_t> fundingCount_{0};
    std::atomic<std::uint64_t> priceLimitCount_{0};
    std::atomic<std::uint64_t> depthCount_{0};
    std::atomic<std::uint64_t> candlesCount_{0};
    std::atomic<std::uint64_t> candles2Count_{0};
    std::atomic<std::uint64_t> snapshotCount_{0};
    std::atomic<std::uint64_t> tradesCaptureSeq_{0};
    std::atomic<std::uint64_t> liquidationsCaptureSeq_{0};
    std::atomic<std::uint64_t> bookTickerCaptureSeq_{0};
    std::atomic<std::uint64_t> ingestSeq_{0};
    mutable std::mutex stateMutex_{};
    std::thread marketDataThread_{};
    std::thread referenceDataThread_{};
    std::thread tradesThread_{};
    std::thread liquidationsThread_{};
    std::thread bookTickerThread_{};
    std::thread orderbookThread_{};
    std::atomic<bool> marketDataRunning_{false};
    std::atomic<bool> marketDataStop_{false};
    std::atomic<bool> referenceDataRunning_{false};
    std::atomic<bool> referenceDataStop_{false};
    std::atomic<bool> desiredTrades_{false};
    std::atomic<bool> desiredBookTicker_{false};
    std::atomic<bool> desiredOrderbook_{false};
    std::atomic<bool> desiredMarkPrice_{false};
    std::atomic<bool> desiredIndexPrice_{false};
    std::atomic<bool> desiredFunding_{false};
    std::atomic<bool> desiredPriceLimit_{false};
    std::string lastError_{};
};

}  // namespace hftrec::capture
