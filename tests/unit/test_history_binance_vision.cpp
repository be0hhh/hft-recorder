#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <zlib.h>

#include "core/capture/JsonSerializers.hpp"
#include "core/history/BinanceVisionFormat.hpp"
#include "core/history/ZipReader.hpp"
#include "core/replay/JsonLineParser.hpp"

namespace hftrec::history {
namespace {

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

void appendU16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
}

void appendU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xffu));
}

std::vector<std::uint8_t> rawDeflate(std::string_view text) {
    std::vector<std::uint8_t> out(text.size() + 64u);
    z_stream stream{};
    EXPECT_EQ(deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY), Z_OK);
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(text.data()));
    stream.avail_in = static_cast<uInt>(text.size());
    stream.next_out = out.data();
    stream.avail_out = static_cast<uInt>(out.size());
    EXPECT_EQ(deflate(&stream, Z_FINISH), Z_STREAM_END);
    out.resize(stream.total_out);
    EXPECT_EQ(deflateEnd(&stream), Z_OK);
    return out;
}

std::vector<std::uint8_t> makeSingleFileZip(std::string_view name, std::string_view text) {
    const auto compressed = rawDeflate(text);
    const std::uint32_t crc = crc32(crc32(0L, Z_NULL, 0),
                                   reinterpret_cast<const Bytef*>(text.data()),
                                   static_cast<uInt>(text.size()));
    std::vector<std::uint8_t> out;
    appendU32(out, 0x04034b50u);
    appendU16(out, 20u);
    appendU16(out, 0u);
    appendU16(out, 8u);
    appendU16(out, 0u);
    appendU16(out, 0u);
    appendU32(out, crc);
    appendU32(out, static_cast<std::uint32_t>(compressed.size()));
    appendU32(out, static_cast<std::uint32_t>(text.size()));
    appendU16(out, static_cast<std::uint16_t>(name.size()));
    appendU16(out, 0u);
    out.insert(out.end(), name.begin(), name.end());
    out.insert(out.end(), compressed.begin(), compressed.end());

    const std::uint32_t centralOffset = static_cast<std::uint32_t>(out.size());
    appendU32(out, 0x02014b50u);
    appendU16(out, 20u);
    appendU16(out, 20u);
    appendU16(out, 0u);
    appendU16(out, 8u);
    appendU16(out, 0u);
    appendU16(out, 0u);
    appendU32(out, crc);
    appendU32(out, static_cast<std::uint32_t>(compressed.size()));
    appendU32(out, static_cast<std::uint32_t>(text.size()));
    appendU16(out, static_cast<std::uint16_t>(name.size()));
    appendU16(out, 0u);
    appendU16(out, 0u);
    appendU16(out, 0u);
    appendU16(out, 0u);
    appendU32(out, 0u);
    appendU32(out, 0u);
    out.insert(out.end(), name.begin(), name.end());
    const std::uint32_t centralSize = static_cast<std::uint32_t>(out.size()) - centralOffset;

    appendU32(out, 0x06054b50u);
    appendU16(out, 0u);
    appendU16(out, 0u);
    appendU16(out, 1u);
    appendU16(out, 1u);
    appendU32(out, centralSize);
    appendU32(out, centralOffset);
    appendU16(out, 0u);
    return out;
}

TEST(BinanceVisionHistory, ParsesDecimalE8WithoutFloat) {
    std::int64_t value = 0;
    EXPECT_TRUE(parseDecimalE8("0.12345678", value));
    EXPECT_EQ(value, 12345678);
    EXPECT_TRUE(parseDecimalE8("3538.0", value));
    EXPECT_EQ(value, 353800000000LL);
    EXPECT_TRUE(parseDecimalE8("1", value));
    EXPECT_EQ(value, 100000000);
    EXPECT_FALSE(parseDecimalE8("1.123456789", value));
}

