#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hftrec {

enum class ChannelHealthState : std::uint8_t {
    NotCaptured = 0,
    Missing,
    Clean,
    Degraded,
    Corrupt,
};

enum class SessionHealth : std::uint8_t {
    Clean = 0,
    Degraded,
    Corrupt,
};

enum class IntegritySeverity : std::uint8_t {
    Info = 0,
    Warning,
    Error,
};

enum class IntegrityIncidentKind : std::uint8_t {
    MissingFile = 0,
    ParseError,
    CountMismatch,
    DepthSequenceGap,
    DepthNonMonotonic,
    SnapshotMissingForDepth,
    ExactnessUnprovable,
    CaptureAborted,
    UnknownIntegrity,
};

enum class IntegrityChannel : std::uint8_t {
    Trades = 0,
    Liquidations,
    BookTicker,
    Depth,
    Snapshot,
};

inline constexpr std::string_view toString(ChannelHealthState state) noexcept {
    switch (state) {
        case ChannelHealthState::NotCaptured: return "not_captured";
        case ChannelHealthState::Missing:     return "missing";
        case ChannelHealthState::Clean:       return "clean";
        case ChannelHealthState::Degraded:    return "degraded";
        case ChannelHealthState::Corrupt:     return "corrupt";
    }
    return "degraded";
}

inline constexpr std::string_view toString(SessionHealth state) noexcept {
    switch (state) {
        case SessionHealth::Clean:    return "clean";
        case SessionHealth::Degraded: return "degraded";
        case SessionHealth::Corrupt:  return "corrupt";
    }
    return "degraded";
}

inline constexpr std::string_view toString(IntegritySeverity severity) noexcept {
    switch (severity) {
        case IntegritySeverity::Info:    return "info";
        case IntegritySeverity::Warning: return "warning";
        case IntegritySeverity::Error:   return "error";
    }
    return "warning";
}

inline constexpr std::string_view toString(IntegrityIncidentKind kind) noexcept {
    switch (kind) {
        case IntegrityIncidentKind::MissingFile:             return "missing_file";
        case IntegrityIncidentKind::ParseError:              return "parse_error";
        case IntegrityIncidentKind::CountMismatch:           return "count_mismatch";
        case IntegrityIncidentKind::DepthSequenceGap:        return "depth_sequence_gap";
        case IntegrityIncidentKind::DepthNonMonotonic:       return "depth_non_monotonic";
        case IntegrityIncidentKind::SnapshotMissingForDepth: return "snapshot_missing_for_depth";
        case IntegrityIncidentKind::ExactnessUnprovable:     return "exactness_unprovable";
        case IntegrityIncidentKind::CaptureAborted:          return "capture_aborted";
        case IntegrityIncidentKind::UnknownIntegrity:        return "unknown_integrity";
    }
    return "unknown_integrity";
}

inline constexpr std::string_view toString(IntegrityChannel channel) noexcept {
    switch (channel) {
        case IntegrityChannel::Trades:       return "trades";
        case IntegrityChannel::Liquidations: return "liquidations";
        case IntegrityChannel::BookTicker: return "bookticker";
        case IntegrityChannel::Depth:      return "depth";
        case IntegrityChannel::Snapshot:   return "snapshot";
    }
    return "trades";
}

struct IntegrityIncident {
    IntegrityChannel      channel{IntegrityChannel::Trades};
    IntegrityIncidentKind kind{IntegrityIncidentKind::UnknownIntegrity};
    IntegritySeverity     severity{IntegritySeverity::Warning};
    std::string           reasonCode{};
    std::string           message{};
    std::int64_t          detectedAtTsNs{0};
    std::size_t           rowIndex{0};
    std::string           expectedValue{};
    std::string           observedValue{};
    bool                  exactnessLost{false};
};

struct ChannelIntegritySummary {
    ChannelHealthState state{ChannelHealthState::NotCaptured};
    bool               exactReplayEligible{false};
    std::size_t        incidentCount{0};
    std::size_t        gapCount{0};
    std::size_t        parseErrorCount{0};
    IntegritySeverity  highestSeverity{IntegritySeverity::Info};
    std::string        reasonCode{};
    std::string        reasonText{};
};

struct SessionIntegritySummary {
    SessionHealth                  sessionHealth{SessionHealth::Clean};
    bool                           exactReplayEligible{false};
    std::size_t                    totalIncidents{0};
    IntegritySeverity              highestSeverity{IntegritySeverity::Info};
    ChannelIntegritySummary        trades{};
    ChannelIntegritySummary        liquidations{};
    ChannelIntegritySummary        bookTicker{};
    ChannelIntegritySummary        depth{};
    ChannelIntegritySummary        snapshot{};
    std::vector<IntegrityIncident> incidents{};
};

}  // namespace hftrec
