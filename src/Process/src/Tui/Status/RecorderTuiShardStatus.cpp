#include "RecorderTuiShardStatus.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string_view>

namespace hftrec::tui {

namespace {

std::filesystem::path tmpPathFor(const std::filesystem::path& path) {
    return std::filesystem::path{path.string() + ".tmp"};
}

std::map<std::string, std::string> readKeyValueFile(const std::filesystem::path& path) {
    std::map<std::string, std::string> out;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        out.emplace(line.substr(0, eq), line.substr(eq + 1u));
    }
    return out;
}

int intValue(const std::map<std::string, std::string>& values, std::string_view key, int fallback = 0) {
    const auto it = values.find(std::string{key});
    if (it == values.end()) return fallback;
    errno = 0;
    char* end = nullptr;
    const long parsed = std::strtol(it->second.c_str(), &end, 10);
    if (errno != 0 || end == it->second.c_str()) return fallback;
    return static_cast<int>(parsed);
}

std::int64_t i64Value(const std::map<std::string, std::string>& values, std::string_view key) {
    const auto it = values.find(std::string{key});
    if (it == values.end()) return 0;
    errno = 0;
    char* end = nullptr;
    const long long parsed = std::strtoll(it->second.c_str(), &end, 10);
    if (errno != 0 || end == it->second.c_str()) return 0;
    return static_cast<std::int64_t>(parsed);
}

std::uint64_t u64Value(const std::map<std::string, std::string>& values, std::string_view key) {
    const auto it = values.find(std::string{key});
    if (it == values.end()) return 0;
    errno = 0;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(it->second.c_str(), &end, 10);
    if (errno != 0 || end == it->second.c_str()) return 0;
    return static_cast<std::uint64_t>(parsed);
}

bool stateEquals(const RecorderTuiShardStatus& status, std::string_view state) noexcept {
    return status.state == state;
}

bool cleanFinalState(const RecorderTuiShardStatus& status) noexcept {
    return stateEquals(status, "done")
        || stateEquals(status, "done_warn")
        || stateEquals(status, "stopped")
        || stateEquals(status, "stopped_starting")
        || stateEquals(status, "skipped")
        || stateEquals(status, "error")
        || stateEquals(status, "dead")
        || stateEquals(status, "failed_empty")
        || stateEquals(status, "preflight_failed")
        || stateEquals(status, "exited")
        || stateEquals(status, "exited_incomplete")
        || stateEquals(status, "spawn_error");
}

bool staleActiveState(const RecorderTuiShardStatus& status) noexcept {
    return status.state.empty()
        || stateEquals(status, "starting")
        || stateEquals(status, "running")
        || stateEquals(status, "spawned")
        || stateEquals(status, "pending");
}

bool preserveMessageOnCleanExit(const RecorderTuiShardStatus& status) noexcept {
    return status.errors != 0
        || stateEquals(status, "error")
        || stateEquals(status, "dead")
        || stateEquals(status, "failed_empty")
        || stateEquals(status, "preflight_failed")
        || stateEquals(status, "spawn_error");
}

void markStoppedAfterCleanExit(RecorderTuiShardStatus& status) {
    if (!preserveMessageOnCleanExit(status)) status.message = "exit=0";
    status.state = "stopped";
    status.running = 0;
    status.starting = 0;
    status.pending = 0;
}

std::string statusLineValue(std::string_view value) {
    std::string out{value};
    for (char& ch : out) {
        if (ch == '\n' || ch == '\r') ch = ' ';
    }
    return out;
}

}  // namespace

RecorderTuiShardStatus readShardStatusFile(const std::filesystem::path& path) {
    RecorderTuiShardStatus status{};

    std::error_code ec;
    status.statusFilePresent = std::filesystem::exists(path, ec) && !ec;
    const std::filesystem::path tempPath = tmpPathFor(path);
    ec.clear();
    status.statusTmpPresent = std::filesystem::exists(tempPath, ec) && !ec;
    if (status.statusTmpPresent) {
        ec.clear();
        status.statusTmpNonEmpty = std::filesystem::file_size(tempPath, ec) > 0u && !ec;
    }
    if (!status.statusFilePresent) return status;

    const auto values = readKeyValueFile(path);
    if (values.empty()) {
        status.state = "empty_status";
        status.message = "status file is empty or malformed";
        status.errors = 1;
        return status;
    }

    if (const auto it = values.find("state"); it != values.end()) status.state = it->second;
    if (const auto it = values.find("message"); it != values.end()) status.message = it->second;
    if (const auto it = values.find("first_error"); it != values.end()) status.firstError = it->second;
    if (const auto it = values.find("last_error"); it != values.end()) status.lastError = it->second;
    status.jobs = intValue(values, "jobs");
    status.running = intValue(values, "running");
    status.starting = intValue(values, "starting");
    status.stalled = intValue(values, "stalled");
    status.pending = intValue(values, "pending");
    status.finalized = intValue(values, "finalized");
    status.errors = intValue(values, "errors");
    status.skipped = intValue(values, "skipped");
    status.pid = intValue(values, "pid", -1);
    status.exitCode = intValue(values, "exit_code", -1);
    status.signal = intValue(values, "signal", -1);
    status.rows = u64Value(values, "rows");
    status.updatedWallNs = i64Value(values, "updated_wall_ns");
    return status;
}

