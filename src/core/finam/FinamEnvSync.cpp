#include "core/finam/FinamEnvSync.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <chrono>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>

#if HFTREC_WITH_CXET
#include "api/auth/BearerTokenRegistry.hpp"
#endif

namespace hftrec::finam {
namespace {

constexpr const char* kFinamPrefix = "FINAM_API";

std::uint8_t normalizedSlot(std::uint8_t slot) noexcept {
    return slot == 0u ? 1u : slot;
}

bool textEqualsAscii(std::string_view lhs, std::string_view rhs) noexcept {
    if (lhs.size() != rhs.size()) return false;
    for (std::size_t i = 0u; i < lhs.size(); ++i) {
        char a = lhs[i];
        char b = rhs[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + ('a' - 'A'));
        if (a != b) return false;
    }
    return true;
}

std::string envKey(std::uint8_t slot, std::string_view suffix) {
    return std::string{kFinamPrefix} + "_" + std::to_string(normalizedSlot(slot)) + "_" + std::string{suffix};
}

bool lineDefinesKey(const std::string& line, const std::string& key) noexcept {
    std::size_t pos = 0u;
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
    if (pos >= line.size() || line[pos] == '#') return false;
    if (line.compare(pos, key.size(), key) != 0) return false;
    pos += key.size();
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
    return pos < line.size() && line[pos] == '=';
}

std::filesystem::path absoluteNormalized(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::path absolute = std::filesystem::absolute(path.empty() ? std::filesystem::path{"."} : path, ec);
    if (ec) absolute = path.empty() ? std::filesystem::path{"."} : path;
    return absolute.lexically_normal();
}

bool samePathLexical(const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
    return absoluteNormalized(lhs).generic_string() == absoluteNormalized(rhs).generic_string();
}

bool standardEnvLayoutExists(const std::filesystem::path& root) {
    std::error_code ec;
    return std::filesystem::exists(root / "apps" / "hft-recorder", ec) &&
           std::filesystem::exists(root / "apps" / "hft-trader", ec);
}

std::optional<std::filesystem::path> findRepoRootFrom(std::filesystem::path start) {
    if (start.empty()) return std::nullopt;
    std::error_code ec;
    start = std::filesystem::absolute(start, ec).lexically_normal();
    if (ec) return std::nullopt;
    if (!std::filesystem::is_directory(start, ec)) start = start.parent_path();
    while (!start.empty()) {
        if (standardEnvLayoutExists(start)) return start;
        const auto parent = start.parent_path();
        if (parent == start) break;
        start = parent;
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> findRepoRoot(const std::filesystem::path& envPath) {
    if (auto root = findRepoRootFrom(envPath); root.has_value()) return root;
    std::error_code ec;
    const auto cwd = std::filesystem::current_path(ec);
    if (!ec) return findRepoRootFrom(cwd);
    return std::nullopt;
}

bool envPathIsDefaultDotEnv(const std::filesystem::path& path) {
    return path.empty() || path.generic_string() == ".env";
}

std::vector<std::filesystem::path> targetEnvPaths(const FinamEnvSyncRequest& request) {
    std::vector<std::filesystem::path> targets;
    const auto root = findRepoRoot(request.envPath);
    const std::filesystem::path selected = envPathIsDefaultDotEnv(request.envPath)
        ? std::filesystem::path{".env"}
        : request.envPath;
    std::filesystem::path primary = absoluteNormalized(selected);

    if (envPathIsDefaultDotEnv(request.envPath)) {
        std::error_code ec;
        if (!std::filesystem::exists(primary, ec) && root.has_value()) {
            primary = root.value() / "apps" / "hft-recorder" / ".env";
        }
    }

    targets.push_back(primary);
    if (!request.mirrorStandardEnv || !root.has_value()) return targets;

    const auto recorderEnv = root.value() / "apps" / "hft-recorder" / ".env";
    const auto traderEnv = root.value() / "apps" / "hft-trader" / ".env";
    const bool selectedRecorder = samePathLexical(primary, recorderEnv);
    const bool selectedTrader = samePathLexical(primary, traderEnv);
    if (!selectedRecorder && !selectedTrader) return targets;

    const auto mirror = selectedRecorder ? traderEnv : recorderEnv;
    if (std::none_of(targets.begin(), targets.end(), [&](const auto& path) { return samePathLexical(path, mirror); })) {
        targets.push_back(mirror);
    }
    return targets;
}

Status replaceFile(const std::filesystem::path& path, const std::string& text) noexcept {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return Status::IoError;

    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path tempPath = path.string() + ".tmp." + std::to_string(now);
    {
        std::ofstream out(tempPath, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return Status::IoError;
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out.good()) {
            out.close();
            std::filesystem::remove(tempPath, ec);
            return Status::IoError;
        }
    }

    ec.clear();
    std::filesystem::rename(tempPath, path, ec);
    if (!ec) return Status::Ok;

    ec.clear();
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(tempPath, path, ec);
    if (!ec) return Status::Ok;

    std::filesystem::remove(tempPath, ec);
    return Status::IoError;
}

Status updateEnvFile(const std::filesystem::path& path,
                     const std::vector<std::pair<std::string, std::string>>& values) noexcept {
    std::vector<std::string> lines;
    {
        std::ifstream in(path, std::ios::in | std::ios::binary);
        if (in.is_open()) {
            std::string line;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                lines.push_back(std::move(line));
            }
        }
    }

    for (const auto& [key, value] : values) {
        bool replaced = false;
        for (std::string& line : lines) {
            if (!lineDefinesKey(line, key)) continue;
            line = key + "=" + value;
            replaced = true;
            break;
        }
        if (!replaced) lines.push_back(key + "=" + value);
    }

    std::string out;
    for (const std::string& line : lines) {
        out += line;
        out += '\n';
    }
    return replaceFile(path, out);
}

void setProcessEnvValue(const std::string& key, const std::string& value) noexcept {
    if (!key.empty() && !value.empty()) (void)::setenv(key.c_str(), value.c_str(), 1);
}

std::string envValue(const std::string& key) {
    const char* value = std::getenv(key.c_str());
    return value && value[0] != '\0' ? std::string{value} : std::string{};
}

#if HFTREC_WITH_CXET
bool resolveCurrentToken(std::uint8_t slot, std::string& out) noexcept {
    std::array<char, 512> token{};
    if (!cxet::api::resolveBearerToken(kFinamPrefix, normalizedSlot(slot), token.data(), token.size())) return false;
    out = token.data();
    return !out.empty();
}

bool refreshTokenViaProvider(std::uint8_t slot, std::string& out) noexcept {
    std::array<char, 512> token{};
    if (!cxet::api::resolveBearerTokenViaProvider(kFinamPrefix, normalizedSlot(slot), token.data(), token.size())) {
        return false;
    }
    out = token.data();
    return !out.empty();
}

std::string resolveAccountId(std::uint8_t slot) {
    std::array<char, 128> account{};
    if (cxet::api::resolveBearerAccountId(kFinamPrefix, normalizedSlot(slot), account.data(), account.size())) {
        return account.data();
    }
    return envValue(envKey(slot, "ACCOUNT_ID"));
}
#else
bool resolveCurrentToken(std::uint8_t, std::string&) noexcept {
    return false;
}

bool refreshTokenViaProvider(std::uint8_t, std::string&) noexcept {
    return false;
}

std::string resolveAccountId(std::uint8_t slot) {
    return envValue(envKey(slot, "ACCOUNT_ID"));
}
#endif

void resetResult(FinamEnvSyncResult* result) {
    if (result) *result = FinamEnvSyncResult{};
}

Status fail(FinamEnvSyncResult* result, Status status, std::string error) {
    if (result) result->error = std::move(error);
    return status;
}

}  // namespace

bool isFinamExchangeName(std::string_view exchange) noexcept {
    return textEqualsAscii(exchange, "finam");
}

Status writeFinamEnvValues(const FinamEnvSyncRequest& request,
                           const FinamEnvValues& values,
                           FinamEnvSyncResult* result) noexcept {
    resetResult(result);
    if (values.jwt.empty()) {
        return fail(result, Status::InvalidArgument, "missing Finam JWT for FINAM_API env sync");
    }
    if (request.requireAccountId && values.accountId.empty()) {
        return fail(result,
                    Status::InvalidArgument,
                    "missing FINAM_API_" + std::to_string(normalizedSlot(request.apiSlot)) +
                        "_ACCOUNT_ID for Finam futures metadata");
    }

    std::vector<std::pair<std::string, std::string>> valuesToWrite;
    valuesToWrite.emplace_back(envKey(request.apiSlot, "JWT"), values.jwt);
    if (!values.accountId.empty()) valuesToWrite.emplace_back(envKey(request.apiSlot, "ACCOUNT_ID"), values.accountId);

    const auto targets = targetEnvPaths(request);
    if (targets.empty()) {
        return fail(result, Status::InvalidArgument, "no Finam env target path resolved");
    }

    for (const auto& path : targets) {
        const auto status = updateEnvFile(path, valuesToWrite);
        if (!isOk(status)) {
            return fail(result, status, "failed to update Finam env file: " + path.string());
        }
        if (result) result->writtenPaths.push_back(path);
    }

    setProcessEnvValue(envKey(request.apiSlot, "JWT"), values.jwt);
    if (!values.accountId.empty()) setProcessEnvValue(envKey(request.apiSlot, "ACCOUNT_ID"), values.accountId);

    if (result) {
        result->persisted = true;
        result->accountId = values.accountId;
    }
    return Status::Ok;
}

Status refreshFinamEnvAndBearer(const FinamEnvSyncRequest& request,
                                FinamEnvSyncResult* result) noexcept {
    resetResult(result);
    if (result) result->attempted = true;

    std::string token;
    bool refreshed = refreshTokenViaProvider(request.apiSlot, token);
    if (!refreshed && !resolveCurrentToken(request.apiSlot, token)) {
        return fail(result,
                    Status::Unknown,
                    "Finam bearer refresh failed for FINAM_API_" +
                        std::to_string(normalizedSlot(request.apiSlot)) +
                        "; check SECRET/JWT, proxy, and api.finam.ru session access");
    }

    FinamEnvValues values{};
    values.jwt = std::move(token);
    values.accountId = resolveAccountId(request.apiSlot);
    const auto status = writeFinamEnvValues(request, values, result);
    if (result) {
        result->attempted = true;
        result->refreshed = refreshed;
    }
    return status;
}

Status persistCurrentFinamBearer(const FinamEnvSyncRequest& request,
                                 FinamEnvSyncResult* result) noexcept {
    resetResult(result);
    if (result) result->attempted = true;

    std::string token;
    if (!resolveCurrentToken(request.apiSlot, token)) {
        return fail(result,
                    Status::Unknown,
                    "missing current Finam bearer token for FINAM_API_" +
                        std::to_string(normalizedSlot(request.apiSlot)));
    }

    FinamEnvValues values{};
    values.jwt = std::move(token);
    values.accountId = resolveAccountId(request.apiSlot);
    const auto status = writeFinamEnvValues(request, values, result);
    if (result) result->attempted = true;
    return status;
}

}  // namespace hftrec::finam
