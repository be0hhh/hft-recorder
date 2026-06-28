#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <openssl/sha.h>

#if !defined(_WIN32)
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

#include "core/capture/JsonSerializers.hpp"
#include "core/capture/SessionManifest.hpp"
#include "core/capture/SupportArtifacts.hpp"
#include "core/common/Integrity.hpp"
#include "core/common/JsonString.hpp"
#include "core/corpus/InstrumentMetadata.hpp"
#include "core/history/BinanceVisionFormat.hpp"
#include "core/history/ZipReader.hpp"
#include "core/recordings/RecordingRoot.hpp"
#include "hftrec/status.hpp"
#include "hftrec/version.hpp"

namespace fs = std::filesystem;

namespace hftrec::app {
namespace {

struct Options {
    std::string symbol;
    std::optional<int> days;
    std::optional<history::Date> from;
    std::optional<history::Date> to;
    fs::path outRoot{recordings::defaultRecordingsRoot()};
    bool interactive{false};
    bool help{false};
    bool version{false};
    std::string search;
};

struct ImportStats {
    std::uint64_t trades{0};
    std::uint64_t bookTickers{0};
    std::int64_t firstTsNs{0};
    std::int64_t lastTsNs{0};
};

std::string upperAscii(std::string_view text) {
    std::string out{text};
    for (char& ch : out) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return out;
}

std::string lowerHex(const unsigned char* bytes, std::size_t size) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < size; ++i) out << std::setw(2) << static_cast<unsigned>(bytes[i]);
    return out.str();
}

void updateRange(ImportStats& stats, std::int64_t tsNs) noexcept {
    if (tsNs <= 0) return;
    if (stats.firstTsNs == 0 || tsNs < stats.firstTsNs) stats.firstTsNs = tsNs;
    if (tsNs > stats.lastTsNs) stats.lastTsNs = tsNs;
}

bool readBytes(const fs::path& path, std::vector<std::uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;
    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    if (size < 0) return false;
    in.seekg(0, std::ios::beg);
    out.resize(static_cast<std::size_t>(size));
    if (!out.empty()) in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return in.good() || in.eof();
}

bool readText(const fs::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return in.good() || in.eof();
}

bool writeText(const fs::path& path, std::string_view text) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) return false;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return out.good();
}

fs::path tempFilePath(std::string_view stem) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
#if defined(_WIN32)
    const int pid = 0;
#else
    const int pid = static_cast<int>(::getpid());
#endif
    return fs::temp_directory_path() / (std::string{"hftrec_"} + std::string{stem} + "_" + std::to_string(pid) + "_" + std::to_string(now));
}

bool runCurl(const std::string& url, const fs::path& output, bool progress, std::string& error) {
#if defined(_WIN32)
    (void)url;
    (void)output;
    (void)progress;
    error = "history importer requires POSIX /usr/bin/curl in v1";
    return false;
#else
    std::error_code ec;
    fs::create_directories(output.parent_path(), ec);
    if (ec) {
        error = "failed to create download directory: " + output.parent_path().string();
        return false;
    }
    std::vector<std::string> args{"/usr/bin/curl", "-fL", "--retry", "3", "--connect-timeout", "15", "--max-time", "900"};
    if (!progress) {
        args.push_back("-sS");
    }
    args.push_back("-o");
    args.push_back(output.string());
    args.push_back(url);

    std::vector<char*> argv;
    argv.reserve(args.size() + 1u);
    for (std::string& arg : args) argv.push_back(arg.data());
    argv.push_back(nullptr);

    pid_t pid = 0;
    const int spawnRc = posix_spawnp(&pid, "/usr/bin/curl", nullptr, nullptr, argv.data(), environ);
    if (spawnRc != 0) {
        error = "failed to spawn curl: errno=" + std::to_string(spawnRc);
        return false;
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        error = "failed to wait for curl";
        return false;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        error = "curl failed for " + url;
        return false;
    }
    return true;
#endif
}

