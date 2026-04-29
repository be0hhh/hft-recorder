#include "core/capture/CaptureCoordinator.hpp"

namespace hftrec::capture {
namespace {
constexpr const char* kNoCxet = "hft-recorder was built without CXETCPP";
}

Status CaptureCoordinator::startTrades(const CaptureConfig&) noexcept {
    lastError_ = kNoCxet;
    return Status::Unimplemented;
}

Status CaptureCoordinator::requestStopTrades() noexcept {
    tradesStop_.store(true, std::memory_order_release);
    return Status::Ok;
}

Status CaptureCoordinator::stopTrades() noexcept {
    (void)requestStopTrades();
    if (tradesThread_.joinable()) tradesThread_.join();
    tradesRunning_.store(false, std::memory_order_release);
    return Status::Ok;
}

Status CaptureCoordinator::startLiquidations(const CaptureConfig&) noexcept {
    lastError_ = kNoCxet;
    return Status::Unimplemented;
}

Status CaptureCoordinator::requestStopLiquidations() noexcept {
    liquidationsStop_.store(true, std::memory_order_release);
    return Status::Ok;
}

Status CaptureCoordinator::stopLiquidations() noexcept {
    (void)requestStopLiquidations();
    if (liquidationsThread_.joinable()) liquidationsThread_.join();
    liquidationsRunning_.store(false, std::memory_order_release);
    return Status::Ok;
}

Status CaptureCoordinator::startBookTicker(const CaptureConfig&) noexcept {
    lastError_ = kNoCxet;
    return Status::Unimplemented;
}

Status CaptureCoordinator::requestStopBookTicker() noexcept {
    bookTickerStop_.store(true, std::memory_order_release);
    return Status::Ok;
}

Status CaptureCoordinator::stopBookTicker() noexcept {
    (void)requestStopBookTicker();
    if (bookTickerThread_.joinable()) bookTickerThread_.join();
    bookTickerRunning_.store(false, std::memory_order_release);
    return Status::Ok;
}

Status CaptureCoordinator::startOrderbook(const CaptureConfig&) noexcept {
    lastError_ = kNoCxet;
    return Status::Unimplemented;
}

Status CaptureCoordinator::requestStopOrderbook() noexcept {
    orderbookStop_.store(true, std::memory_order_release);
    return Status::Ok;
}

Status CaptureCoordinator::stopOrderbook() noexcept {
    (void)requestStopOrderbook();
    if (orderbookThread_.joinable()) orderbookThread_.join();
    orderbookRunning_.store(false, std::memory_order_release);
    return Status::Ok;
}

void CaptureCoordinator::reapStoppedThreads() noexcept {
    if (tradesThread_.joinable() && !tradesRunning_.load(std::memory_order_acquire)) tradesThread_.join();
    if (liquidationsThread_.joinable() && !liquidationsRunning_.load(std::memory_order_acquire)) liquidationsThread_.join();
    if (bookTickerThread_.joinable() && !bookTickerRunning_.load(std::memory_order_acquire)) bookTickerThread_.join();
    if (orderbookThread_.joinable() && !orderbookRunning_.load(std::memory_order_acquire)) orderbookThread_.join();
}

Status CaptureCoordinator::writeSnapshotFile(const cxet::composite::OrderBookSnapshot&,
                                             std::uint64_t,
                                             std::string_view,
                                             std::string_view,
                                             bool) noexcept {
    lastError_ = kNoCxet;
    return Status::Unimplemented;
}

}  // namespace hftrec::capture
