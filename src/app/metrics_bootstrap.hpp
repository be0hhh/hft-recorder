#pragma once

#include <cstdint>

namespace hftrec::app {

class MetricsBootstrap {
public:
    MetricsBootstrap() noexcept;
    ~MetricsBootstrap() noexcept;

    MetricsBootstrap(const MetricsBootstrap&) = delete;
    MetricsBootstrap& operator=(const MetricsBootstrap&) = delete;

private:
    struct Impl;
    Impl* impl_{nullptr};
};

}  // namespace hftrec::app
