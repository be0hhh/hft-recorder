#pragma once

#include <core/storage/EventStorage.hpp>

namespace hftrec::market_data {

class IMarketDataIngress {
  public:
    IMarketDataIngress() = default;
    IMarketDataIngress(const IMarketDataIngress&) = delete;
    IMarketDataIngress& operator=(const IMarketDataIngress&) = delete;
    virtual ~IMarketDataIngress() = default;

    virtual const storage::IEventSource* eventSource() const noexcept = 0;
    virtual const storage::IHotEventCache* hotCache() const noexcept = 0;
};

}  // namespace hftrec::market_data
