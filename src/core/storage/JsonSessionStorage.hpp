#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

#include "core/capture/ChannelKind.hpp"
#include "core/common/Status.hpp"
#include "core/storage/EventStorage.hpp"

namespace hftrec::storage {

class JsonSessionSink final : public IStorageBackend {
  public:
    Status open(const std::filesystem::path& sessionDir) noexcept;
    Status ensureChannelFile(capture::ChannelKind channel) noexcept;

    const char* backendId() const noexcept override;
    EventStoreStats stats() const noexcept override;

    Status appendTrade(const replay::TradeRow& row) noexcept override;
    Status appendBookTicker(const replay::BookTickerRow& row) noexcept override;
    Status appendDepth(const replay::DepthRow& row) noexcept override;
    Status appendTradeLine(const replay::TradeRow& row, const std::string& line) noexcept;
    Status appendBookTickerLine(const replay::BookTickerRow& row, const std::string& line) noexcept;
    Status appendDepthLine(const replay::DepthRow& row, const std::string& line) noexcept;
    Status appendSnapshot(const replay::SnapshotDocument& snapshot,
                          std::uint64_t snapshotIndex) noexcept override;
    Status flush() noexcept override;
    Status close() noexcept override;

  private:
    Status ensureLineStream_(capture::ChannelKind channel, std::ofstream& stream) noexcept;
    Status writeLine_(capture::ChannelKind channel, std::ofstream& stream, const std::string& line) noexcept;

    std::filesystem::path sessionDir_{};
    std::ofstream trades_{};
    std::ofstream bookTicker_{};
    std::ofstream depth_{};
    mutable std::mutex mutex_{};
    EventStoreStats stats_{};
};

}  // namespace hftrec::storage