bool downloadText(const std::string& url, std::string& text, std::string& error) {
    const fs::path path = tempFilePath("download.txt");
    if (!runCurl(url, path, false, error)) {
        fs::remove(path);
        return false;
    }
    const bool ok = readText(path, text);
    fs::remove(path);
    if (!ok) error = "failed to read downloaded text";
    return ok;
}

bool sha256File(const fs::path& path, std::string& outHex) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;
    SHA256_CTX ctx{};
    if (SHA256_Init(&ctx) != 1) return false;
    std::array<char, 64 * 1024> buf{};
    while (in.good()) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize got = in.gcount();
        if (got > 0 && SHA256_Update(&ctx, buf.data(), static_cast<std::size_t>(got)) != 1) return false;
    }
    unsigned char digest[SHA256_DIGEST_LENGTH]{};
    if (SHA256_Final(digest, &ctx) != 1) return false;
    outHex = lowerHex(digest, sizeof(digest));
    return true;
}

bool downloadVerifiedZip(const std::string& key, const fs::path& downloadDir, fs::path& zipPath, std::string& error) {
    const std::string checksumUrl = history::visionHttpsUrl(key + ".CHECKSUM");
    const fs::path checksumPath = downloadDir / (fs::path{key}.filename().string() + ".CHECKSUM");
    if (!runCurl(checksumUrl, checksumPath, false, error)) return false;

    std::string checksumText;
    if (!readText(checksumPath, checksumText)) {
        error = "failed to read checksum file: " + checksumPath.string();
        return false;
    }
    std::string expected;
    if (!history::parseChecksumSha256(checksumText, expected)) {
        error = "failed to parse checksum file: " + checksumPath.string();
        return false;
    }

    zipPath = downloadDir / fs::path{key}.filename();
    std::printf("download %s\n", key.c_str());
    if (!runCurl(history::visionHttpsUrl(key), zipPath, true, error)) return false;

    std::string observed;
    if (!sha256File(zipPath, observed)) {
        error = "failed to compute sha256 for " + zipPath.string();
        return false;
    }
    if (observed != expected) {
        error = "checksum mismatch for " + key;
        return false;
    }
    return true;
}

bool firstLineIsHeader(std::string_view line, std::string_view firstColumn) noexcept {
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (line == firstColumn) return true;
    return line.size() > firstColumn.size() &&
           line.substr(0, firstColumn.size()) == firstColumn &&
           line[firstColumn.size()] == ',';
}

const std::vector<std::string>& tradeAliases() {
    static const std::vector<std::string> aliases{
        "price",
        "amount",
        "side",
        "timestamp",
        "id",
        "firstTradeId",
        "lastTradeId",
        "quoteQty",
        "isBuyerMaker",
        "symbol",
        "exchange",
        "market",
        "captureSeq",
        "ingestSeq",
    };
    return aliases;
}

