#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

#include "core/common/Status.hpp"

namespace hftrec::local_exchange {

enum class LocalReplayMode : std::uint8_t {
    Replay = 0,
    LiveReserved = 1,
    ArchiveReserved = 2,
};

struct LocalReplayConfig {
    LocalReplayMode mode{LocalReplayMode::Replay};
    std::filesystem::path sessionPath{};
    std::string symbolOverride{};
    std::uint64_t speedMultiplier{1};
    std::uint64_t repeatCount{1};
    bool maxSpeed{false};
    bool startPaused{false};
    bool publishSnapshot{true};
    bool resetOrderEngineOnStart{true};
};

struct LocalReplayStats {
    Status status{Status::Ok};
    std::size_t loopsCompleted{0};
    std::size_t bucketsDelivered{0};
    std::size_t eventsDelivered{0};
    std::int64_t replayTimeNs{0};
    bool stopped{false};
};

class LocalReplayRunner {
  public:
    LocalReplayRunner() = default;
    LocalReplayRunner(const LocalReplayRunner&) = delete;
    LocalReplayRunner& operator=(const LocalReplayRunner&) = delete;
    ~LocalReplayRunner();

    bool start(LocalReplayConfig config) noexcept;
    Status runBlocking(const LocalReplayConfig& config, LocalReplayStats* outStats = nullptr) noexcept;
    void requestStop() noexcept;
    void stop() noexcept;
    void setPaused(bool paused) noexcept;
    bool running() const noexcept { return running_.load(std::memory_order_acquire); }
    bool paused() const noexcept { return paused_.load(std::memory_order_acquire); }
    LocalReplayStats lastStats() const noexcept;
    std::string lastError() const;

  private:
    Status runReplay_(const LocalReplayConfig& config, LocalReplayStats& stats) noexcept;

    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> running_{false};
    std::thread worker_{};
    mutable std::mutex statsMutex_{};
    LocalReplayStats lastStats_{};
    std::string lastError_{};
};

}  // namespace hftrec::local_exchange