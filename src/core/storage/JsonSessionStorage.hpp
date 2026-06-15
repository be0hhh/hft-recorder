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
    Status appendLiquidation(const replay::LiquidationRow& row) noexcept override;
    Status appendBookTicker(const replay::BookTickerRow& row) noexcept override;
    Status appendMarkPrice(const replay::MarkPriceRow& row) noexcept override;
    Status appendIndexPrice(const replay::IndexPriceRow& row) noexcept override;
    Status appendFunding(const replay::FundingRow& row) noexcept override;
    Status appendPriceLimit(const replay::PriceLimitRow& row) noexcept override;
    Status appendDepth(const replay::DepthRow& row) noexcept override;
    Status appendTradeLine(const replay::TradeRow& row, const std::string& line) noexcept;
    Status appendLiquidationLine(const replay::LiquidationRow& row, const std::string& line) noexcept;
    Status appendBookTickerLine(const replay::BookTickerRow& row, const std::string& line) noexcept;
    Status appendMarkPriceLine(const replay::MarkPriceRow& row, const std::string& line) noexcept;
    Status appendIndexPriceLine(const replay::IndexPriceRow& row, const std::string& line) noexcept;
    Status appendFundingLine(const replay::FundingRow& row, const std::string& line) noexcept;
    Status appendPriceLimitLine(const replay::PriceLimitRow& row, const std::string& line) noexcept;
    Status appendDepthLine(const replay::DepthRow& row, const std::string& line) noexcept;
    Status appendDepthTapeSidecarLines(const replay::DepthRow& row,
                                       const std::string& tapeLine,
                                       const std::string& sidecarLine) noexcept;
    Status appendSnapshot(const replay::SnapshotDocument& snapshot,
                          std::uint64_t snapshotIndex) noexcept override;
    Status flush() noexcept override;
    Status close() noexcept override;

  private:
    Status ensureLineStream_(capture::ChannelKind channel, std::ofstream& stream) noexcept;
    Status writeLine_(capture::ChannelKind channel, std::ofstream& stream, const std::string& line) noexcept;

    std::filesystem::path sessionDir_{};
    std::ofstream trades_{};
    std::ofstream liquidations_{};
    std::ofstream bookTicker_{};
    std::ofstream markPrice_{};
    std::ofstream indexPrice_{};
    std::ofstream funding_{};
    std::ofstream priceLimit_{};
    std::ofstream depth_{};
    std::ofstream depthTape_{};
    std::ofstream depthSidecar_{};
    mutable std::mutex mutex_{};
    EventStoreStats stats_{};
};

}  // namespace hftrec::storage