bool appendAggTradesCsv(const std::string& csv,
                        const history::ImportIdentity& identity,
                        std::ofstream& out,
                        ImportStats& stats,
                        std::string& error) {
    std::istringstream lines(csv);
    std::string line;
    bool first = true;
    std::uint64_t lineNo = 0;
    while (std::getline(lines, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (first && firstLineIsHeader(line, "agg_trade_id")) {
            first = false;
            continue;
        }
        first = false;
        replay::TradeRow row{};
        if (!history::parseAggTradeCsvLine(line, identity, static_cast<std::int64_t>(stats.trades + 1u), row, error)) {
            error = "aggTrades CSV line " + std::to_string(lineNo) + ": " + error + ": " +
                    line.substr(0, std::min<std::size_t>(line.size(), 240u));
            return false;
        }
        out << capture::renderTradeJsonLine(row, tradeAliases()) << '\n';
        if (!out.good()) {
            error = "failed to write trades.jsonl";
            return false;
        }
        ++stats.trades;
        updateRange(stats, row.tsNs);
    }
    return true;
}

bool appendBookTickerCsv(const std::string& csv,
                         const history::ImportIdentity& identity,
                         std::ofstream& out,
                         ImportStats& stats,
                         std::string& error) {
    std::istringstream lines(csv);
    std::string line;
    bool first = true;
    std::uint64_t lineNo = 0;
    while (std::getline(lines, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (first && firstLineIsHeader(line, "update_id")) {
            first = false;
            continue;
        }
        first = false;
        replay::BookTickerRow row{};
        if (!history::parseBookTickerCsvLine(line, identity, static_cast<std::int64_t>(stats.bookTickers + 1u), row, error)) {
            error = "bookTicker CSV line " + std::to_string(lineNo) + ": " + error + ": " +
                    line.substr(0, std::min<std::size_t>(line.size(), 240u));
            return false;
        }
        out << capture::renderBookTickerJsonLine(row) << '\n';
        if (!out.good()) {
            error = "failed to write bookticker.jsonl";
            return false;
        }
        ++stats.bookTickers;
        updateRange(stats, row.tsNs);
    }
    return true;
}

bool importOneZip(const std::string& key,
                  const fs::path& downloadDir,
                  const history::ImportIdentity& identity,
                  bool trades,
                  std::ofstream& out,
                  ImportStats& stats,
                  std::string& error) {
    fs::path zipPath;
    if (!downloadVerifiedZip(key, downloadDir, zipPath, error)) return false;
    std::vector<std::uint8_t> zip;
    if (!readBytes(zipPath, zip)) {
        error = "failed to read zip file: " + zipPath.string();
        return false;
    }
    std::string memberName;
    std::string csv;
    if (!history::extractSingleFileZip(zip, memberName, csv, error)) return false;
    return trades ? appendAggTradesCsv(csv, identity, out, stats, error)
                  : appendBookTickerCsv(csv, identity, out, stats, error);
}

bool remoteVisionKeyAvailable(const std::string& key, std::string& error) {
    std::string xml;
    if (!downloadText(history::visionListUrl(key, false), xml, error)) {
        error = "failed to list Binance Vision key before download: " + key + ": " + error;
        return false;
    }
    return history::hasVisionZipAndChecksum(xml, key);
}

bool preflightRequiredVisionFiles(const history::ImportIdentity& identity,
                                  const std::vector<history::Date>& dates,
                                  std::vector<std::string>& missing,
                                  std::string& error) {
    missing.clear();
    error.clear();
    for (const auto date : dates) {
        const std::string tradeKey = history::visionDailyKey(history::VisionChannel::AggTrades, identity.symbol, date);
        const std::string bookKey = history::visionDailyKey(history::VisionChannel::BookTicker, identity.symbol, date);
        if (!remoteVisionKeyAvailable(tradeKey, error)) {
            if (!error.empty()) return false;
            missing.push_back(tradeKey + " (+ .CHECKSUM)");
        }
        if (!remoteVisionKeyAvailable(bookKey, error)) {
            if (!error.empty()) return false;
            missing.push_back(bookKey + " (+ .CHECKSUM)");
        }
    }
    return true;
}

std::string renderIntegrityReport(const capture::SessionManifest& manifest) {
    auto channel = [](std::string_view state,
                      bool exact,
                      std::string_view code,
                      std::string_view text,
                      bool comma) {
        std::ostringstream out;
        out << "{"
            << "\"state\":" << json::quote(state)
            << ",\"exact_replay_eligible\":" << (exact ? "true" : "false")
            << ",\"incident_count\":0"
            << ",\"gap_count\":0"
            << ",\"parse_error_count\":0"
            << ",\"highest_severity\":\"info\""
            << ",\"reason_code\":" << json::quote(code)
            << ",\"reason_text\":" << json::quote(text)
            << "}" << (comma ? "," : "");
        return out.str();
    };

    std::ostringstream out;
    out << "{\n";
    out << "  \"session_health\": \"clean\",\n";
    out << "  \"exact_replay_eligible\": false,\n";
    out << "  \"total_incidents\": 0,\n";
    out << "  \"highest_severity\": \"info\",\n";
    out << "  \"channels\": {\n";
    out << "    \"trades\": " << channel("clean", false, "ok", "Binance Vision aggTrades rows imported", true) << "\n";
    out << "    \"liquidations\": " << channel("not_captured", false, "not_captured", "liquidations disabled", true) << "\n";
    out << "    \"bookticker\": " << channel("clean", false, "ok", "Binance Vision bookTicker rows imported", true) << "\n";
    out << "    \"depth\": " << channel("not_captured", false, "not_captured", "depth disabled for history import", true) << "\n";
    out << "    \"snapshot\": " << channel("not_captured", false, "not_captured", "snapshot disabled for history import", false) << "\n";
    out << "  },\n";
    out << "  \"incidents\": [],\n";
    out << "  \"producer\": \"history\",\n";
    out << "  \"session_id\": " << json::quote(manifest.sessionId) << "\n";
    out << "}\n";
    return out.str();
}

std::string sessionName(std::string_view symbol, const std::vector<history::Date>& dates) {
    const std::string from = dates.empty() ? "unknown" : history::formatDate(dates.front());
    const std::string to = dates.empty() ? "unknown" : history::formatDate(dates.back());
    auto compact = [](std::string text) {
        text.erase(std::remove(text.begin(), text.end(), '-'), text.end());
        return text;
    };
    return compact(from) + "_" + compact(to) + "_binance_futures_um_" + std::string{symbol} + "_vision";
}

bool writeSessionArtifacts(const fs::path& sessionDir,
                           const fs::path& outRoot,
                           const std::string& sessionId,
                           const history::ImportIdentity& identity,
                           const std::vector<history::Date>& dates,
                           const ImportStats& stats,
                           std::string& error) {
    capture::SessionManifest manifest{};
    manifest.sessionId = sessionId;
    manifest.exchange = identity.exchange;
    manifest.market = identity.market;
    manifest.symbols = {identity.symbol};
    manifest.selectedParentDir = outRoot.string();
    manifest.startedAtNs = stats.firstTsNs;
    manifest.endedAtNs = stats.lastTsNs;
    manifest.targetDurationSec = 0;
    manifest.actualDurationSec = stats.firstTsNs > 0 && stats.lastTsNs >= stats.firstTsNs
                                     ? (stats.lastTsNs - stats.firstTsNs) / 1000000000LL
                                     : 0;
    manifest.snapshotIntervalSec = 0;
    manifest.tradesEnabled = true;
    manifest.bookTickerEnabled = true;
    manifest.orderbookEnabled = false;
    manifest.tradesRequiredWhenEnabled = true;
    manifest.bookTickerRequiredWhenEnabled = true;
    manifest.orderbookRequiredWhenEnabled = false;
    manifest.tradesCount = stats.trades;
    manifest.bookTickerCount = stats.bookTickers;
    manifest.tradesHistoryRows = stats.trades;
    manifest.tradesHistoryRequests = static_cast<std::uint64_t>(dates.size());
    manifest.tradesHistoryRequestedStartNs = stats.firstTsNs;
    manifest.tradesHistoryRequestedEndNs = stats.lastTsNs;
    manifest.tradesHistoryFeedKind = "agg_trade";
    manifest.tradesHistoryStatus = "imported_binance_vision_futures_um";
    manifest.tradesRowSchema = "cxet_trade_strict_v1";
    manifest.bookTickerRowSchema = "cxet_bookticker_strict_v1";
    manifest.canonicalArtifacts = {
        "manifest.json",
        "instrument_metadata.json",
        "jsonl/trades.jsonl",
        "jsonl/bookticker.jsonl",
    };
    manifest.supportArtifacts = {
        "reports/session_audit.json",
        "reports/loader_diagnostics.json",
        "reports/market_data_launch.json",
        "reports/integrity_report.json",
    };
    manifest.warningSummary = "offline import from Binance Vision USD-M futures; trades feed_kind=agg_trade; no depth/orderbook exact replay";
    manifest.sessionHealth = SessionHealth::Clean;
    manifest.exactReplayEligible = false;

    const std::int64_t generatedAt = stats.lastTsNs > 0 ? stats.lastTsNs : stats.firstTsNs;
    auto metadata = corpus::makeInstrumentMetadata(identity.exchange, identity.market, identity.symbol);
    metadata.metadataSource = "binance_vision_futures_um";
    metadata.metadataWarning = "offline import: aggTrades are aggregated; bookTicker is observational L1 BBO";

    if (!writeText(sessionDir / "manifest.json", capture::renderManifestJson(manifest)) ||
        !writeText(sessionDir / "instrument_metadata.json", corpus::renderInstrumentMetadataJson(metadata)) ||
        !writeText(sessionDir / "reports" / "session_audit.json", capture::renderSessionAuditJson(manifest, generatedAt)) ||
        !writeText(sessionDir / "reports" / "loader_diagnostics.json", capture::renderLoaderDiagnosticsJson(manifest, generatedAt)) ||
        !writeText(sessionDir / "reports" / "market_data_launch.json", capture::renderMarketDataLaunchJson(manifest, generatedAt)) ||
        !writeText(sessionDir / "reports" / "integrity_report.json", renderIntegrityReport(manifest))) {
        error = "failed to write session metadata artifacts";
        return false;
    }
    return true;
}

bool importHistory(const Options& options, const std::vector<history::Date>& dates, std::string& error) {
    if (options.symbol.empty()) {
        error = "missing --symbol";
        return false;
    }
    if (dates.empty()) {
        error = "empty date range";
        return false;
    }

    const history::ImportIdentity identity{.symbol = upperAscii(options.symbol)};
    const std::string id = sessionName(identity.symbol, dates);
    const fs::path finalDir = options.outRoot / id;
    const fs::path tmpDir = options.outRoot / ("." + id + ".tmp");
    const fs::path downloadDir = tmpDir / "downloads";

    std::vector<std::string> missing;
    if (!preflightRequiredVisionFiles(identity, dates, missing, error)) return false;
    if (!missing.empty()) {
        std::ostringstream out;
        out << "Binance Vision is missing required files for exact import before download:";
        for (const auto& key : missing) out << "\n  " << key;
        out << "\nCannot create a recorder session with required aggTrades + bookTicker for "
            << identity.symbol << ".";
        error = out.str();
        return false;
    }

    std::error_code ec;
    if (fs::exists(finalDir, ec)) {
        error = "session already exists: " + finalDir.string();
        return false;
    }
    fs::remove_all(tmpDir, ec);
    fs::create_directories(tmpDir / "jsonl", ec);
    if (ec) {
        error = "failed to create temp session directory: " + tmpDir.string();
        return false;
    }

    ImportStats stats{};
    std::ofstream tradesOut(tmpDir / "jsonl" / "trades.jsonl", std::ios::binary | std::ios::trunc);
    std::ofstream bookOut(tmpDir / "jsonl" / "bookticker.jsonl", std::ios::binary | std::ios::trunc);
    if (!tradesOut.is_open() || !bookOut.is_open()) {
        error = "failed to open output JSONL files";
        fs::remove_all(tmpDir, ec);
        return false;
    }

    for (const auto date : dates) {
        const std::string tradeKey = history::visionDailyKey(history::VisionChannel::AggTrades, identity.symbol, date);
        const std::string bookKey = history::visionDailyKey(history::VisionChannel::BookTicker, identity.symbol, date);
        if (!importOneZip(tradeKey, downloadDir, identity, true, tradesOut, stats, error) ||
            !importOneZip(bookKey, downloadDir, identity, false, bookOut, stats, error)) {
            fs::remove_all(tmpDir, ec);
            return false;
        }
    }
    tradesOut.flush();
    bookOut.flush();
    if (!tradesOut.good() || !bookOut.good()) {
        error = "failed to flush JSONL output";
        fs::remove_all(tmpDir, ec);
        return false;
    }
    tradesOut.close();
    bookOut.close();

    if (stats.trades == 0 || stats.bookTickers == 0) {
        error = "import produced empty required channel";
        fs::remove_all(tmpDir, ec);
        return false;
    }
    if (!writeSessionArtifacts(tmpDir, options.outRoot, id, identity, dates, stats, error)) {
        fs::remove_all(tmpDir, ec);
        return false;
    }
    fs::remove_all(downloadDir, ec);
    fs::rename(tmpDir, finalDir, ec);
    if (ec) {
        error = "failed to publish final session: " + ec.message();
        fs::remove_all(tmpDir, ec);
        return false;
    }

    std::printf("session=%s\ntrades=%llu bookticker=%llu\n",
                finalDir.string().c_str(),
                static_cast<unsigned long long>(stats.trades),
                static_cast<unsigned long long>(stats.bookTickers));
    return true;
}

std::vector<std::string> searchSymbols(std::string_view query, std::string& error) {
    auto listSymbols = [&](history::VisionChannel channel) -> std::vector<std::string> {
        std::string xml;
        if (!downloadText(history::visionListUrl(history::visionSymbolPrefix(channel), true), xml, error)) return {};
        const std::string base = history::visionSymbolPrefix(channel);
        const std::string needle = upperAscii(query);
        std::vector<std::string> symbols;
        for (const auto& prefix : history::parseS3Prefixes(xml)) {
            if (prefix.size() <= base.size() || prefix.substr(0, base.size()) != base) continue;
            std::string symbol = prefix.substr(base.size());
            if (!symbol.empty() && symbol.back() == '/') symbol.pop_back();
            if (symbol.empty() || (!needle.empty() && symbol.find(needle) == std::string::npos)) continue;
            symbols.push_back(symbol);
        }
        std::sort(symbols.begin(), symbols.end());
        symbols.erase(std::unique(symbols.begin(), symbols.end()), symbols.end());
        return symbols;
    };

    auto aggSymbols = listSymbols(history::VisionChannel::AggTrades);
    if (!error.empty()) return {};
    auto bookSymbols = listSymbols(history::VisionChannel::BookTicker);
    if (!error.empty()) return {};

    std::vector<std::string> out;
    std::set_intersection(aggSymbols.begin(),
                          aggSymbols.end(),
                          bookSymbols.begin(),
                          bookSymbols.end(),
                          std::back_inserter(out));
    return out;
}

bool channelHasAnyHistory(history::VisionChannel channel, std::string_view symbol, std::string& error) {
    std::string xml;
    const std::string prefix = history::visionDailyPrefix(channel, symbol);
    if (!downloadText(history::visionListUrl(prefix, false, 1), xml, error)) return false;
    return !history::parseS3Keys(xml).empty();
}

std::vector<history::Date> availableDatesForChannel(history::VisionChannel channel,
                                                    std::string_view symbol,
                                                    history::Date startDate,
                                                    std::string& error) {
    std::string xml;
    const std::string startAfter = history::visionDailyKey(channel, symbol, history::addDays(startDate, -1));
    if (!downloadText(history::visionListUrl(history::visionDailyPrefix(channel, symbol), false, 1000, startAfter), xml, error)) return {};
    return history::parseAvailableVisionDates(xml, channel, symbol);
}

std::vector<history::Date> resolveLatestAvailableDates(std::string_view symbol, int days, std::string& error) {
    if (!channelHasAnyHistory(history::VisionChannel::AggTrades, symbol, error)) {
        if (error.empty()) error = "Binance Vision has no daily aggTrades history for " + std::string{symbol};
        return {};
    }
    if (!channelHasAnyHistory(history::VisionChannel::BookTicker, symbol, error)) {
        if (error.empty()) {
            error = "Binance Vision has no daily bookTicker history for " + std::string{symbol} +
                    "; exact recorder import requires both aggTrades and bookTicker";
        }
        return {};
    }

    const history::Date last = history::lastCompletedUtcDate();
    constexpr int lookbacks[] = {14, 45, 120, 365, 1095, 3660};
    std::vector<history::Date> best;
    for (const int lookback : lookbacks) {
        if (lookback < days) continue;
        const history::Date start = history::addDays(last, 1 - lookback);
        auto aggDates = availableDatesForChannel(history::VisionChannel::AggTrades, symbol, start, error);
        if (!error.empty()) return {};
        auto bookDates = availableDatesForChannel(history::VisionChannel::BookTicker, symbol, start, error);
        if (!error.empty()) return {};
        best = history::latestCommonDates(std::move(aggDates), std::move(bookDates), last, static_cast<std::size_t>(days));
        if (best.size() == static_cast<std::size_t>(days)) return best;
    }

    std::ostringstream out;
    out << "Binance Vision has only " << best.size() << " exact dates with aggTrades + bookTicker for "
        << symbol << " in the recent lookup window";
    if (!best.empty()) {
        out << " (latest range " << history::formatDate(best.front()) << ".." << history::formatDate(best.back()) << ")";
    }
    error = out.str();
    return {};
}

void printHelp() {
    std::puts("history - Binance Vision USD-M futures importer for hft-recorder");
    std::puts("");
    std::puts("Usage:");
    std::puts("  history");
    std::puts("  history --symbol AGLDUSDT --days 5 [--out /mnt/d/recordings]");
    std::puts("  history --symbol AGLDUSDT --from 2026-06-22 --to 2026-06-26 [--out /mnt/d/recordings]");
    std::puts("  history --search AGLD");
    std::puts("");
    std::puts("Notes:");
    std::puts("  Source is data/futures/um only: aggTrades + bookTicker.");
    std::puts("  Symbol search returns only pairs with both daily aggTrades and daily bookTicker history.");
    std::puts("  --days selects the latest available exact dates for that symbol, not raw calendar days.");
    std::puts("  Missing required bookTicker zip aborts the import.");
}

bool parseOptions(int argc, char** argv, Options& out, std::string& error) {
    out = Options{};
    out.interactive = argc == 1;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        auto requireValue = [&](std::string_view name) -> const char* {
            if (i + 1 >= argc) {
                error = "missing value for " + std::string{name};
                return nullptr;
            }
            return argv[++i];
        };
        if (arg == "--help" || arg == "-h") out.help = true;
        else if (arg == "--version" || arg == "-v") out.version = true;
        else if (arg == "--symbol") {
            const char* value = requireValue(arg);
            if (value == nullptr) return false;
            out.symbol = upperAscii(value);
        } else if (arg == "--days") {
            const char* value = requireValue(arg);
            if (value == nullptr) return false;
            char* end = nullptr;
            const long days = std::strtol(value, &end, 10);
            if (end == value || *end != '\0' || days <= 0 || days > 3660) {
                error = "invalid --days value";
                return false;
            }
            out.days = static_cast<int>(days);
        } else if (arg == "--from") {
            const char* value = requireValue(arg);
            if (value == nullptr) return false;
            history::Date date{};
            if (!history::parseDate(value, date)) {
                error = "invalid --from date";
                return false;
            }
            out.from = date;
        } else if (arg == "--to") {
            const char* value = requireValue(arg);
            if (value == nullptr) return false;
            history::Date date{};
            if (!history::parseDate(value, date)) {
                error = "invalid --to date";
                return false;
            }
            out.to = date;
        } else if (arg == "--out") {
            const char* value = requireValue(arg);
            if (value == nullptr) return false;
            out.outRoot = recordings::normalizeExplicitRecordingsPath(value);
        } else if (arg == "--search") {
            const char* value = requireValue(arg);
            if (value == nullptr) return false;
            out.search = value;
        } else {
            error = "unknown argument: " + std::string{arg};
            return false;
        }
    }
    return true;
}

std::vector<history::Date> resolveDates(const Options& options, std::string& error) {
    if (options.from.has_value() || options.to.has_value()) {
        if (!options.from.has_value() || !options.to.has_value()) {
            error = "--from and --to must be used together";
            return {};
        }
        auto dates = history::inclusiveDateRange(*options.from, *options.to);
        if (dates.empty()) error = "--to must be >= --from";
        return dates;
    }
    const int days = options.days.value_or(5);
    if (options.symbol.empty()) {
        error = "missing --symbol for --days";
        return {};
    }
    return resolveLatestAvailableDates(options.symbol, days, error);
}

bool runInteractive(Options& options, std::vector<history::Date>& dates, std::string& error) {
    std::string query;
    std::cout << "symbol search: ";
    if (!std::getline(std::cin, query)) {
        error = "failed to read symbol search";
        return false;
    }
    auto matches = searchSymbols(query, error);
    if (matches.empty()) {
        if (error.empty()) error = "no exact Binance USD-M futures symbols matched search (requires aggTrades + bookTicker)";
        return false;
    }
    const std::size_t limit = std::min<std::size_t>(matches.size(), 30u);
    for (std::size_t i = 0; i < limit; ++i) std::cout << (i + 1u) << ") " << matches[i] << '\n';
    std::cout << "select [1]: ";
    std::string selected;
    if (!std::getline(std::cin, selected)) {
        error = "failed to read selection";
        return false;
    }
    std::size_t index = 1;
    if (!selected.empty()) {
        try {
            index = static_cast<std::size_t>(std::stoul(selected));
        } catch (...) {
            error = "invalid selection";
            return false;
        }
    }
    if (index == 0 || index > limit) {
        error = "selection out of range";
        return false;
    }
    options.symbol = matches[index - 1u];

    std::cout << "days [5] or range YYYY-MM-DD YYYY-MM-DD: ";
    std::string range;
    if (!std::getline(std::cin, range)) {
        error = "failed to read date range";
        return false;
    }
    if (range.empty()) {
        options.days = 5;
    } else if (range.find(' ') == std::string::npos) {
        try {
            const int days = std::stoi(range);
            if (days <= 0) {
                error = "days must be positive";
                return false;
            }
            options.days = days;
        } catch (...) {
            error = "invalid days value";
            return false;
        }
    } else {
        std::istringstream ss(range);
        std::string fromText;
        std::string toText;
        ss >> fromText >> toText;
        history::Date from{};
        history::Date to{};
        if (!history::parseDate(fromText, from) || !history::parseDate(toText, to)) {
            error = "invalid date range";
            return false;
        }
        options.from = from;
        options.to = to;
    }
    dates = resolveDates(options, error);
    if (dates.empty()) return false;
    std::cout << "will import " << options.symbol << " "
              << history::formatDate(dates.front()) << ".." << history::formatDate(dates.back())
              << " into " << options.outRoot.string() << '\n';
    std::cout << "continue? [y/N]: ";
    std::string confirm;
    if (!std::getline(std::cin, confirm)) return false;
    return confirm == "y" || confirm == "Y" || confirm == "yes" || confirm == "YES";
}

}  // namespace
}  // namespace hftrec::app

