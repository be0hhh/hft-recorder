#include "RecorderTuiInternal.hpp"

namespace hftrec::app::tui_detail {

bool manifestHasRows(const capture::SessionManifest& manifest) noexcept {
    return manifest.tradesCount != 0u
        || manifest.liquidationsCount != 0u
        || manifest.bookTickerCount != 0u
        || manifest.markPriceCount != 0u
        || manifest.indexPriceCount != 0u
        || manifest.fundingCount != 0u
        || manifest.priceLimitCount != 0u
        || manifest.depthCount != 0u
        || manifest.candlesCount != 0u
        || manifest.candles2Count != 0u;
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in) return {};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

bool looksLikeSessionDirName(std::string_view name) noexcept {
    std::size_t digits = 0;
    while (digits < name.size() && std::isdigit(static_cast<unsigned char>(name[digits]))) {
        ++digits;
    }
    return digits >= 10u && digits < name.size() && name[digits] == '_';
}

bool sessionDirHasJsonlRows(const std::filesystem::path& sessionDir) {
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(sessionDir, std::filesystem::directory_options::skip_permission_denied, ec), end;
         !ec && it != end;
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        if (it->path().extension() != ".jsonl") continue;
        if (std::filesystem::file_size(it->path(), ec) != 0u && !ec) return true;
        ec.clear();
    }
    return false;
}

bool deadZeroRowManifest(const capture::SessionManifest& manifest, std::int64_t nowNs) noexcept {
    if (manifest.startedAtNs <= 0) return false;
    if (manifestHasRows(manifest)) return false;
    const auto ttlNs = std::chrono::duration_cast<std::chrono::nanoseconds>(kDeadZeroRowSessionTtl).count();
    return nowNs - manifest.startedAtNs >= ttlNs;
}

bool removeOrphanSessionDirIfDead(const std::filesystem::path& sessionDir,
                                  std::filesystem::file_time_type now,
                                  DeadSessionSweepResult& result) {
    if (!looksLikeSessionDirName(sessionDir.filename().string())) return false;
    std::error_code ec;
    if (std::filesystem::exists(sessionDir / "manifest.json", ec) || ec) return false;

    const auto updatedAt = std::filesystem::last_write_time(sessionDir, ec);
    if (ec || now - updatedAt < kDeadZeroRowSessionTtl) return false;
    if (sessionDirHasJsonlRows(sessionDir)) return false;

    std::filesystem::remove_all(sessionDir, ec);
    if (ec) return false;
    ++result.removedSessions;
    return true;
}

bool removeSessionDirIfDeadZeroRows(const std::filesystem::path& sessionDir,
                                    std::int64_t nowNs,
                                    DeadSessionSweepResult& result) {
    const std::filesystem::path manifestPath = sessionDir / "manifest.json";
    const std::string document = readTextFile(manifestPath);
    if (document.empty()) return false;

    capture::SessionManifest manifest{};
    if (!isOk(capture::parseManifestJson(document, manifest))) return false;
    if (!deadZeroRowManifest(manifest, nowNs)) return false;

    std::error_code ec;
    std::filesystem::remove_all(sessionDir, ec);
    if (ec) return false;
    ++result.removedSessions;
    return true;
}

void removeGroupIfEmpty(const std::filesystem::path& root,
                        const std::filesystem::path& groupPath,
                        DeadSessionSweepResult& result) {
    std::error_code ec;
    const auto canonicalRoot = std::filesystem::weakly_canonical(root, ec);
    if (ec) return;
    const auto canonicalGroup = std::filesystem::weakly_canonical(groupPath, ec);
    if (ec || canonicalGroup == canonicalRoot) return;
    if (!std::filesystem::exists(canonicalGroup, ec) || ec) return;
    if (!std::filesystem::is_empty(canonicalGroup, ec) || ec) return;
    std::filesystem::remove(canonicalGroup, ec);
    if (!ec) ++result.removedGroups;
}

