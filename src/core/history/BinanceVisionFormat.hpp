#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/replay/EventRows.hpp"

namespace hftrec::history {

struct ImportIdentity {
    std::string exchange{"binance"};
    std::string market{"futures"};
    std::string symbol{};
};

struct Date {
    int year{0};
    int month{0};
    int day{0};
};

enum class VisionChannel {
    AggTrades,
    BookTicker,
};

bool parseDate(std::string_view text, Date& out) noexcept;
std::string formatDate(Date date);
Date addDays(Date date, int days) noexcept;
std::vector<Date> inclusiveDateRange(Date from, Date to);
Date lastCompletedUtcDate();

bool parseDecimalE8(std::string_view text, std::int64_t& out) noexcept;
bool parseAggTradeCsvLine(std::string_view line,
                          const ImportIdentity& identity,
                          std::int64_t sequence,
                          replay::TradeRow& out,
                          std::string& error);
bool parseBookTickerCsvLine(std::string_view line,
                            const ImportIdentity& identity,
                            std::int64_t sequence,
                            replay::BookTickerRow& out,
                            std::string& error);

std::string visionChannelName(VisionChannel channel);
std::string visionDailyKey(VisionChannel channel, std::string_view symbol, Date date);
std::string visionDailyPrefix(VisionChannel channel, std::string_view symbol);
std::string visionSymbolPrefix(VisionChannel channel);
std::string visionHttpsUrl(std::string_view key);
std::string visionListUrl(std::string_view prefix,
                          bool delimiter,
                          int maxKeys = 1000,
                          std::string_view startAfter = {});
bool dateFromVisionKey(std::string_view key, VisionChannel channel, std::string_view symbol, Date& out) noexcept;

std::vector<std::string> parseS3Keys(std::string_view xml);
std::vector<std::string> parseS3Prefixes(std::string_view xml);
bool hasVisionZipAndChecksum(std::string_view xml, std::string_view key);
std::vector<Date> parseAvailableVisionDates(std::string_view xml, VisionChannel channel, std::string_view symbol);
std::vector<Date> latestCommonDates(std::vector<Date> lhs, std::vector<Date> rhs, Date notAfter, std::size_t limit);

bool parseChecksumSha256(std::string_view text, std::string& outHex);

}  // namespace hftrec::history
