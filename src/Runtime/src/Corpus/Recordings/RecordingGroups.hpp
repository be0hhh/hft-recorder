#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace hftrec::recordings {

// Cold discovery owns these bounded allocations. A refused snapshot has no rows.
inline constexpr std::size_t kRecordingMaxEntries = 65536;
inline constexpr std::size_t kRecordingMaxSessions = 8192;
inline constexpr std::size_t kRecordingMaxGroups = 4096;
inline constexpr std::size_t kRecordingMaxDepth = 8;
inline constexpr std::size_t kRecordingMaxSessionManifestBytes = 1024 * 1024;
inline constexpr std::size_t kRecordingMaxGroupManifestBytes = 2 * 1024 * 1024;
inline constexpr std::size_t kRecordingMaxAggregateInputBytes = 64 * 1024 * 1024;
inline constexpr std::size_t kRecordingMaxCacheBytes = 32 * 1024 * 1024;
inline constexpr std::size_t kRecordingMaxPathBytes = 4096;
inline constexpr std::size_t kRecordingMaxSymbols = 65536;
inline constexpr std::size_t kRecordingMaxSymbolBytes = 128;

enum class RecordingMetadataReadResult { Ready, Missing, Refused };
enum class RecordingMetadataReadMode { Document, FirstNonemptyLine };

// Validates every ancestor beneath an explicit existing root, opens one
// nonblocking no-follow descriptor, and checks its regular-file identity before
// and after reading. Output changes only on Ready. FirstNonemptyLine permits a
// larger file but reads at most maximumBytes, including preceding blank lines.
// consumedBytes, when supplied, reports actual bytes read even on refusal.
RecordingMetadataReadResult readRecordingMetadata(
    const std::filesystem::path& root, const std::filesystem::path& path,
    std::size_t maximumBytes, std::string& output, std::string* error = nullptr,
    RecordingMetadataReadMode mode = RecordingMetadataReadMode::Document,
    std::size_t* consumedBytes = nullptr) noexcept;

struct RecordedSessionInfo {
    std::filesystem::path path, manifestPath, groupPath;
    std::string sessionId, groupId, groupTitle, exchange, market;
    std::string localSymbol, folderSymbol, normalizedSymbol;
    std::string sessionHealth, warningSummary, displayTime, searchText;
    std::vector<std::string> symbols;
    std::int64_t startedAtNs{0}, endedAtNs{0};
    std::uint64_t tradesCount{0}, bookTickerCount{0}, candleCount{0}, totalRows{0};
    // Binary totalRows includes every typed record, including lifecycle rows.
    // Per-channel zero values are unavailable when channelCountsAvailable=false.
    std::uint32_t sourceCount{0};
    bool binaryCorpus{false}, channelCountsAvailable{true};
    bool grouped{false};
    // Finalized (endedAtNs > 0), not an exact replay/readiness assertion.
    bool complete{false};
};

struct RecordingGroupInfo {
    std::filesystem::path path;
    std::string id, title, localSymbol, folderSymbol, normalizedSymbol;
    std::string displayTime, searchText;
    std::int64_t startedAtNs{0}, endedAtNs{0};
    std::uint64_t totalRows{0};
    bool physical{false};
    std::vector<RecordedSessionInfo> sessions;
};

struct RecordingDiscoveryResult {
    std::vector<RecordedSessionInfo> sessions;
    std::vector<RecordingGroupInfo> groups;
    // Strict valid on-disk membership is a proper subset of current sessions.
    std::vector<std::filesystem::path> refreshRequired;
    std::vector<std::string> errors;
    bool complete{false};
};

struct RecordingGroupRefreshResult {
    RecordingDiscoveryResult discovery;
    std::vector<std::filesystem::path> planned, rebuilt;
    std::vector<std::string> errors;
    bool complete{false};
};

// Pure UTC conversion of nonnegative Unix nanoseconds: zero is 1970-01-01;
// invalid input refuses with an empty string and never reads the current clock.
std::string recordingGroupFolderName(std::int64_t startedAtNs,
                                    const std::string& folderSymbol) noexcept;
std::string recordingDisplayTimestamp(std::int64_t timestampNs) noexcept;
RecordingDiscoveryResult discoverRecordings(const std::filesystem::path& root) noexcept;
// Validates cached identities, bounds and internal physical memberships. A
// valid older cache may omit newly captured sessions; presentation metadata
// does not certify fresh manifest provenance. Removed cached paths refuse.
bool validateRecordingDiscovery(const std::filesystem::path& root,
                                const RecordingDiscoveryResult& discovery,
                                std::string* error = nullptr) noexcept;
bool writeGroupManifest(const std::filesystem::path& root,
                        const RecordingGroupInfo& group,
                        std::string* error = nullptr) noexcept;
bool loadGroupManifest(const std::filesystem::path& root,
                       const std::filesystem::path& groupPath,
                       RecordingGroupInfo& group,
                       std::string* error = nullptr) noexcept;
bool writeGroupManifestForPath(const std::filesystem::path& root,
                               const std::filesystem::path& path,
                               std::string* error = nullptr) noexcept;
RecordingGroupRefreshResult rebuildRecordingGroups(const std::filesystem::path& root,
                                                   bool apply = false) noexcept;

}  // namespace hftrec::recordings