DeadSessionSweepResult sweepDeadZeroRowSessionsInGroup(const std::filesystem::path& root,
                                                       const std::filesystem::path& groupPath,
                                                       std::int64_t nowNs) {
    DeadSessionSweepResult result{};
    std::error_code ec;
    const auto fileNow = std::filesystem::file_time_type::clock::now();
    if (!std::filesystem::exists(groupPath, ec) || ec) return result;
    for (std::filesystem::directory_iterator it(groupPath, std::filesystem::directory_options::skip_permission_denied, ec), end;
         !ec && it != end;
         it.increment(ec)) {
        if (!it->is_directory(ec)) continue;
        if (removeOrphanSessionDirIfDead(it->path(), fileNow, result)) continue;
        (void)removeSessionDirIfDeadZeroRows(it->path(), nowNs, result);
    }
    removeGroupIfEmpty(root, groupPath, result);
    return result;
}

DeadSessionSweepResult sweepDeadZeroRowSessions(const std::filesystem::path& root, std::int64_t nowNs) {
    DeadSessionSweepResult result{};
    std::error_code ec;
    if (!std::filesystem::exists(root, ec) || ec) return result;
    std::vector<std::filesystem::path> sessionDirs;
    std::vector<std::filesystem::path> orphanDirs;
    for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end;
         !ec && it != end;
         it.increment(ec)) {
        if (it->is_directory(ec) && looksLikeSessionDirName(it->path().filename().string())) {
            orphanDirs.push_back(it->path());
            continue;
        }
        if (!it->is_regular_file(ec) || it->path().filename() != "manifest.json") continue;
        sessionDirs.push_back(it->path().parent_path());
    }
    const auto fileNow = std::filesystem::file_time_type::clock::now();
    for (const auto& orphanDir : orphanDirs) {
        const std::filesystem::path groupPath = orphanDir.parent_path();
        if (removeOrphanSessionDirIfDead(orphanDir, fileNow, result)) {
            removeGroupIfEmpty(root, groupPath, result);
        }
    }
    for (const auto& sessionDir : sessionDirs) {
        const std::filesystem::path groupPath = sessionDir.parent_path();
        if (removeSessionDirIfDeadZeroRows(sessionDir, nowNs, result)) {
            removeGroupIfEmpty(root, groupPath, result);
        }
    }
    return result;
}

DeadSessionSweepResult sweepRunOutputGroups(const RunOutputGroups& outputGroups) {
    DeadSessionSweepResult total{};
    const std::int64_t nowNs = wallNowNs();
    for (const auto& [_, groupPath] : outputGroups.bySymbol) {
        const DeadSessionSweepResult result =
            sweepDeadZeroRowSessionsInGroup(outputGroups.root, groupPath, nowNs);
        total.removedSessions += result.removedSessions;
        total.removedGroups += result.removedGroups;
    }
    return total;
}

std::string deadSessionSweepMessage(const DeadSessionSweepResult& result) {
    if (result.removedSessions == 0 && result.removedGroups == 0) return {};
    std::ostringstream out;
    out << "removed " << result.removedSessions << " dead zero-row session(s)";
    if (result.removedGroups != 0) out << ", " << result.removedGroups << " empty group(s)";
    return out.str();
}

bool deadZeroRowJobExpired(const RunningJob& job, Clock::time_point now) noexcept {
    if (!job.launched || job.finalized || job.startInProgress || !job.coordinator) return false;
    if (totalRows(*job.coordinator) != 0u) return false;
    return now - job.started >= kDeadZeroRowSessionTtl;
}

bool cullDeadZeroRowJob(RunningJob& job, Clock::time_point now) {
    if (!deadZeroRowJobExpired(job, now)) return false;
    job.error = "dead session: no rows for 5m";
    finalizeJob(job);
    job.status = "dead";
    job.running = false;
    job.finalized = true;
    return true;
}

}  // namespace hftrec::app::tui_detail
