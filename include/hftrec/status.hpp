#pragma once

#include <string_view>

namespace hftrec {

enum class Status : int {
    Ok = 0,
    InvalidArgument,
    IoError,
    OutOfRange,
    CorruptData,
    Unimplemented,
    Cancelled,
    Unknown,
};

inline constexpr std::string_view statusToString(Status status) noexcept {
    switch (status) {
        case Status::Ok:              return "Ok";
        case Status::InvalidArgument: return "InvalidArgument";
        case Status::IoError:         return "IoError";
        case Status::OutOfRange:      return "OutOfRange";
        case Status::CorruptData:     return "CorruptData";
        case Status::Unimplemented:   return "Unimplemented";
        case Status::Cancelled:       return "Cancelled";
        case Status::Unknown:         return "Unknown";
    }
    return "Unknown";
}

inline constexpr bool isOk(Status status) noexcept { return status == Status::Ok; }

}  // namespace hftrec