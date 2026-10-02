#pragma once

#include <MarketData/MarketDataIngress.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <cstdint>
#include <atomic>
#include <mutex>

#include "ChannelJsonWriter.hpp"
#include "SessionManifest.hpp"
#include "../Common/Status.hpp"
#include "Corpus/Recordings/RecordingRoot.hpp"
#include "../Corpus/Storage/EventStorage.hpp"
#include "../Corpus/Storage/JsonSessionStorage.hpp"

namespace cxet {
namespace composite {
struct OrderBookSnapshot;
}  // namespace composite
}  // namespace cxet

namespace hftrec::capture {

enum class LiveCacheMode : std::uint8_t {
    Off,
    Full
};

struct CaptureConfig {
    std::string exchange{"binance"};
    std::string market{"futures"};
    std::vector<std::string> symbols{};
    std::vector<std::string> routeSymbols{};
    std::filesystem::path envPath{".env"};
    std::uint8_t apiSlot{1u};
    std::filesystem::path outputDir{hftrec::recordings::defaultRecordingsRoot()};
    std::int64_t durationSec{0};
    std::int64_t snapshotIntervalSec{60};
    std::int64_t tradesHistoryWarmupSec{3600};
    std::int64_t tradesHistoryEndNs{0};
    std::uint32_t tradesHistoryPageLimit{1000u};
    std::uint32_t tradesHistoryMaxRows{0u};
    std::vector<std::string> tradesAliases{};
    std::vector<std::string> bookTickerAliases{};
    std::vector<std::string> orderbookAliases{};
    std::string tradesRequestCommand{};
    std::string bookTickerRequestCommand{};
    std::string orderbookRequestCommand{};
    std::string detailedCandlesTimeframe{"15m"};
    std::uint32_t detailedCandlesLimit{5000u};
    std::uint32_t detailedCandlesPageLimit{0u};
    std::uint32_t detailedCandlesMaxAttemptsPerPage{3u};
    std::uint32_t detailedCandlesMaxEmptyWindows{96u};
    std::int64_t detailedCandlesEndNs{0};
    LiveCacheMode liveCacheMode{LiveCacheMode::Off};
};

struct ExternalCaptureChannels {
    bool trades{false};
    bool bookTicker{false};
    bool orderbook{false};
};

class CaptureCoordinator : public market_data::IMarketDataIngress {
  public:
    CaptureCoordinator();
    ~CaptureCoordinator();

    Status ensureSession(const CaptureConfig& config) noexcept;
    Status startTrades(const CaptureConfig& config) noexcept;
    Status requestStopTrades() noexcept;
    Status stopTrades() noexcept;
    Status startBookTicker(const CaptureConfig& config) noexcept;
    Status requestStopBookTicker() noexcept;
    Status stopBookTicker() noexcept;
    Status startOrderbook(const CaptureConfig& config) noexcept;
    Status requestStopOrderbook() noexcept;
    Status stopOrderbook() noexcept;
    Status finalizeSession() noexcept;
    Status captureCandlesOnce(const CaptureConfig& config) noexcept;
    Status probeDetailedCandlesOnce(const CaptureConfig& config) noexcept;
    Status captureDetailedCandlesOnce(const CaptureConfig& config) noexcept;
    Status captureDetailedCandlesBulk(const CaptureConfig& config) noexcept;
    Status captureTradesHistoryOnce(const CaptureConfig& config) noexcept;
    Status startExternalCapture(const CaptureConfig& config,
                                const ExternalCaptureChannels& enabledChannels,
                                const ExternalCaptureChannels& requestedChannels) noexcept;
    Status appendExternalTrade(const replay::TradeRow& row) noexcept;
    Status appendExternalBookTicker(const replay::BookTickerRow& row) noexcept;
    Status appendExternalDepth(const replay::DepthRow& row) noexcept;
    void noteExternalCaptureLoss(std::string_view channel,std::uint64_t count) noexcept;
    void noteExternalChannelError(std::string_view channel, std::string_view error) noexcept;
    void noteExternalUnsupportedChannel(std::string_view channel, std::string_view error) noexcept;
    void noteExternalUnroutableEvent(std::string_view channel, std::string_view error) noexcept;
    void noteExternalChannelConnection(std::string_view channel, bool connected, bool reconnected) noexcept;
    Status refreshExternalManifest() noexcept;
    void reapStoppedThreads() noexcept;