TEST(BinanceVisionHistory, ParsesAndRejectsDates) {
    Date date{};
    EXPECT_TRUE(parseDate("2026-06-26", date));
    EXPECT_EQ(formatDate(date), "2026-06-26");
    EXPECT_FALSE(parseDate("2026-1x-26", date));
    EXPECT_FALSE(parseDate("2026-06-x6", date));
    EXPECT_FALSE(parseDate("2026-02-30", date));
}

TEST(BinanceVisionHistory, BuildsS3ListUrlWithStartAfter) {
    EXPECT_EQ(visionListUrl("data/futures/um/daily/bookTicker/AGLDUSDT/",
                            false,
                            1000,
                            "data/futures/um/daily/bookTicker/AGLDUSDT/AGLDUSDT-bookTicker-2026-06-01.zip"),
              "https://s3-ap-northeast-1.amazonaws.com/data.binance.vision?list-type=2&prefix=data/futures/um/daily/bookTicker/AGLDUSDT/&max-keys=1000&start-after=data/futures/um/daily/bookTicker/AGLDUSDT/AGLDUSDT-bookTicker-2026-06-01.zip");
}

TEST(BinanceVisionHistory, ConvertsAggTradeCsvToRecorderTradeRow) {
    const ImportIdentity identity{.symbol = "AGLDUSDT"};
    replay::TradeRow row{};
    std::string error;

    ASSERT_TRUE(parseAggTradeCsvLine("101421200,0.1232,3538.0,241470566,241470568,1782432000038,True",
                                     identity,
                                     7,
                                     row,
                                     error));

    EXPECT_EQ(row.tradeId, 101421200u);
    EXPECT_EQ(row.firstTradeId, 241470566u);
    EXPECT_EQ(row.lastTradeId, 241470568u);
    EXPECT_EQ(row.symbol, "AGLDUSDT");
    EXPECT_EQ(row.exchange, "binance");
    EXPECT_EQ(row.market, "futures");
    EXPECT_EQ(row.tsNs, 1782432000038000000LL);
    EXPECT_EQ(row.captureSeq, 7);
    EXPECT_EQ(row.ingestSeq, 7);
    EXPECT_EQ(row.priceE8, 12320000);
    EXPECT_EQ(row.qtyE8, 353800000000LL);
    EXPECT_EQ(row.quoteQtyE8, 43588160000LL);
    EXPECT_EQ(row.side, 0);
    EXPECT_EQ(row.sideBuy, 0u);
    EXPECT_EQ(row.isBuyerMaker, 1u);
}

TEST(BinanceVisionHistory, ConvertsAggTradeCsvFalseBoolToTakerBuy) {
    const ImportIdentity identity{.symbol = "AGLDUSDT"};
    replay::TradeRow row{};
    std::string error;

    ASSERT_TRUE(parseAggTradeCsvLine("101421201,0.1232,1.0,241470569,241470569,1782432000040,False",
                                     identity,
                                     8,
                                     row,
                                     error));

    EXPECT_EQ(row.side, 1);
    EXPECT_EQ(row.sideBuy, 1u);
    EXPECT_EQ(row.isBuyerMaker, 0u);
}

TEST(BinanceVisionHistory, ConvertsObservedLabAggTradeCsvRow) {
    const ImportIdentity identity{.symbol = "LABUSDT"};
    replay::TradeRow row{};
    std::string error;

    ASSERT_TRUE(parseAggTradeCsvLine("291582192,14.544,10.0,528129694,528129695,1782259200338,false",
                                     identity,
                                     1,
                                     row,
                                     error)) << error;

    EXPECT_EQ(row.tradeId, 291582192u);
    EXPECT_EQ(row.firstTradeId, 528129694u);
    EXPECT_EQ(row.lastTradeId, 528129695u);
    EXPECT_EQ(row.symbol, "LABUSDT");
    EXPECT_EQ(row.tsNs, 1782259200338000000LL);
    EXPECT_EQ(row.priceE8, 1454400000LL);
    EXPECT_EQ(row.qtyE8, 1000000000LL);
    EXPECT_EQ(row.quoteQtyE8, 14544000000LL);
    EXPECT_EQ(row.side, 1);
    EXPECT_EQ(row.sideBuy, 1u);
    EXPECT_EQ(row.isBuyerMaker, 0u);
}

