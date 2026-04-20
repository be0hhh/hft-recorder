#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core/common/Status.hpp"

namespace hftrec::corpus {

struct InstrumentMetadata {
    std::string schemaVersion{"hftrec.instrument_metadata.v1"};
    std::string exchange{};
    std::string exchangeSource{"unknown"};
    std::string market{};
    std::string marketSource{"unknown"};
    std::string symbol{};
    std::string symbolSource{"unknown"};
    std::string instrumentType{};
    std::string instrumentTypeSource{"unknown"};
    std::optional<std::string> baseAsset{};
    std::string baseAssetSource{"unknown"};
    std::optional<std::string> quoteAsset{};
    std::string quoteAssetSource{"unknown"};
    std::optional<std::string> settlementAsset{};
    std::string settlementAssetSource{"unknown"};
    std::optional<std::int64_t> priceScaleDigits{};
    std::string priceScaleDigitsSource{"unknown"};
    std::optional<std::int64_t> qtyScaleDigits{};
    std::string qtyScaleDigitsSource{"unknown"};
    std::optional<std::int64_t> tickSizeE8{};
    std::string tickSizeSource{"unknown"};
    std::optional<std::int64_t> lotSizeE8{};
    std::string lotSizeSource{"unknown"};
    std::optional<std::string> instrumentStatus{};
    std::string instrumentStatusSource{"unknown"};
};

InstrumentMetadata makeInstrumentMetadata(std::string_view exchange,
                                          std::string_view market,
                                          std::string_view symbol) noexcept;

std::string renderInstrumentMetadataJson(const InstrumentMetadata& metadata);
Status parseInstrumentMetadataJson(std::string_view document, InstrumentMetadata& out) noexcept;

}  // namespace hftrec::corpus
