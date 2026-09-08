#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace hftrec::tui {

struct RecorderTuiShardExit {
    enum class Kind : std::uint8_t {
        Unknown,
        Exited,
        Signaled
    };

    Kind kind{Kind::Unknown};
    int code{-1};

    static RecorderTuiShardExit exited(int exitCode) noexcept {
        return RecorderTuiShardExit{.kind = Kind::Exited, .code = exitCode};
    }

    static RecorderTuiShardExit signaled(int signalNumber) noexcept {
        return RecorderTuiShardExit{.kind = Kind::Signaled, .code = signalNumber};
    }
};

struct RecorderTuiShardStatus {
    std::string state{"starting"};
    std::string message{};
    int jobs{0};
    int running{0};
    int starting{0};
    int stalled{0};
    int pending{0};
    int finalized{0};
    int errors{0};
    int skipped{0};
    int pid{-1};
    int exitCode{-1};
    int signal{-1};
    std::uint64_t rows{0};
    std::int64_t updatedWallNs{0};
    std::string firstError{};
    std::string lastError{};
    bool statusFilePresent{false};
    bool statusTmpPresent{false};
    bool statusTmpNonEmpty{false};
};

RecorderTuiShardStatus readShardStatusFile(const std::filesystem::path& path);
bool writeShardStatusFile(const std::filesystem::path& path, const RecorderTuiShardStatus& status);
void applyShardChildExit(RecorderTuiShardStatus& status, RecorderTuiShardExit childExit);
bool shardStatusIsFinal(const RecorderTuiShardStatus& status) noexcept;
bool shardStatusIsIssue(const RecorderTuiShardStatus& status) noexcept;

}  // namespace hftrec::tui
