#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "core/common/Status.hpp"

namespace hftrec::finam {

struct FinamEnvSyncRequest {
    std::filesystem::path envPath{".env"};
    std::uint8_t apiSlot{1u};
    bool requireAccountId{false};
    bool mirrorStandardEnv{true};
};

struct FinamEnvValues {
    std::string jwt{};
    std::string accountId{};
};

struct FinamEnvSyncResult {
    bool attempted{false};
    bool refreshed{false};
    bool persisted{false};
    std::vector<std::filesystem::path> writtenPaths{};
    std::string accountId{};
    std::string error{};
};

bool isFinamExchangeName(std::string_view exchange) noexcept;

Status writeFinamEnvValues(const FinamEnvSyncRequest& request,
                           const FinamEnvValues& values,
                           FinamEnvSyncResult* result) noexcept;

Status refreshFinamEnvAndBearer(const FinamEnvSyncRequest& request,
                                FinamEnvSyncResult* result) noexcept;

Status persistCurrentFinamBearer(const FinamEnvSyncRequest& request,
                                 FinamEnvSyncResult* result) noexcept;

}  // namespace hftrec::finam
