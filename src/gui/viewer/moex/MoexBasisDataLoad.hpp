#pragma once

#include <QString>

#include <vector>

#include "core/recordings/BasisChainSeries.hpp"
#include "core/recordings/RecordingDiscovery.hpp"
#include "gui/viewer/MoexBasisController.hpp"

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
