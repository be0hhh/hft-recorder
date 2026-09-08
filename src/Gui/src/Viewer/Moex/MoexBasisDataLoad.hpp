#pragma once

#include <QString>

#include <vector>

#include "Corpus/Recordings/BasisChainSeries.hpp"
#include "Corpus/Recordings/RecordingDiscovery.hpp"
#include "../MoexBasisController.hpp"

namespace hftrec::gui::viewer::moex {

enum class LegLoadMode {
    MetadataOnly,
    FullCandles,
};

MoexBasisController::LegState loadLeg(const hftrec::recordings::RecordedSessionInfo& session,
                                      const QString& role,
                                      LegLoadMode mode);

bool applyBasisChainSeriesRows(const std::vector<hftrec::recordings::BasisChainSeriesRow>& rows,
                               MoexBasisController::LegState& spot,
                               std::vector<MoexBasisController::LegState>& futures);

}  // namespace hftrec::gui::viewer::moex
