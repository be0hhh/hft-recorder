#include "core/tui/RecorderTuiPreset.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

namespace hftrec::tui {

namespace {

std::string trim(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1u]))) --end;
    return std::string{text.substr(begin, end - begin)};
}

std::string lower(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char ch : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

bool parseInt64(std::string_view text, std::int64_t& out) noexcept {
    const auto* first = text.data();
    const auto* last = text.data() + text.size();
    std::int64_t value = 0;
    const auto result = std::from_chars(first, last, value);
    if (result.ec != std::errc{} || result.ptr != last) return false;
    out = value;
    return true;
}

bool parseInt(std::string_view text, int& out) noexcept {
    const auto* first = text.data();
    const auto* last = text.data() + text.size();
    int value = 0;
    const auto result = std::from_chars(first, last, value);
    if (result.ec != std::errc{} || result.ptr != last) return false;
    out = value;
    return true;
}

std::vector<std::string> splitCsv(std::string_view text) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t comma = text.find(',', pos);
        const std::size_t end = comma == std::string_view::npos ? text.size() : comma;
        out.push_back(trim(text.substr(pos, end - pos)));
        if (comma == std::string_view::npos) break;
        pos = comma + 1u;
    }
    return out;
}

bool assignChannel(std::string_view raw, ChannelSelection& channels) {
    const std::string name = lower(trim(raw));
    if (name == "trades" || name == "trade") {
        channels.trades = true;
        return true;
    }
    if (name == "liquidations" || name == "liquidation" || name == "forceorder" || name == "force_order") {
        channels.liquidations = true;
        return true;
    }
    if (name == "bookticker" || name == "book_ticker" || name == "book-ticker" || name == "bbo") {
        channels.bookTicker = true;
        return true;
    }
    if (name == "orderbook" || name == "order_book" || name == "order-book" || name == "depth") {
        channels.orderbook = true;
        return true;
    }
    if (name == "mark_price" || name == "mark-price" || name == "markprice" || name == "mark") {
        channels.markPrice = true;
        return true;
    }
    if (name == "index_price" || name == "index-price" || name == "indexprice" || name == "index") {
        channels.indexPrice = true;
        return true;
    }
    if (name == "funding" || name == "funding_rate" || name == "funding-rate") {
        channels.funding = true;
        return true;
    }
    if (name == "price_limit" || name == "price-limit" || name == "pricelimit" || name == "limit" || name == "limits") {
        channels.priceLimit = true;
        return true;
    }
    return false;
}

void appendLine(std::string& out, std::string_view key, std::string_view value) {
    out.append(key);
    out.push_back('=');
    out.append(value);
    out.push_back('\n');
}

bool validateJob(const RecorderTuiJob& job, std::string& error) {
    if (trim(job.name).empty()) {
        error = "job name is required";
        return false;
    }
    if (trim(job.exchange).empty()) {
        error = "job " + job.name + ": exchange is required";
        return false;
    }
    if (trim(job.market).empty()) {
        error = "job " + job.name + ": market is required";
        return false;
    }
    if (trim(job.symbol).empty()) {
        error = "job " + job.name + ": symbol is required";
        return false;
    }
    if (job.durationMin < 0) {
        error = "job " + job.name + ": duration_min must be >= 0";
        return false;
    }
    if (!anyChannelSelected(job.channels)) {
        error = "job " + job.name + ": at least one live channel is required";
        return false;
    }
    return true;
}

}  // namespace

ChannelSelection allLiveChannels() noexcept {
    return ChannelSelection{
        .trades = true,
        .liquidations = true,
        .bookTicker = true,
        .orderbook = true,
        .markPrice = true,
        .indexPrice = true,
        .funding = true,
        .priceLimit = true,
    };
}

bool anyChannelSelected(const ChannelSelection& channels) noexcept {
    return channels.trades || channels.liquidations || channels.bookTicker || channels.orderbook ||
           channels.markPrice || channels.indexPrice || channels.funding || channels.priceLimit;
}

bool parseDurationMinutes(std::string_view text, std::int64_t& out, std::string& error) {
    error.clear();
    std::string value = lower(trim(text));
    if (value.empty() || value == "none" || value == "indefinite" || value == "forever") {
        out = 0;
        return true;
    }
    if (!value.empty() && value.back() == 'm') value.pop_back();
    value = trim(value);
    std::int64_t parsed = 0;
    if (!parseInt64(value, parsed) || parsed < 0) {
        error = "duration_min must be a non-negative minute count or none";
        return false;
    }
    out = parsed;
    return true;
}

bool parseChannelSelection(std::string_view text, ChannelSelection& out, std::string& error) {
    error.clear();
    const std::string value = lower(trim(text));
    if (value.empty() || value == "all") {
        out = allLiveChannels();
        return true;
    }

    ChannelSelection channels{};
    for (const std::string& token : splitCsv(value)) {
        if (token.empty()) continue;
        if (!assignChannel(token, channels)) {
            error = "unknown live channel: " + token;
            return false;
        }
    }
    if (!anyChannelSelected(channels)) {
        error = "at least one live channel is required";
        return false;
    }
    out = channels;
    return true;
}