TEST(BinanceVisionHistory, ConvertsBookTickerCsvToRecorderBookTickerRow) {
    const ImportIdentity identity{.symbol = "AGLDUSDT"};
    replay::BookTickerRow row{};
    std::string error;

    ASSERT_TRUE(parseBookTickerCsvLine("3120782747429,0.64710000,1912.00000000,0.64720000,914.00000000,1690848000120,1690848000125",
                                       identity,
                                       9,
                                       row,
                                       error));

    EXPECT_EQ(row.symbol, "AGLDUSDT");
    EXPECT_EQ(row.exchange, "binance");
    EXPECT_EQ(row.market, "futures");
    EXPECT_EQ(row.tsNs, 1690848000125000000LL);
    EXPECT_EQ(row.captureSeq, 9);
    EXPECT_EQ(row.ingestSeq, 9);
    EXPECT_EQ(row.bidPriceE8, 64710000);
    EXPECT_EQ(row.bidQtyE8, 191200000000LL);
    EXPECT_EQ(row.askPriceE8, 64720000);
    EXPECT_EQ(row.askQtyE8, 91400000000LL);
}

TEST(BinanceVisionHistory, RenderedRowsParseWithRecorderStrictParser) {
    const ImportIdentity identity{.symbol = "AGLDUSDT"};
    replay::TradeRow trade{};
    replay::BookTickerRow book{};
    std::string error;

    ASSERT_TRUE(parseAggTradeCsvLine("101421200,0.1232,3538.0,241470566,241470568,1782432000038,true",
                                     identity,
                                     1,
                                     trade,
                                     error));
    ASSERT_TRUE(parseBookTickerCsvLine("3120782747429,0.64710000,1912.00000000,0.64720000,914.00000000,1690848000120,1690848000125",
                                       identity,
                                       2,
                                       book,
                                       error));

    replay::TradeRow parsedTrade{};
    replay::BookTickerRow parsedBook{};
    EXPECT_EQ(replay::parseTradeLine(capture::renderTradeJsonLine(trade, tradeAliases()), parsedTrade), Status::Ok);
    EXPECT_EQ(replay::parseBookTickerLine(capture::renderBookTickerJsonLine(book), parsedBook), Status::Ok);
    EXPECT_EQ(parsedTrade.tradeId, trade.tradeId);
    EXPECT_EQ(parsedTrade.firstTradeId, trade.firstTradeId);
    EXPECT_EQ(parsedTrade.lastTradeId, trade.lastTradeId);
    EXPECT_EQ(parsedTrade.quoteQtyE8, trade.quoteQtyE8);
    EXPECT_EQ(parsedTrade.isBuyerMaker, trade.isBuyerMaker);
    EXPECT_EQ(parsedBook.tsNs, book.tsNs);
    EXPECT_EQ(parsedBook.bidPriceE8, book.bidPriceE8);
    EXPECT_EQ(parsedBook.askPriceE8, book.askPriceE8);
}

TEST(BinanceVisionHistory, ParsesS3KeysFromListing) {
    const std::string xml =
        "<ListBucketResult>"
        "<Contents><Key>data/futures/um/daily/bookTicker/AGLDUSDT/AGLDUSDT-bookTicker-2023-08-01.zip</Key><Size>3196753</Size></Contents>"
        "<Contents><Key>data/futures/um/daily/bookTicker/AGLDUSDT/AGLDUSDT-bookTicker-2023-08-01.zip.CHECKSUM</Key><Size>101</Size></Contents>"
        "</ListBucketResult>";

    const auto keys = parseS3Keys(xml);
    ASSERT_EQ(keys.size(), 2u);
    EXPECT_EQ(keys[0], "data/futures/um/daily/bookTicker/AGLDUSDT/AGLDUSDT-bookTicker-2023-08-01.zip");
    EXPECT_EQ(keys[1], "data/futures/um/daily/bookTicker/AGLDUSDT/AGLDUSDT-bookTicker-2023-08-01.zip.CHECKSUM");
    EXPECT_TRUE(hasVisionZipAndChecksum(xml, "data/futures/um/daily/bookTicker/AGLDUSDT/AGLDUSDT-bookTicker-2023-08-01.zip"));
    EXPECT_FALSE(hasVisionZipAndChecksum(xml, "data/futures/um/daily/bookTicker/LABUSDT/LABUSDT-bookTicker-2026-06-24.zip"));
}

