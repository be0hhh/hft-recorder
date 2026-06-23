#include <chrono>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "core/capture/ChannelKind.hpp"
#include "core/storage/EventStorage.hpp"
#include "core/storage/JsonSessionStorage.hpp"
#include "gui/viewer/LiveDataProvider.hpp"

namespace fs = std::filesystem;

namespace {

struct Options {
    std::string mode{"json-only"};
    std::uint64_t rows{100000};
    std::size_t depthLevels{20};
    fs::path outputDir{fs::temp_directory_path() / "hftrec_recorder_memory_soak"};
};

std::uint64_t rssKb() {
    std::ifstream in("/proc/self/status");
    std::string key;
    while (in >> key) {
        if (key == "VmRSS:") {
            std::uint64_t value = 0;
            in >> value;
            return value;
        }
        std::string rest;
        std::getline(in, rest);
    }
    return 0;
}

bool parseU64(std::string_view text, std::uint64_t& out) {
    const std::string raw{text};
    char* end = nullptr;
    const auto value = std::strtoull(raw.c_str(), &end, 10);
    if (end == nullptr || *end != '\0') return false;
    out = static_cast<std::uint64_t>(value);
    return true;
}

Options parseOptions(int argc, char** argv) {
    Options options{};
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--mode" && i + 1 < argc) {
            options.mode = argv[++i];
        } else if (arg == "--rows" && i + 1 < argc) {
            (void)parseU64(argv[++i], options.rows);
        } else if (arg == "--depth-levels" && i + 1 < argc) {
            std::uint64_t levels = 0;
            if (parseU64(argv[++i], levels)) options.depthLevels = static_cast<std::size_t>(levels);
        } else if (arg == "--output-dir" && i + 1 < argc) {
            options.outputDir = argv[++i];
        }
    }
    return options;
}

hftrec::replay::TradeRow tradeRow(std::uint64_t i) {
    hftrec::replay::TradeRow row{};
    row.symbol = "SOAKUSDT";
    row.exchange = "bench";
    row.market = "synthetic";
    row.tsNs = static_cast<std::int64_t>(i + 1u);
    row.captureSeq = static_cast<std::int64_t>(i + 1u);
    row.ingestSeq = static_cast<std::int64_t>(i + 1u);
    row.priceE8 = 10000000000ll + static_cast<std::int64_t>(i % 1000u);
    row.qtyE8 = 100000000ll;
    row.side = static_cast<std::int64_t>(i & 1u);
    row.sideBuy = static_cast<std::uint8_t>(i & 1u);
    return row;
}

hftrec::replay::BookTickerRow bookTickerRow(std::uint64_t i) {
    hftrec::replay::BookTickerRow row{};
    row.symbol = "SOAKUSDT";
    row.exchange = "bench";
    row.market = "synthetic";
    row.tsNs = static_cast<std::int64_t>(i + 1u);
    row.captureSeq = static_cast<std::int64_t>(i + 1u);
    row.ingestSeq = static_cast<std::int64_t>(i + 1u);
    row.bidPriceE8 = 10000000000ll + static_cast<std::int64_t>(i % 1000u);
    row.askPriceE8 = row.bidPriceE8 + 1000;
    row.bidQtyE8 = 100000000ll;
    row.askQtyE8 = 110000000ll;
    return row;
}

hftrec::replay::DepthRow depthRow(std::uint64_t i, std::size_t levels) {
    hftrec::replay::DepthRow row{};
    row.tsNs = static_cast<std::int64_t>(i + 1u);
    row.levels.reserve(levels);
    for (std::size_t level = 0; level < levels; ++level) {
        row.levels.push_back(hftrec::replay::PricePair{
            10000000000ll + static_cast<std::int64_t>(level * 100 + (i % 100u)),
            100000000ll + static_cast<std::int64_t>(level),
            static_cast<std::int64_t>(level & 1u),
        });
    }
    return row;
}

bool appendRows(hftrec::storage::JsonSessionSink& sink,
                hftrec::storage::LiveEventStore* store,
                std::uint64_t i,
                std::size_t depthLevels) {
    const auto trade = tradeRow(i);
    const auto ticker = bookTickerRow(i);
    const auto depth = depthRow(i, depthLevels);
    if (store != nullptr) {
        if (!hftrec::isOk(store->appendTrade(trade))) return false;
        if (!hftrec::isOk(store->appendBookTicker(ticker))) return false;
        if (!hftrec::isOk(store->appendDepth(depth))) return false;
    }
    return hftrec::isOk(sink.appendTrade(trade))
        && hftrec::isOk(sink.appendBookTicker(ticker))
        && hftrec::isOk(sink.appendDepth(depth));
}

int run(const Options& options) {
    std::error_code ec;
    const auto runId = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path sessionDir = options.outputDir / ("session_" + std::to_string(runId));
    fs::create_directories(sessionDir / "jsonl", ec);
    if (ec) return 2;

    hftrec::storage::JsonSessionSink sink{};
    if (!hftrec::isOk(sink.open(sessionDir))) return 2;
    (void)sink.ensureChannelFile(hftrec::capture::ChannelKind::Trades);
    (void)sink.ensureChannelFile(hftrec::capture::ChannelKind::BookTicker);
    (void)sink.ensureChannelFile(hftrec::capture::ChannelKind::DepthTape);
    (void)sink.ensureChannelFile(hftrec::capture::ChannelKind::DepthSidecar);

    hftrec::storage::LiveEventStore store{};
    hftrec::storage::LiveEventStore* storePtr = options.mode == "full-cache" ? &store : nullptr;
    hftrec::gui::viewer::JsonTailLiveDataProvider provider{};
    if (options.mode == "tail-provider") {
        provider.start(hftrec::gui::viewer::LiveDataProviderConfig{sessionDir, "SOAKUSDT", "soak"});
    }

    const auto rssStart = rssKb();
    auto rssPeak = rssStart;
    const auto started = std::chrono::steady_clock::now();
    std::uint64_t batchId = 1;
    for (std::uint64_t i = 0; i < options.rows; ++i) {
        if (!appendRows(sink, storePtr, i, options.depthLevels)) return 3;
        if ((i + 1u) % 4096u == 0u) {
            (void)sink.flush();
            if (options.mode == "tail-provider") (void)provider.pollHot(batchId++);
            rssPeak = std::max(rssPeak, rssKb());
        }
    }
    (void)sink.flush();
    if (options.mode == "tail-provider") (void)provider.pollHot(batchId++);
    rssPeak = std::max(rssPeak, rssKb());
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

    std::printf("mode,rows,depth_levels,rss_start_kb,rss_peak_kb,rss_end_kb,rows_per_sec\n");
    std::printf("%s,%llu,%zu,%llu,%llu,%llu,%.2f\n",
                options.mode.c_str(),
                static_cast<unsigned long long>(options.rows),
                options.depthLevels,
                static_cast<unsigned long long>(rssStart),
                static_cast<unsigned long long>(rssPeak),
                static_cast<unsigned long long>(rssKb()),
                elapsed > 0.0 ? static_cast<double>(options.rows) / elapsed : 0.0);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const Options options = parseOptions(argc, argv);
    if (options.mode != "json-only" && options.mode != "full-cache" && options.mode != "tail-provider") {
        std::fprintf(stderr, "unknown --mode '%s'\n", options.mode.c_str());
        return 1;
    }
    return run(options);
}