    const SessionManifest& manifest() const noexcept { return manifest_; }
    SessionManifest manifestCopy() const;
    std::filesystem::path sessionDirCopy() const;
    const std::filesystem::path& sessionDir() const noexcept { return sessionDir_; }
    bool tradesRunning() const noexcept { return tradesRunning_.load(std::memory_order_acquire); }
    bool bookTickerRunning() const noexcept { return bookTickerRunning_.load(std::memory_order_acquire); }
    bool orderbookRunning() const noexcept { return orderbookRunning_.load(std::memory_order_acquire); }
    std::uint64_t tradesCount() const noexcept { return tradesCount_.load(std::memory_order_relaxed); }
    std::uint64_t bookTickerCount() const noexcept { return bookTickerCount_.load(std::memory_order_relaxed); }
    std::uint64_t depthCount() const noexcept { return depthCount_.load(std::memory_order_relaxed); }
    std::uint64_t candlesCount() const noexcept { return candlesCount_.load(std::memory_order_relaxed); }
    std::uint64_t candles2Count() const noexcept { return candles2Count_.load(std::memory_order_relaxed); }
    std::string lastError() const;
    storage::EventBatch liveEventsCopy() const;
    const storage::IEventSource* liveEventSource() const noexcept {
        return liveCacheEnabled_.load(std::memory_order_acquire) ? &liveStore_ : nullptr;
    }
    const storage::IEventSource* eventSource() const noexcept override { return liveEventSource(); }
    const storage::IHotEventCache* hotCache() const noexcept override {
        return liveCacheEnabled_.load(std::memory_order_acquire) ? &liveStore_ : nullptr;
    }

	  private:
	    static void noteExternalRow_(ChannelRuntimeHealth& health, std::int64_t tsNs) noexcept;
	    void noteArrival_(const replay::EventArrival& arrival,
	                      std::int64_t exchangeTsNs) noexcept;
	    Status accountExternalAppend_(Status status,
	                                  ChannelRuntimeHealth& health,
	                                  std::atomic<std::uint64_t>& counter,
	                                  std::int64_t tsNs,
	                                  const replay::EventArrival& arrival,
	                                  std::string_view channel) noexcept;
	    Status ensureSession_(const CaptureConfig& config, bool allowMultiSymbol) noexcept;
	    void resetSessionState() noexcept;
    bool sessionOpen() const noexcept;
    enum class ManagedStreamKind : std::uint8_t {
        Trades,
        BookTicker,
        Orderbook
    };

    Status startManagedMarketData_(const CaptureConfig& config, ManagedStreamKind stream) noexcept;
    void requestStopManagedMarketData_(ManagedStreamKind stream) noexcept;
    void joinManagedMarketDataIfIdle_() noexcept;
    bool anyManagedMarketDataDesired_() const noexcept;
    void marketDataManagerLoop_(CaptureConfig config) noexcept;
    void refreshRecordingManifestLocked_(std::int64_t nowNs) noexcept;
    Status flushRecordingManifestIfDue_(std::int64_t& nextFlushNs) noexcept;
    void syncManifestIntegrityFromReplay_() noexcept;
    Status writeManifestFile_() noexcept;
    Status writeStartupFailureManifest_(std::string_view reason) noexcept;
    Status writeInstrumentMetadataFile() noexcept;
    Status refreshInstrumentMetadataFromExchangeInfo() noexcept;
    Status writeSupportArtifacts() noexcept;
    bool liveCacheEnabled() const noexcept { return liveCacheEnabled_.load(std::memory_order_acquire); }
    Status appendLiveTrade(const replay::TradeRow& row) noexcept;
    Status appendLiveBookTicker(const replay::BookTickerRow& row) noexcept;
    Status appendLiveDepth(const replay::DepthRow& row) noexcept;

    SessionManifest manifest_{};
    std::filesystem::path sessionDir_{};
    ChannelJsonWriter tradesWriter_{};
    ChannelJsonWriter bookTickerWriter_{};
    ChannelJsonWriter candlesWriter_{};
    ChannelJsonWriter candles2Writer_{};
    storage::LiveEventStore liveStore_{};
    storage::JsonSessionSink jsonSink_{};
    storage::CompositeEventSink eventSink_{};
    CaptureConfig config_{};
    std::atomic<bool> tradesRunning_{false};
    std::atomic<bool> bookTickerRunning_{false};
    std::atomic<bool> orderbookRunning_{false};
    std::atomic<bool> tradesStop_{false};
    std::atomic<bool> bookTickerStop_{false};
    std::atomic<bool> orderbookStop_{false};
    std::atomic<std::uint64_t> tradesCount_{0};
    std::atomic<std::uint64_t> bookTickerCount_{0};
    std::atomic<std::uint64_t> depthCount_{0};
    std::atomic<std::uint64_t> candlesCount_{0};
    std::atomic<std::uint64_t> candles2Count_{0};
    std::atomic<std::uint64_t> tradesCaptureSeq_{0};
    std::atomic<std::uint64_t> bookTickerCaptureSeq_{0};
    std::atomic<std::uint64_t> ingestSeq_{0};
    std::atomic<bool> liveCacheEnabled_{false};
    bool instrumentMetadataReady_{false};
    mutable std::mutex stateMutex_{};
    std::thread marketDataThread_{};
    std::thread tradesThread_{};
    std::thread bookTickerThread_{};
    std::thread orderbookThread_{};
    std::atomic<bool> marketDataRunning_{false};
    std::atomic<bool> marketDataStop_{false};
    std::atomic<bool> desiredTrades_{false};
    std::atomic<bool> desiredBookTicker_{false};
    std::atomic<bool> desiredOrderbook_{false};
    std::string lastError_{};
};

}  // namespace hftrec::capture
