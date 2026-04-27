#include "core/capture/CaptureCoordinator.hpp"

namespace hftrec::capture {
namespace {
constexpr const char* kNoCxet = "hft-recorder was built without CXETCPP";
}

Status CaptureCoordinator::startTrades(const CaptureConfig&) noexcept {
    lastError_ = kNoCxet;
    return Status::Unimplemented;
}

Status CaptureCoordinator::stopTrades() noexcept {
    tradesStop_.store(true, std::memory_order_release);
    if (tradesThread_.joinable()) tradesThread_.join();
    tradesRunning_.store(false, std::memory_order_release);
    return Status::Ok;
}

Status CaptureCoordinator::startLiquidations(const CaptureConfig&) noexcept {
    lastError_ = kNoCxet;
    return Status::Unimplemented;
}

Status CaptureCoordinator::stopLiquidations() noexcept {
    liquidationsStop_.store(true, std::memory_order_release);
    if (liquidationsThread_.joinable()) liquidationsThread_.join();
    liquidationsRunning_.store(false, std::memory_order_release);
    return Status::Ok;
}

Status CaptureCoordinator::startBookTicker(const CaptureConfig&) noexcept {
    lastError_ = kNoCxet;
    return Status::Unimplemented;
}

Status CaptureCoordinator::stopBookTicker() noexcept {
    bookTickerStop_.store(true, std::memory_order_release);
    if (bookTickerThread_.joinable()) bookTickerThread_.join();
    bookTickerRunning_.store(false, std::memory_order_release);
    return Status::Ok;
}

Status CaptureCoordinator::startOrderbook(const CaptureConfig&) noexcept {
    lastError_ = kNoCxet;
    return Status::Unimplemented;
}

Status CaptureCoordinator::stopOrderbook() noexcept {
    orderbookStop_.store(true, std::memory_order_release);
    if (orderbookThread_.joinable()) orderbookThread_.join();
    orderbookRunning_.store(false, std::memory_order_release);
    return Status::Ok;
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