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
    std::string market{"futures_usd"};
    std::vector<std::string> symbols{};
    std::filesystem::path outputDir{"./recordings"};
    std::int64_t durationSec{1800};
    std::int64_t snapshotIntervalSec{60};
    std::vector<std::string> tradesAliases{};
    std::vector<std::string> liquidationAliases{};
    std::vector<std::string> bookTickerAliases{};
    std::vector<std::string> orderbookAliases{};
    std::string tradesRequestCommand{};
    std::string liquidationRequestCommand{};
    std::string bookTickerRequestCommand{};
    std::string orderbookRequestCommand{};
};

class CaptureCoordinator : public market_data::IMarketDataIngress {
  public:
    CaptureCoordinator();
    ~CaptureCoordinator();

    Status ensureSession(const CaptureConfig& config) noexcept;
    Status startTrades(const CaptureConfig& config) noexcept;
    Status stopTrades() noexcept;
    Status startLiquidations(const CaptureConfig& config) noexcept;
    Status stopLiquidations() noexcept;
    Status startBookTicker(const CaptureConfig& config) noexcept;
    Status stopBookTicker() noexcept;
    Status startOrderbook(const CaptureConfig& config) noexcept;
    Status stopOrderbook() noexcept;
    Status finalizeSession() noexcept;

    const SessionManifest& manifest() const noexcept { return manifest_; }
    SessionManifest manifestCopy() const;
    std::filesystem::path sessionDirCopy() const;
    const std::filesystem::path& sessionDir() const noexcept { return sessionDir_; }
    bool tradesRunning() const noexcept { return tradesRunning_.load(std::memory_order_acquire); }
    bool liquidationsRunning() const noexcept { return liquidationsRunning_.load(std::memory_order_acquire); }
    bool bookTickerRunning() const noexcept { return bookTickerRunning_.load(std::memory_order_acquire); }
    bool orderbookRunning() const noexcept { return orderbookRunning_.load(std::memory_order_acquire); }
    std::uint64_t tradesCount() const noexcept { return tradesCount_.load(std::memory_order_relaxed); }
    std::uint64_t liquidationsCount() const noexcept { return liquidationsCount_.load(std::memory_order_relaxed); }
    std::uint64_t bookTickerCount() const noexcept { return bookTickerCount_.load(std::memory_order_relaxed); }
    std::uint64_t depthCount() const noexcept { return depthCount_.load(std::memory_order_relaxed); }
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
    void syncManifestIntegrityFromReplay_() noexcept;
    Status writeInstrumentMetadataFile() noexcept;
    Status writeSupportArtifacts() noexcept;

    SessionManifest manifest_{};
    std::filesystem::path sessionDir_{};
    ChannelJsonWriter tradesWriter_{};
    ChannelJsonWriter liquidationsWriter_{};
    ChannelJsonWriter bookTickerWriter_{};
    ChannelJsonWriter depthWriter_{};
    storage::LiveEventStore liveStore_{};
    storage::JsonSessionSink jsonSink_{};
    storage::CompositeEventSink eventSink_{};
    CaptureConfig config_{};
    std::atomic<bool> tradesRunning_{false};
    std::atomic<bool> liquidationsRunning_{false};
    std::atomic<bool> bookTickerRunning_{false};
    std::atomic<bool> orderbookRunning_{false};
    std::atomic<bool> tradesStop_{false};
    std::atomic<bool> liquidationsStop_{false};
    std::atomic<bool> bookTickerStop_{false};
    std::atomic<bool> orderbookStop_{false};
    std::atomic<std::uint64_t> tradesCount_{0};
    std::atomic<std::uint64_t> liquidationsCount_{0};
    std::atomic<std::uint64_t> bookTickerCount_{0};
    std::atomic<std::uint64_t> depthCount_{0};
    std::atomic<std::uint64_t> snapshotCount_{0};
    std::atomic<std::uint64_t> tradesCaptureSeq_{0};
    std::atomic<std::uint64_t> liquidationsCaptureSeq_{0};
    std::atomic<std::uint64_t> bookTickerCaptureSeq_{0};
    std::atomic<std::uint64_t> ingestSeq_{0};
    mutable std::mutex stateMutex_{};
    std::thread tradesThread_{};
    std::thread liquidationsThread_{};
    std::thread bookTickerThread_{};
    std::thread orderbookThread_{};
    std::string lastError_{};
};

}  // namespace hftrec::capture