int main(int argc, char** argv) {
    hftrec::app::Options options{};
    std::string error;
    if (!hftrec::app::parseOptions(argc, argv, options, error)) {
        std::fprintf(stderr, "history: %s\n", error.c_str());
        hftrec::app::printHelp();
        return 2;
    }
    if (options.help) {
        hftrec::app::printHelp();
        return 0;
    }
    if (options.version) {
        std::printf("history %s\n", hftrec::kHftRecorderVersion);
        return 0;
    }
    if (!options.search.empty()) {
        const auto matches = hftrec::app::searchSymbols(options.search, error);
        if (!error.empty()) {
            std::fprintf(stderr, "history: %s\n", error.c_str());
            return 1;
        }
        for (const auto& symbol : matches) std::puts(symbol.c_str());
        return 0;
    }

    std::vector<hftrec::history::Date> dates;
    if (options.interactive) {
        if (!hftrec::app::runInteractive(options, dates, error)) {
            if (!error.empty()) std::fprintf(stderr, "history: %s\n", error.c_str());
            return 1;
        }
    } else {
        dates = hftrec::app::resolveDates(options, error);
        if (!error.empty()) {
            std::fprintf(stderr, "history: %s\n", error.c_str());
            return 2;
        }
    }

    if (!hftrec::app::importHistory(options, dates, error)) {
        std::fprintf(stderr, "history: %s\n", error.c_str());
        return 1;
    }
    return 0;
}