bool writeShardStatusFile(const std::filesystem::path& path, const RecorderTuiShardStatus& status) {
    if (path.empty()) return false;

    std::error_code ec;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return false;

    const std::filesystem::path tempPath = tmpPathFor(path);
    {
        std::ofstream out(tempPath, std::ios::out | std::ios::trunc);
        if (!out.is_open()) return false;
        out << "state=" << status.state << '\n';
        out << "message=" << statusLineValue(status.message) << '\n';
        out << "jobs=" << status.jobs << '\n';
        out << "running=" << status.running << '\n';
        out << "starting=" << status.starting << '\n';
        out << "stalled=" << status.stalled << '\n';
        out << "pending=" << status.pending << '\n';
        out << "finalized=" << status.finalized << '\n';
        out << "errors=" << status.errors << '\n';
        out << "skipped=" << status.skipped << '\n';
        out << "rows=" << status.rows << '\n';
        out << "pid=" << status.pid << '\n';
        out << "exit_code=" << status.exitCode << '\n';
        out << "signal=" << status.signal << '\n';
        out << "updated_wall_ns=" << status.updatedWallNs << '\n';
        out << "first_error=" << statusLineValue(status.firstError) << '\n';
        out << "last_error=" << statusLineValue(status.lastError) << '\n';
        out.flush();
        if (!out.good()) return false;
    }

    std::filesystem::rename(tempPath, path, ec);
    if (!ec) return true;

    ec.clear();
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(tempPath, path, ec);
    return !ec;
}

void applyShardChildExit(RecorderTuiShardStatus& status, RecorderTuiShardExit childExit) {
    switch (childExit.kind) {
        case RecorderTuiShardExit::Kind::Signaled:
            status.signal = childExit.code;
            status.exitCode = -1;
            status.message = "signal=" + std::to_string(childExit.code);
            status.errors = std::max(status.errors, 1);
            status.state = "exited";
            return;
        case RecorderTuiShardExit::Kind::Exited:
            status.exitCode = childExit.code;
            status.signal = -1;
            if (childExit.code != 0) {
                status.message = "exit=" + std::to_string(childExit.code);
                status.errors = std::max(status.errors, 1);
                status.state = "exited";
                return;
            }
            if (!preserveMessageOnCleanExit(status)) status.message = "exit=0";
            if (stateEquals(status, "stopping")) {
                markStoppedAfterCleanExit(status);
                return;
            }
            if (cleanFinalState(status)) return;
            if (staleActiveState(status)) {
                if (status.jobs > 0 && status.finalized >= status.jobs && status.errors == 0) {
                    status.state = "done";
                    return;
                }
                status.message = "exit=0 before final status";
                status.errors = std::max(status.errors, 1);
                status.state = "exited_incomplete";
            }
            return;
        case RecorderTuiShardExit::Kind::Unknown:
            status.message = "exit=unknown";
            status.errors = std::max(status.errors, 1);
            status.state = "exited";
            return;
    }
}

bool shardStatusIsFinal(const RecorderTuiShardStatus& status) noexcept {
    return cleanFinalState(status);
}

bool shardStatusIsIssue(const RecorderTuiShardStatus& status) noexcept {
    if (!status.statusFilePresent || status.statusTmpPresent) return true;
    if (status.errors != 0 || status.stalled != 0) return true;
    if (status.signal >= 0 || status.exitCode > 0) return true;
    if (status.state == "spawn_error"
        || status.state == "error"
        || status.state == "dead"
        || status.state == "failed_empty"
        || status.state == "preflight_failed"
        || status.state == "empty_status"
        || status.state == "exited"
        || status.state == "exited_incomplete"
        || status.state == "kill_sent_not_reaped") {
        return true;
    }
    if (shardStatusIsFinal(status) && status.rows == 0u) return true;
    return false;
}

}  // namespace hftrec::tui