std::string renderChannelSelection(const ChannelSelection& channels) {
    std::vector<std::string_view> names;
    if (channels.trades) names.push_back("trades");
    if (channels.liquidations) names.push_back("liquidations");
    if (channels.bookTicker) names.push_back("bookticker");
    if (channels.orderbook) names.push_back("orderbook");
    if (channels.markPrice) names.push_back("mark_price");
    if (channels.indexPrice) names.push_back("index_price");
    if (channels.funding) names.push_back("funding");
    if (channels.priceLimit) names.push_back("price_limit");

    std::string out;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i != 0u) out.push_back(',');
        out.append(names[i]);
    }
    return out;
}

bool parsePresetText(std::string_view text, RecorderTuiPreset& out, std::string& error) {
    error.clear();
    RecorderTuiPreset preset{};
    RecorderTuiJob* currentJob = nullptr;

    std::istringstream input{std::string{text}};
    std::string line;
    int lineNo = 0;
    while (std::getline(input, line)) {
        ++lineNo;
        const std::size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        line = trim(line);
        if (line.empty()) continue;

        if (line.front() == '[' && line.back() == ']') {
            const std::string section = trim(std::string_view{line}.substr(1u, line.size() - 2u));
            constexpr std::string_view kJobPrefix = "job ";
            if (!section.starts_with(kJobPrefix)) {
                error = "line " + std::to_string(lineNo) + ": unknown section [" + section + "]";
                return false;
            }
            RecorderTuiJob job{};
            job.name = trim(std::string_view{section}.substr(kJobPrefix.size()));
            job.channels = allLiveChannels();
            preset.jobs.push_back(std::move(job));
            currentJob = &preset.jobs.back();
            continue;
        }

        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) {
            error = "line " + std::to_string(lineNo) + ": expected key=value";
            return false;
        }
        const std::string key = lower(trim(std::string_view{line}.substr(0, eq)));
        const std::string value = trim(std::string_view{line}.substr(eq + 1u));

        if (currentJob == nullptr) {
            if (key == "output_dir") {
                if (value.empty()) {
                    error = "line " + std::to_string(lineNo) + ": output_dir is empty";
                    return false;
                }
                preset.outputDir = value;
            } else if (key == "progress_sec") {
                int progressSec = 0;
                if (!parseInt(value, progressSec) || progressSec < 1 || progressSec > 3600) {
                    error = "line " + std::to_string(lineNo) + ": progress_sec must be in [1,3600]";
                    return false;
                }
                preset.progressSec = progressSec;
            } else {
                error = "line " + std::to_string(lineNo) + ": unknown global key " + key;
                return false;
            }
            continue;
        }

        if (key == "exchange") currentJob->exchange = value;
        else if (key == "market") currentJob->market = value;
        else if (key == "symbol") currentJob->symbol = value;
        else if (key == "duration_min") {
            if (!parseDurationMinutes(value, currentJob->durationMin, error)) {
                error = "line " + std::to_string(lineNo) + ": " + error;
                return false;
            }
        } else if (key == "channels") {
            if (!parseChannelSelection(value, currentJob->channels, error)) {
                error = "line " + std::to_string(lineNo) + ": " + error;
                return false;
            }
        } else {
            error = "line " + std::to_string(lineNo) + ": unknown job key " + key;
            return false;
        }
    }

    if (preset.progressSec < 1) preset.progressSec = 10;
    if (preset.outputDir.empty()) preset.outputDir = "./recordings";
    for (const RecorderTuiJob& job : preset.jobs) {
        if (!validateJob(job, error)) return false;
    }
    out = std::move(preset);
    return true;
}

std::string renderPresetText(const RecorderTuiPreset& preset) {
    std::string out;
    appendLine(out, "output_dir", preset.outputDir.string());
    appendLine(out, "progress_sec", std::to_string(preset.progressSec));
    for (const RecorderTuiJob& job : preset.jobs) {
        out.push_back('\n');
        out.append("[job ");
        out.append(job.name);
        out.append("]\n");
        appendLine(out, "exchange", job.exchange);
        appendLine(out, "market", job.market);
        appendLine(out, "symbol", job.symbol);
        appendLine(out, "duration_min", std::to_string(job.durationMin));
        appendLine(out, "channels", renderChannelSelection(job.channels));
    }
    return out;
}

bool loadPresetFile(const std::filesystem::path& path, RecorderTuiPreset& out, std::string& error) {
    std::ifstream file(path);
    if (!file) {
        error = "failed to open preset: " + path.string();
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return parsePresetText(buffer.str(), out, error);
}

bool savePresetFile(const std::filesystem::path& path, const RecorderTuiPreset& preset, std::string& error) {
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "failed to create preset directory: " + path.parent_path().string();
        return false;
    }
    std::ofstream file(path);
    if (!file) {
        error = "failed to write preset: " + path.string();
        return false;
    }
    file << renderPresetText(preset);
    if (!file) {
        error = "failed to flush preset: " + path.string();
        return false;
    }
    error.clear();
    return true;
}

std::filesystem::path presetConfigDir() {
    return std::filesystem::path{"configs"};
}

std::filesystem::path resolvePresetPath(std::string_view text) {
    const std::string value = trim(text);
    if (value.empty()) return defaultPresetPath();

    const bool explicitPath = value.front() == '.'
        || value.find('/') != std::string::npos
        || value.find('\\') != std::string::npos;
    std::filesystem::path path{value};
    if (path.is_absolute() || explicitPath) return path;
    if (!path.has_extension()) path += ".ini";
    return presetConfigDir() / path;
}

std::filesystem::path defaultPresetPath() {
    return presetConfigDir() / "default.ini";
}

}  // namespace hftrec::tui
