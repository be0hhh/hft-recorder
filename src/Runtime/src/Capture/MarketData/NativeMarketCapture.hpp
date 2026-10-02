#pragma once
#include "../CaptureCoordinator.hpp"
#include "cxet/Runtime/Market/ConfiguredMarketOwner.hpp"
#include "cxet/Runtime/Market/MarketRuntime.hpp"
#include "../Stream/SpscRing.hpp"
#include <atomic>
#include <array>
#include <mutex>
#include <thread>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace hftrec::capture {
struct NativeCaptureSource final {
    cxet::runtime::market::ConfiguredMarketSource native{};
    const CaptureConfig* config{nullptr};
    CaptureCoordinator* sink{nullptr};
};
class NativeMarketCapture;
struct NativeCaptureHooks final {
  template<class View>
  void onTrade(const cxet::runtime::market::CommitMetadata& metadata,
               const View& view) noexcept {
    namespace role = cxet::contract::semantic;
    cxet::runtime::market::TradeCommit value{};
    value.metadata = metadata;
    value.value.eventId.raw = view.template get<role::TradeId>().value;
    value.value.price.raw = view.template get<role::Price>().value;
    value.value.qty.raw = view.template get<role::Quantity>().value;
    value.value.ts.raw = view.template get<role::EventTime>().value;
    value.value.initiatorSide = view.template get<role::Side>().value;
    retainTrade(value);
  }
  template<class View>
  void onBbo(const cxet::runtime::market::CommitMetadata& metadata,
             const View& view) noexcept {
    namespace role = cxet::contract::semantic;
    cxet::runtime::market::BboCommit value{};
    value.metadata = metadata;
    value.value.eventId.raw = view.template get<role::Sequence>().value;
    value.value.bid.px.raw = view.template get<role::BidPrice>().value;
    value.value.bid.qty.raw = view.template get<role::BidQuantity>().value;
    value.value.ask.px.raw = view.template get<role::AskPrice>().value;
    value.value.ask.qty.raw = view.template get<role::AskQuantity>().value;
    value.value.ts.raw = view.template get<role::EventTime>().value;
    value.bidPresent = view.template get<role::BidPrice>().present;
    value.askPresent = view.template get<role::AskPrice>().present;
    retainBbo(value);
  }
    using TradeDemand = cxet::runtime::market::FullTradeDemand;
    using BboDemand = cxet::runtime::market::FullBboDemand;
    NativeMarketCapture* owner{nullptr};
    bool observeTransport(std::uint64_t) noexcept;
    bool progressAccountOrders(std::uint64_t) noexcept {return true;}
    bool progressControl(std::uint64_t) noexcept {return true;}
    bool progressColdPolicy(std::uint64_t) noexcept {return true;}
    bool progressRetirement(std::uint64_t) noexcept {return true;}
    void onDepth(const cxet::runtime::market::CommitMetadata&,
                 const cxet::api::market::PublicMarketDepthFrame&) noexcept;
 private:
    void retainBbo(const cxet::runtime::market::BboCommit&) noexcept;
    void retainTrade(const cxet::runtime::market::TradeCommit&) noexcept;
};
// Recorder's cold topology and row sink. The sole iteration and all native
// parsing, wire selection and transport lifecycle belong to Core.
class NativeMarketCapture final {
 public:
    ~NativeMarketCapture() noexcept {shutdown();}
    bool configure(std::span<const NativeCaptureSource>,const char* envPath,std::string&) noexcept;
    bool iterate(std::uint64_t now) noexcept;
    void shutdown() noexcept;
    bool connected(std::size_t source) const noexcept;
    std::uint64_t committedRows() const noexcept {return committedRows_.load(std::memory_order_acquire);}
    std::string error() const noexcept;
 private:
    friend struct NativeCaptureHooks;
    void onBbo(const cxet::runtime::market::BboCommit&) noexcept;
    void onTrade(const cxet::runtime::market::TradeCommit&) noexcept;
    void onDepth(const cxet::runtime::market::CommitMetadata&,
                 const cxet::api::market::PublicMarketDepthFrame&) noexcept;
    struct Event final {
        cxet::runtime::market::CommitMetadata metadata{};
        cxet::api::market::PublicMarketObject object{};
        cxet::composite::Trade trade{};
        cxet::composite::BookTicker bbo{};
        cxet::api::market::PublicMarketDepthFrame depth{};
        std::uint64_t rowEnd{0u};
        bool bidPresent{false};bool askPresent{false};
    };
    void publish(const Event&) noexcept;
    void lost(std::uint32_t sourceId) noexcept;
    void drainWorker() noexcept;
    void consume(const Event&) noexcept;
    const NativeCaptureSource* source(std::uint32_t) noexcept;
    replay::EventArrival arrival(const cxet::runtime::market::CommitMetadata&,std::uint64_t) noexcept;
    void account(Status,const NativeCaptureSource&,std::string_view) noexcept;
    NativeCaptureHooks hooks_{this};
    cxet::runtime::market::MarketRuntime<NativeCaptureHooks> core_{hooks_};
    std::unique_ptr<cxet::runtime::market::ConfiguredMarketOwner> owner_{};
    std::vector<NativeCaptureSource> sources_{};
    std::uint64_t producerEpoch_{0u};
    std::uint64_t shardSequence_{0u};
    std::atomic<std::uint64_t> committedRows_{0u};
    std::atomic<bool> failed_{false};
    std::atomic<std::uint64_t> lostEvents_{0u};
    std::array<std::atomic<std::uint64_t>,60u> lostBySource_{};
    std::atomic<bool> stopStorage_{false};
    std::unique_ptr<hftrec::SpscRing<Event,128u>> queue_{};
    std::unique_ptr<cxet::market::DepthMutation[]> rows_{};
    std::size_t rowCapacity_{0u};
    std::uint64_t rowHead_{0u};
    std::atomic<std::uint64_t> rowTail_{0u};
    std::unique_ptr<std::thread> storageThread_{};
    mutable std::mutex errorMutex_{};
    std::string error_{};
};
} // namespace hftrec::capture
