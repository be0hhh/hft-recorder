#include "core/common/Log.hpp"

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/async.h>

namespace hftrec::log {

namespace {

std::once_flag g_init_flag;
std::unordered_map<std::string, std::shared_ptr<spdlog::logger>> g_loggers;
std::shared_ptr<spdlog::logger> g_fallback;

spdlog::level::level_enum parseLevel(std::string_view s) noexcept {
    if (s == "trace")    return spdlog::level::trace;
    if (s == "debug")    return spdlog::level::debug;
    if (s == "info")     return spdlog::level::info;
    if (s == "warn")     return spdlog::level::warn;
    if (s == "error")    return spdlog::level::err;
    if (s == "critical") return spdlog::level::critical;
    return spdlog::level::info;
}

constexpr const char* kCategories[] = {
    "main",
    "control",
    "producer.trades",
    "producer.bookticker",
    "producer.depth",
    "producer.snapshot",
    "writer.trades",
    "writer.bookticker",
    "writer.depth",
    "writer.snapshot",
    "codec",
    "bench",
};

}  // namespace

void init(std::string_view logDir, std::string_view level) {
    std::call_once(g_init_flag, [&]() {
        const auto lvl = parseLevel(level);
        spdlog::init_thread_pool(8192, 1);

        auto stdoutSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        stdoutSink->set_level(spdlog::level::info);

        std::string pattern = "[%Y-%m-%dT%H:%M:%S.%e%z] [%n] [%^%l%$] %v";

        std::shared_ptr<spdlog::sinks::rotating_file_sink_mt> fileSink;
        if (!logDir.empty()) {
            std::string path(logDir);
            path += "/hft-recorder.log";
            fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                path, 100u * 1024u * 1024u, 7);
            fileSink->set_level(spdlog::level::trace);
        }

        for (const auto* name : kCategories) {
            std::vector<spdlog::sink_ptr> sinks{stdoutSink};
            if (fileSink) sinks.push_back(fileSink);
            auto logger = std::make_shared<spdlog::async_logger>(
                name, sinks.begin(), sinks.end(),
                spdlog::thread_pool(), spdlog::async_overflow_policy::block);
            logger->set_level(lvl);
            logger->set_pattern(pattern);
            g_loggers.emplace(name, logger);
        }
        g_fallback = g_loggers["main"];
    });
}

std::shared_ptr<spdlog::logger> get(std::string_view category) {
    auto it = g_loggers.find(std::string{category});
    if (it != g_loggers.end()) return it->second;
    return g_fallback;
}

void shutdown() {
    for (auto& [_, logger] : g_loggers) {
        logger->flush();
    }
    spdlog::shutdown();
}

}  // namespace hftrec::log
