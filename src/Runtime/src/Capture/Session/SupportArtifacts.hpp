#pragma once

#include <cstdint>
#include <string>

#include "SessionManifest.hpp"

namespace hftrec::capture {

std::string renderSessionAuditJson(const SessionManifest& manifest, std::int64_t generatedAtNs);
std::string renderLoaderDiagnosticJson(const SessionManifest& manifest, std::int64_t generatedAtNs);
std::string renderMarketDataLaunchJson(const SessionManifest& manifest, std::int64_t generatedAtNs);

}  // namespace hftrec::capture
