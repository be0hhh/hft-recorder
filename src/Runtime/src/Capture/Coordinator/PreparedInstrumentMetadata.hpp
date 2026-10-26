#pragma once

#include "../../Corpus/InstrumentMetadata.hpp"

#include <cstdint>
#include <string>

namespace hftrec::capture {

// Cold owning input qualified by the producer directory reader. This is not
// a saved sidecar format or authority to discover native metadata.
struct PreparedInstrumentMetadata final {
    corpus::InstrumentMetadata metadata{};
    std::string exchange{}, market{}, canonicalSymbol{}, nativeSymbol{};
    std::uint64_t producerEpoch{0u}, directoryGeneration{0u};
    std::uint64_t sourceGeneration{0u};
    std::uint32_t sourceId{0u};
    bool qualified{false};
};

}  // namespace hftrec::capture
