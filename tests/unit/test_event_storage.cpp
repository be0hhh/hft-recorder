#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "core/storage/EventStorage.hpp"
#include "core/storage/JsonSessionStorage.hpp"
#include "gui/viewer/LiveDataProvider.hpp"

namespace fs = std::filesystem;

namespace {

constexpr char kJsonSessionId[] = {'j', 's', 'o', 'n', '_', 's', 'e', 's', 's', 'i', 'o', 'n', '\0'};

fs::path makeTmpDir() {
    const auto base = fs::temp_directory_path();
    auto dir = base / (std::string{"hftrec_event_storage_"} + std::to_string(std::rand()));
    fs::create_directories(dir);
    return dir;
}

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

hftrec::replay::TradeRow tradeRow(std::int64_t tsNs, std::int64_t captureSeq, std::int64_t ingestSeq) {
    hftrec::replay::TradeRow row{};
    row.tsNs = tsNs;
    row.captureSeq = captureSeq;
    row.ingestSeq = ingestSeq;
    row.priceE8 = 30000;
    row.qtyE8 = 10;
    row.side = 1;
    row.sideBuy = 1u;
    return row;
}

class StubIngress final : public hftrec::market_data::IMarketDataIngress {
  public:
    explicit StubIngress(const hftrec::storage::IEventSource* source) : source_(source) {}

    const hftrec::storage::IEventSource* eventSource() const noexcept override { return source_; }
    const hftrec::storage::IHotEventCache* hotCache() const noexcept override { return nullptr; }

  private:
    const hftrec::storage::IEventSource* source_{nullptr};
};

}  // namespace

TEST(EventStorage, LiveStoreKeepsExactRowsAndRanges) {
    hftrec::storage::LiveEventStore store{};
    ASSERT_EQ(store.appendTrade(tradeRow(100, 1, 1)), hftrec::Status::Ok);
    ASSERT_EQ(store.appendTrade(tradeRow(200, 2, 2)), hftrec::Status::Ok);

    const auto all = store.readAll();
    ASSERT_EQ(all.trades.size(), 2u);
    EXPECT_EQ(all.trades[0].tsNs, 100);
    EXPECT_EQ(all.trades[1].ingestSeq, 2);

    const auto range = store.readRange(150, 250);
    ASSERT_EQ(range.trades.size(), 1u);
    EXPECT_EQ(range.trades[0].tsNs, 200);

    const auto stats = store.stats();
    EXPECT_EQ(stats.tradesTotal, 2u);
    EXPECT_GE(stats.version, 2u);
}


TEST(EventStorage, LiveStoreRangeHandlesEmptyAndSingleRowWindows) {
    hftrec::storage::LiveEventStore store{};
    ASSERT_EQ(store.appendTrade(tradeRow(100, 1, 1)), hftrec::Status::Ok);
    ASSERT_EQ(store.appendTrade(tradeRow(200, 2, 2)), hftrec::Status::Ok);
    ASSERT_EQ(store.appendTrade(tradeRow(300, 3, 3)), hftrec::Status::Ok);

    const auto empty = store.readRange(201, 299);
    EXPECT_TRUE(empty.trades.empty());

    const auto single = store.readRange(200, 200);
    ASSERT_EQ(single.trades.size(), 1u);
    EXPECT_EQ(single.trades[0].tsNs, 200);
}
TEST(EventStorage, LiveStoreReadsOnlyDeltaRowsFromOffsets) {
    hftrec::storage::LiveEventStore store{};
    ASSERT_EQ(store.appendTrade(tradeRow(100, 1, 1)), hftrec::Status::Ok);
    ASSERT_EQ(store.appendTrade(tradeRow(200, 2, 2)), hftrec::Status::Ok);
    ASSERT_EQ(store.appendTrade(tradeRow(300, 3, 3)), hftrec::Status::Ok);

    const auto delta = store.readSince(2u, 0u, 0u, 0u, 0u);
    ASSERT_EQ(delta.trades.size(), 1u);
    EXPECT_EQ(delta.trades[0].tsNs, 300);
}