TEST(BinanceVisionHistory, SelectsLatestCommonDatesWithZipAndChecksum) {
    const std::string aggXml =
        "<ListBucketResult>"
        "<Contents><Key>data/futures/um/daily/aggTrades/AGLDUSDT/AGLDUSDT-aggTrades-2026-06-24.zip</Key></Contents>"
        "<Contents><Key>data/futures/um/daily/aggTrades/AGLDUSDT/AGLDUSDT-aggTrades-2026-06-24.zip.CHECKSUM</Key></Contents>"
        "<Contents><Key>data/futures/um/daily/aggTrades/AGLDUSDT/AGLDUSDT-aggTrades-2026-06-25.zip</Key></Contents>"
        "<Contents><Key>data/futures/um/daily/aggTrades/AGLDUSDT/AGLDUSDT-aggTrades-2026-06-25.zip.CHECKSUM</Key></Contents>"
        "<Contents><Key>data/futures/um/daily/aggTrades/AGLDUSDT/AGLDUSDT-aggTrades-2026-06-26.zip</Key></Contents>"
        "<Contents><Key>data/futures/um/daily/aggTrades/AGLDUSDT/AGLDUSDT-aggTrades-2026-06-26.zip.CHECKSUM</Key></Contents>"
        "</ListBucketResult>";
    const std::string bookXml =
        "<ListBucketResult>"
        "<Contents><Key>data/futures/um/daily/bookTicker/AGLDUSDT/AGLDUSDT-bookTicker-2026-06-24.zip</Key></Contents>"
        "<Contents><Key>data/futures/um/daily/bookTicker/AGLDUSDT/AGLDUSDT-bookTicker-2026-06-24.zip.CHECKSUM</Key></Contents>"
        "<Contents><Key>data/futures/um/daily/bookTicker/AGLDUSDT/AGLDUSDT-bookTicker-2026-06-25.zip</Key></Contents>"
        "<Contents><Key>data/futures/um/daily/bookTicker/AGLDUSDT/AGLDUSDT-bookTicker-2026-06-25.zip.CHECKSUM</Key></Contents>"
        "<Contents><Key>data/futures/um/daily/bookTicker/AGLDUSDT/AGLDUSDT-bookTicker-2026-06-26.zip</Key></Contents>"
        "</ListBucketResult>";

    Date notAfter{};
    ASSERT_TRUE(parseDate("2026-06-27", notAfter));
    const auto dates = latestCommonDates(parseAvailableVisionDates(aggXml, VisionChannel::AggTrades, "AGLDUSDT"),
                                         parseAvailableVisionDates(bookXml, VisionChannel::BookTicker, "AGLDUSDT"),
                                         notAfter,
                                         2);
    ASSERT_EQ(dates.size(), 2u);
    EXPECT_EQ(formatDate(dates[0]), "2026-06-24");
    EXPECT_EQ(formatDate(dates[1]), "2026-06-25");
}

TEST(BinanceVisionHistory, ExtractsDeflatedSingleFileZip) {
    const auto zip = makeSingleFileZip("sample.csv", "a,b,c\n1,2,3\n");
    std::string name;
    std::string text;
    std::string error;

    ASSERT_TRUE(extractSingleFileZip(zip, name, text, error)) << error;
    EXPECT_EQ(name, "sample.csv");
    EXPECT_EQ(text, "a,b,c\n1,2,3\n");
}

}  // namespace
}  // namespace hftrec::history
