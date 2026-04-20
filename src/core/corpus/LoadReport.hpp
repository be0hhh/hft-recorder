#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "core/common/Status.hpp"

namespace hftrec::corpus {

enum class LoadIssueSeverity : std::uint8_t {
    Warning = 0,
    Degraded = 1,
    Fatal = 2,
};

enum class LoadIssueCode : std::uint16_t {
    None = 0,
    MissingSessionDirectory,
    MissingManifest,
    UnreadableArtifact,
    InvalidManifest,
    UnsupportedSchemaVersion,
    MissingRequiredArtifact,
    InvalidJsonLine,
    InvalidJsonDocument,
    StaleSeekIndex,
    InvalidSeekIndex,
    DepthGapDetected,
};

enum class ChannelLoadState : std::uint8_t {
    Clean = 0,
    Warning = 1,
    Degraded = 2,
    Corrupt = 3,
    Missing = 4,
    NotCaptured = 5,
};

enum class SessionLoadState : std::uint8_t {
    Clean = 0,
    Degraded = 1,
    Corrupt = 2,
};

struct SeekIndexBucket {
    std::int64_t tsNs{0};
    std::uint64_t eventIndexStart{0};
    std::uint64_t depthRowIndex{0};
};

struct LoadIssue {
    LoadIssueCode code{LoadIssueCode::None};
    LoadIssueSeverity severity{LoadIssueSeverity::Warning};
    Status statusHint{Status::Unknown};
    std::string channel{};
    std::string artifact{};
    std::size_t lineOrRow{0};
    std::string detail{};
};

struct LoadReport {
    Status finalStatus{Status::Ok};
    SessionLoadState sessionState{SessionLoadState::Clean};
    ChannelLoadState manifestState{ChannelLoadState::Missing};
    ChannelLoadState tradesState{ChannelLoadState::NotCaptured};
    ChannelLoadState bookTickerState{ChannelLoadState::NotCaptured};
    ChannelLoadState depthState{ChannelLoadState::NotCaptured};
    ChannelLoadState snapshotState{ChannelLoadState::NotCaptured};
    ChannelLoadState seekIndexState{ChannelLoadState::Missing};
    bool manifestPresent{false};
    bool usedSeekIndex{false};
    bool staleSeekIndex{false};
    std::int64_t corpusSchemaVersion{1};
    std::vector<LoadIssue> issues{};
    std::vector<SeekIndexBucket> seekBuckets{};
};

inline const char* toString(LoadIssueSeverity severity) noexcept {
    switch (severity) {
        case LoadIssueSeverity::Warning: return "Warning";
        case LoadIssueSeverity::Degraded: return "Degraded";
        case LoadIssueSeverity::Fatal: return "Fatal";
    }
    return "Warning";
}

inline const char* toString(LoadIssueCode code) noexcept {
    switch (code) {
        case LoadIssueCode::None: return "None";
        case LoadIssueCode::MissingSessionDirectory: return "MissingSessionDirectory";
        case LoadIssueCode::MissingManifest: return "MissingManifest";
        case LoadIssueCode::UnreadableArtifact: return "UnreadableArtifact";
        case LoadIssueCode::InvalidManifest: return "InvalidManifest";
        case LoadIssueCode::UnsupportedSchemaVersion: return "UnsupportedSchemaVersion";
        case LoadIssueCode::MissingRequiredArtifact: return "MissingRequiredArtifact";
        case LoadIssueCode::InvalidJsonLine: return "InvalidJsonLine";
        case LoadIssueCode::InvalidJsonDocument: return "InvalidJsonDocument";
        case LoadIssueCode::StaleSeekIndex: return "StaleSeekIndex";
        case LoadIssueCode::InvalidSeekIndex: return "InvalidSeekIndex";
        case LoadIssueCode::DepthGapDetected: return "DepthGapDetected";
    }
    return "None";
}

inline void recomputeLoadReport(LoadReport& report) noexcept {
    report.finalStatus = Status::Ok;
    report.sessionState = SessionLoadState::Clean;
    for (const auto& issue : report.issues) {
        if (issue.severity == LoadIssueSeverity::Fatal) {
            report.sessionState = SessionLoadState::Corrupt;
            if (report.finalStatus == Status::Ok || report.finalStatus == Status::Unknown) {
                report.finalStatus = issue.statusHint == Status::Unknown ? Status::CorruptData : issue.statusHint;
            }
            continue;
        }
        if (issue.severity == LoadIssueSeverity::Degraded && report.sessionState == SessionLoadState::Clean) {
            report.sessionState = SessionLoadState::Degraded;
        }
    }
    if (report.finalStatus == Status::Unknown) {
        report.finalStatus = Status::Ok;
    }
}

inline void appendLoadIssue(LoadReport& report, LoadIssue issue) {
    report.issues.push_back(std::move(issue));
    recomputeLoadReport(report);
}

inline void escalateChannelState(ChannelLoadState& target, ChannelLoadState next) noexcept {
    if (static_cast<int>(next) > static_cast<int>(target)) {
        target = next;
    }
}

}  // namespace hftrec::corpus