TEST(EventStorage, LiveStoreReadsSnapshotDeltaFromOffsets) {
    hftrec::storage::LiveEventStore store{};
    hftrec::replay::SnapshotDocument first{};
    first.tsNs = 100;
    first.updateId = 10;
    hftrec::replay::SnapshotDocument second{};
    second.tsNs = 200;
    second.updateId = 20;

    ASSERT_EQ(store.appendSnapshot(first, 0u), hftrec::Status::Ok);
    ASSERT_EQ(store.appendSnapshot(second, 1u), hftrec::Status::Ok);

    const auto delta = store.readSince(0u, 0u, 0u, 0u, 1u);
    ASSERT_EQ(delta.snapshots.size(), 1u);
    EXPECT_EQ(delta.snapshots[0].updateId, 20);
}

TEST(EventStorage, CompositeSinkFansOutToAllSinks) {
    hftrec::storage::LiveEventStore first{};
    hftrec::storage::LiveEventStore second{};
    hftrec::storage::CompositeEventSink sink{};
    sink.addSink(&first);
    sink.addSink(&second);

    ASSERT_EQ(sink.appendTrade(tradeRow(100, 1, 1)), hftrec::Status::Ok);
    EXPECT_EQ(first.readAll().trades.size(), 1u);
    EXPECT_EQ(second.readAll().trades.size(), 1u);
}

TEST(EventStorage, JsonSessionSinkWritesCurrentTradeSchema) {
    const auto dir = makeTmpDir();
    hftrec::storage::JsonSessionSink sink{};
    ASSERT_EQ(sink.open(dir), hftrec::Status::Ok);
    ASSERT_EQ(sink.appendTrade(tradeRow(100, 1, 7)), hftrec::Status::Ok);
    const auto stats = sink.stats();
    ASSERT_EQ(sink.close(), hftrec::Status::Ok);

    const auto text = readFile(dir / "jsonl" / "trades.jsonl");
    EXPECT_NE(text.find("[30000,10,1,100,0,0,0,0,0,\"\",\"\",\"\",1,7]"), std::string::npos);

    EXPECT_STREQ(sink.backendId(), kJsonSessionId);
    EXPECT_EQ(stats.tradesTotal, 1u);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(EventStorage, InMemoryProviderReadsOnlySelectedLiveSource) {
    hftrec::storage::LiveEventStore ethStore{};
    hftrec::storage::LiveEventStore btcStore{};
    ASSERT_EQ(ethStore.appendTrade(tradeRow(100, 1, 1)), hftrec::Status::Ok);
    ASSERT_EQ(btcStore.appendTrade(tradeRow(200, 2, 2)), hftrec::Status::Ok);

    StubIngress ethIngress(&ethStore);
    StubIngress btcIngress(&btcStore);

    auto& registry = hftrec::gui::viewer::LiveDataRegistry::instance();
    registry.setSources({
        {"live:binance:futures:ETHUSDT", "Binance", "Futures", "ETHUSDT", "s1", {}, &ethIngress},
        {"live:binance:futures:BTCUSDT", "Binance", "Futures", "BTCUSDT", "s2", {}, &btcIngress},
    });

    auto provider = registry.makeProvider("live:binance:futures:BTCUSDT");
    ASSERT_NE(provider, nullptr);
    provider->start(hftrec::gui::viewer::LiveDataProviderConfig{{}, {}, "live:binance:futures:BTCUSDT"});

    const auto hot = provider->pollHot(1u);
    ASSERT_TRUE(hot.appendedRows);
    ASSERT_EQ(hot.batch.trades.size(), 1u);
    EXPECT_EQ(hot.batch.trades[0].tsNs, 200);

    const auto range = provider->materializeRange(hftrec::gui::viewer::LiveDataRangeRequest{"BTCUSDT", 150, 250}, 2u);
    ASSERT_EQ(range.trades.size(), 1u);
    EXPECT_EQ(range.trades[0].tsNs, 200);

    EXPECT_TRUE(registry.hasSource("live:binance:futures:ETHUSDT"));
    EXPECT_EQ(registry.snapshotSources().size(), 2u);
    registry.clear();
}


