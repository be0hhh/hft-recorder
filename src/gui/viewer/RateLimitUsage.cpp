#include "gui/viewer/RateLimitUsage.hpp"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string_view>
#include <vector>

#include "core/common/MiniJsonParser.hpp"

namespace hftrec::gui::viewer {
namespace {

constexpr std::int64_t kPct100E4 = 1000000ll;

struct RateLimitUsageStreamMeta {
    std::string path{"rate_limit_usage.jsonl"};
    std::int64_t rows{0};
    bool declared{false};
};

bool readText(const std::filesystem::path& path, std::string& out) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in.is_open()) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

std::string jsonString(const QJsonObject& object, const QString& key) {
    return object.value(key).toString().toStdString();
}

bool parseManifest(std::string_view text, RateLimitUsageStreamMeta& out) noexcept {
    out = RateLimitUsageStreamMeta{};
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(text.data(), static_cast<qsizetype>(text.size())));
    if (!doc.isObject()) return false;
    const QJsonObject streams = doc.object().value(QStringLiteral("streams")).toObject();
    const QJsonObject usage = streams.value(QStringLiteral("rate_limit_usage")).toObject();
    if (usage.isEmpty()) return true;
    out.declared = true;
    const std::string path = jsonString(usage, QStringLiteral("path"));
    if (!path.empty()) out.path = path;
    out.rows = usage.value(QStringLiteral("rows")).toInteger();
    return true;
}

bool parseUInt32(hftrec::json::MiniJsonParser& parser, std::uint32_t& out) noexcept {
    std::int64_t value = 0;
    if (!parser.parseInt64(value) || value < 0 || value > 0xffffffffll) return false;
    out = static_cast<std::uint32_t>(value);
    return true;
}

bool parseUInt64(hftrec::json::MiniJsonParser& parser, std::uint64_t& out) noexcept {
    std::int64_t value = 0;
    if (!parser.parseInt64(value) || value < 0) return false;
    out = static_cast<std::uint64_t>(value);
    return true;
}

bool parseByte(hftrec::json::MiniJsonParser& parser, std::uint8_t& out) noexcept {
    std::int64_t value = 0;
    if (!parser.parseInt64(value) || value < 0 || value > 255) return false;
    out = static_cast<std::uint8_t>(value);
    return true;
}

bool parseBoolInt(hftrec::json::MiniJsonParser& parser, bool& out) noexcept {
    std::int64_t value = 0;
    if (!parser.parseInt64(value) || value < 0 || value > 1) return false;
    out = value != 0;
    return true;
}

bool parseRateLimitUsageLine(std::string_view line, RateLimitUsagePoint& out) noexcept {
    out = RateLimitUsagePoint{};
    hftrec::json::MiniJsonParser parser{line};
    if (!parser.parseArrayStart()) return false;
    if (!parser.parseInt64(out.tsNs) || !parser.parseComma()) return false;
    if (!parseUInt32(parser, out.legIndex) || !parser.parseComma()) return false;
    if (!parser.parseInt64(out.minRemainingPctE4) || !parser.parseComma()) return false;
    if (!parser.parseInt64(out.remaining) || !parser.parseComma()) return false;
    if (!parser.parseInt64(out.limit) || !parser.parseComma()) return false;
    if (!parseByte(parser, out.bucketKind) || !parser.parseComma()) return false;
    if (!parseUInt64(parser, out.intervalNs) || !parser.parseComma()) return false;
    if (!parseBoolInt(parser, out.exhausted)) return false;
    return parser.parseArrayEnd() && parser.finish();
}

bool loadJsonl(const std::filesystem::path& path, RateLimitUsageData& out) {
    out = RateLimitUsageData{};
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in.is_open()) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        RateLimitUsagePoint row{};
        if (!parseRateLimitUsageLine(line, row)) return false;
        out.legCount = std::max(out.legCount, row.legIndex + 1u);
        out.points.push_back(row);
    }
    std::stable_sort(out.points.begin(), out.points.end(), [](const auto& lhs, const auto& rhs) noexcept {
        if (lhs.tsNs != rhs.tsNs) return lhs.tsNs < rhs.tsNs;
        return lhs.legIndex < rhs.legIndex;
    });
    return in.eof();
}

std::int64_t clampPctE4(std::int64_t value) noexcept {
    return std::clamp<std::int64_t>(value, 0, kPct100E4);
}

}  // namespace

bool loadRateLimitUsageFromResult(const std::filesystem::path& path,
                                  RateLimitUsageData& out,
                                  std::string& error) noexcept {
    out = RateLimitUsageData{};
    error.clear();
    std::string manifest;
    if (!readText(path / "manifest.json", manifest)) {
        error = "failed to read backtest manifest";
        return false;
    }
    RateLimitUsageStreamMeta meta{};
    if (!parseManifest(manifest, meta)) {
        error = "failed to parse backtest rate-limit manifest";
        return false;
    }
    if (!meta.declared || meta.rows == 0) return true;
    if (!loadJsonl(path / meta.path, out)) {
        error = "failed to parse rate-limit usage";
        return false;
    }
    return true;
}

std::int64_t rateLimitMinRemainingPctE4At(const RateLimitUsageData& usage,
                                          std::int64_t tsNs) noexcept {
    if (usage.points.empty()) return kPct100E4;
    const std::uint32_t lanes = std::max<std::uint32_t>(1u, usage.legCount);
    std::vector<std::int64_t> current(lanes, kPct100E4);
    for (const RateLimitUsagePoint& point : usage.points) {
        if (point.tsNs > tsNs) break;
        if (point.legIndex >= lanes) continue;
        current[point.legIndex] = clampPctE4(point.minRemainingPctE4);
    }
    return *std::min_element(current.begin(), current.end());
}

}  // namespace hftrec::gui::viewer
