#include "gui/viewer/StrategyIndicator.hpp"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string_view>

#include "core/common/MiniJsonParser.hpp"

namespace hftrec::gui::viewer {
namespace {

struct IndicatorStreamMeta {
    std::string path{"strategy_indicator.jsonl"};
    std::string profile{};
    std::string title{};
    std::string valueLabel{};
    std::string auxLabel{};
    std::string unit{};
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

bool parseManifest(std::string_view text, IndicatorStreamMeta& out) noexcept {
    out = IndicatorStreamMeta{};
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(text.data(), static_cast<qsizetype>(text.size())));
    if (!doc.isObject()) return false;
    const QJsonObject streams = doc.object().value(QStringLiteral("streams")).toObject();
    const QJsonObject indicator = streams.value(QStringLiteral("strategy_indicator")).toObject();
    if (indicator.isEmpty()) return true;
    out.declared = true;
    const std::string path = jsonString(indicator, QStringLiteral("path"));
    if (!path.empty()) out.path = path;
    out.profile = jsonString(indicator, QStringLiteral("profile"));
    out.title = jsonString(indicator, QStringLiteral("title"));
    out.valueLabel = jsonString(indicator, QStringLiteral("value_label"));
    out.auxLabel = jsonString(indicator, QStringLiteral("aux_label"));
    out.unit = jsonString(indicator, QStringLiteral("unit"));
    out.rows = indicator.value(QStringLiteral("rows")).toInteger();
    return true;
}

bool parseByte(hftrec::json::MiniJsonParser& parser, std::uint8_t& out) noexcept {
    std::int64_t value = 0;
    if (!parser.parseInt64(value) || value < 0 || value > 255) return false;
    out = static_cast<std::uint8_t>(value);
    return true;
}

bool parseIndicatorLine(std::string_view line, StrategyIndicatorPoint& out) noexcept {
    out = StrategyIndicatorPoint{};
    hftrec::json::MiniJsonParser parser{line};
    if (!parser.parseArrayStart()) return false;
    if (!parser.parseInt64(out.tsNs) || !parser.parseComma()) return false;
    if (!parser.parseInt64(out.valueRaw) || !parser.parseComma()) return false;
    if (!parser.parseInt64(out.auxRaw) || !parser.parseComma()) return false;
    if (!parseByte(parser, out.eventCode) || !parser.parseComma()) return false;
    if (!parseByte(parser, out.reasonRaw) || !parser.parseComma()) return false;
    if (!parseByte(parser, out.decisionKind) || !parser.parseComma()) return false;
    if (!parseByte(parser, out.sideRaw)) return false;
    while (!parser.peek(']')) {
        std::int64_t ignored = 0;
        if (!parser.parseComma() || !parser.parseInt64(ignored)) return false;
    }
    return parser.parseArrayEnd() && parser.finish();
}

bool loadJsonl(const std::filesystem::path& path, std::vector<StrategyIndicatorPoint>& out) {
    out.clear();
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in.is_open()) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        StrategyIndicatorPoint row{};
        if (!parseIndicatorLine(line, row)) return false;
        out.push_back(row);
    }
    std::stable_sort(out.begin(), out.end(), [](const auto& lhs, const auto& rhs) noexcept {
        return lhs.tsNs < rhs.tsNs;
    });
    return in.eof();
}

}  // namespace

bool loadStrategyIndicatorFromResult(const std::filesystem::path& path,
                                     StrategyIndicatorData& out,
                                     std::string& error) noexcept {
    out = StrategyIndicatorData{};
    error.clear();
    std::string manifest;
    if (!readText(path / "manifest.json", manifest)) {
        error = "failed to read backtest manifest";
        return false;
    }
    IndicatorStreamMeta meta{};
    if (!parseManifest(manifest, meta)) {
        error = "failed to parse backtest indicator manifest";
        return false;
    }
    if (!meta.declared || meta.rows == 0) return true;
    if (!loadJsonl(path / meta.path, out.points)) {
        error = "failed to parse strategy indicator";
        return false;
    }
    out.profile = QString::fromStdString(meta.profile);
    out.title = QString::fromStdString(meta.title);
    out.valueLabel = QString::fromStdString(meta.valueLabel);
    out.auxLabel = QString::fromStdString(meta.auxLabel);
    out.unit = QString::fromStdString(meta.unit);
    return true;
}

}  // namespace hftrec::gui::viewer
