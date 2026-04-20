#pragma once

#include <cstdint>
#include <string>

#include "core/capture/SessionManifest.hpp"

namespace hftrec::capture {

std::string renderSessionAuditJson(const SessionManifest& manifest, std::int64_t generatedAtNs);
std::string renderIntegrityReportJson(const SessionManifest& manifest, std::int64_t generatedAtNs);
std::string renderLoaderDiagnosticsJson(const SessionManifest& manifest, std::int64_t generatedAtNs);

}  // namespace hftrec::capture
