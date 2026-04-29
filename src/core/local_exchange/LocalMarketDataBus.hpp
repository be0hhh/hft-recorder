#pragma once

#include <atomic>
#include <string_view>

namespace hftrec::local_exchange {

/*
 * EN: Small in-process bus for recorder market-data fanout. Capture/replay code
 *     publishes already-rendered recorder JSON arrays, while GUI/network code owns
 *     the actual WebSocket clients.
 * RU: Небольшая внутренняя шина для fanout market-data. Capture/replay публикует
 *     уже готовые JSON-массивы recorder, а GUI/network слой владеет WebSocket клиентами.
 */
class ILocalMarketDataSink {
  public:
    virtual ~ILocalMarketDataSink() = default;
    virtual void onLocalMarketDataFrame(std::string_view channel,
                                        std::string_view symbol,
                                        std::string_view payload) noexcept = 0;
};

class LocalMarketDataBus {
  public:
    void setSink(ILocalMarketDataSink* sink) noexcept;
    void publish(std::string_view channel,
                 std::string_view symbol,
                 std::string_view payload) noexcept;

  private:
    std::atomic<ILocalMarketDataSink*> sink_{nullptr};
};

LocalMarketDataBus& globalLocalMarketDataBus() noexcept;

}  // namespace hftrec::local_exchange
