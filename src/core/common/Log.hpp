#pragma once

#include <memory>
#include <string_view>

#include <spdlog/spdlog.h>

// spdlog wrapper — all hftrec code logs through this header, never via direct
// `#include <spdlog/...>` in business code (keeps the logging backend swappable).
// See doc/LOGGING_AND_METRICS.md.

namespace hftrec::log {

// Initialise the global logger set (stdout + rotating file sink).
// Safe to call multiple times — subsequent calls are no-ops.
// `logDir` is where per-day files are written. Empty → current working dir.
void init(std::string_view logDir, std::string_view level);

// Fetch a category logger by name. Falls back to "main" if the name is unknown.
std::shared_ptr<spdlog::logger> get(std::string_view category);

// Flush all sinks (call before shutdown).
void shutdown();

}  // namespace hftrec::log
