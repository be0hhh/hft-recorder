#include "core/local_exchange/LocalMarketDataBus.hpp"

namespace hftrec::local_exchange {

void LocalMarketDataBus::setSink(ILocalMarketDataSink* sink) noexcept {
    sink_.store(sink, std::memory_order_release);
}

void LocalMarketDataBus::publish(std::string_view channel,
                                 std::string_view symbol,
                                 std::string_view payload) noexcept {
    ILocalMarketDataSink* sink = sink_.load(std::memory_order_acquire);
    if (sink != nullptr) sink->onLocalMarketDataFrame(channel, symbol, payload);
}

LocalMarketDataBus& globalLocalMarketDataBus() noexcept {
    static LocalMarketDataBus bus{};
    return bus;
}

}  // namespace hftrec::local_exchange
