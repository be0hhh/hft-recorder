#include "core/history/BinanceVisionFormat.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <limits>
#include <optional>
#include <sstream>

namespace hftrec::history {

namespace {

constexpr std::int64_t kScaleE8 = 100000000LL;
constexpr std::string_view kVisionBaseUrl{"https://data.binance.vision/"};

std::string_view trim(std::string_view text) noexcept {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    return text;
}

bool parseI64(std::string_view text, std::int64_t& out) noexcept {
    text = trim(text);
    if (text.empty()) return false;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, out);
    return ec == std::errc{} && ptr == end;
}

bool parseU64(std::string_view text, std::uint64_t& out) noexcept {
    text = trim(text);
    if (text.empty()) return false;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, out);
    return ec == std::errc{} && ptr == end;
}

bool equalsAsciiNoCase(std::string_view lhs, std::string_view rhs) noexcept {
    if (lhs.size() != rhs.size()) return false;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        const auto a = static_cast<unsigned char>(lhs[i]);
        const auto b = static_cast<unsigned char>(rhs[i]);
        if (std::tolower(a) != std::tolower(b)) return false;
    }
    return true;
}

bool parseBool(std::string_view text, bool& out) noexcept {
    text = trim(text);
    if (equalsAsciiNoCase(text, "true") || text == "1") {
        out = true;
        return true;
    }
    if (equalsAsciiNoCase(text, "false") || text == "0") {
        out = false;
        return true;
    }
    return false;
}

bool msToNs(std::int64_t ms, std::int64_t& out) noexcept {
    if (ms < 0) return false;
    if (ms > std::numeric_limits<std::int64_t>::max() / 1000000LL) return false;
    out = ms * 1000000LL;
    return true;
}

std::vector<std::string_view> splitCsv(std::string_view line) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (start <= line.size()) {
        const std::size_t comma = line.find(',', start);
        const std::size_t end = comma == std::string_view::npos ? line.size() : comma;
        out.push_back(trim(line.substr(start, end - start)));
        if (comma == std::string_view::npos) break;
        start = comma + 1u;
    }
    return out;
}

bool scaledNotionalE8(std::int64_t priceE8, std::int64_t qtyE8, std::int64_t& out) noexcept {
    if (priceE8 < 0 || qtyE8 < 0) return false;
    const __int128 product = static_cast<__int128>(priceE8) * static_cast<__int128>(qtyE8);
    const __int128 scaled = product / kScaleE8;
    if (scaled > std::numeric_limits<std::int64_t>::max()) return false;
    out = static_cast<std::int64_t>(scaled);
    return true;
}

std::string xmlDecode(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&') {
            out.push_back(text[i]);
            continue;
        }
        if (text.substr(i, 5) == "&amp;") {
            out.push_back('&');
            i += 4;
        } else if (text.substr(i, 4) == "&lt;") {
            out.push_back('<');
            i += 3;
        } else if (text.substr(i, 4) == "&gt;") {
            out.push_back('>');
            i += 3;
        } else if (text.substr(i, 6) == "&quot;") {
            out.push_back('"');
            i += 5;
        } else if (text.substr(i, 6) == "&apos;") {
            out.push_back('\'');
            i += 5;
        } else {
            out.push_back('&');
        }
    }
    return out;
}

std::vector<std::string> parseTagValues(std::string_view xml, std::string_view tag) {
    std::vector<std::string> out;
    const std::string open = "<" + std::string{tag} + ">";
    const std::string close = "</" + std::string{tag} + ">";
    std::size_t pos = 0;
    while (true) {
        const std::size_t beginTag = xml.find(open, pos);
        if (beginTag == std::string_view::npos) break;
        const std::size_t valueBegin = beginTag + open.size();
        const std::size_t endTag = xml.find(close, valueBegin);
        if (endTag == std::string_view::npos) break;
        out.push_back(xmlDecode(xml.substr(valueBegin, endTag - valueBegin)));
        pos = endTag + close.size();
    }
    return out;
}

bool dateLess(Date lhs, Date rhs) noexcept {
    if (lhs.year != rhs.year) return lhs.year < rhs.year;
    if (lhs.month != rhs.month) return lhs.month < rhs.month;
    return lhs.day < rhs.day;
}

bool dateEqual(Date lhs, Date rhs) noexcept {
    return lhs.year == rhs.year && lhs.month == rhs.month && lhs.day == rhs.day;
}

std::chrono::sys_days toSysDays(Date date) noexcept {
    using namespace std::chrono;
    return sys_days{year{date.year} / month{static_cast<unsigned>(date.month)} / day{static_cast<unsigned>(date.day)}};
}

Date fromSysDays(std::chrono::sys_days days) noexcept {
    const std::chrono::year_month_day ymd{days};
    return Date{static_cast<int>(ymd.year()),
                static_cast<int>(static_cast<unsigned>(ymd.month())),
                static_cast<int>(static_cast<unsigned>(ymd.day()))};
}

std::string zero2(int value) {
    char buf[3]{};
    buf[0] = static_cast<char>('0' + (value / 10) % 10);
    buf[1] = static_cast<char>('0' + value % 10);
    return std::string{buf, 2};
}

const char* channelFolder(VisionChannel channel) noexcept {
    switch (channel) {
        case VisionChannel::AggTrades: return "aggTrades";
        case VisionChannel::BookTicker: return "bookTicker";
    }
    return "";
}

}  // namespace

bool parseDate(std::string_view text, Date& out) noexcept {
    text = trim(text);
    if (text.size() != 10u || text[4] != '-' || text[7] != '-') return false;
    int year = 0;
    int month = 0;
    int day = 0;
    auto parsed = std::from_chars(text.data(), text.data() + 4, year);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + 4) return false;
    parsed = std::from_chars(text.data() + 5, text.data() + 7, month);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + 7) return false;
    parsed = std::from_chars(text.data() + 8, text.data() + 10, day);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + 10) return false;
    const std::chrono::year_month_day ymd{
        std::chrono::year{year} / std::chrono::month{static_cast<unsigned>(month)} / std::chrono::day{static_cast<unsigned>(day)}};
    if (!ymd.ok()) return false;
    out = Date{year, month, day};
    return true;
}

std::string formatDate(Date date) {
    return std::to_string(date.year) + "-" + zero2(date.month) + "-" + zero2(date.day);
}

Date addDays(Date date, int days) noexcept {
    return fromSysDays(toSysDays(date) + std::chrono::days{days});
}

std::vector<Date> inclusiveDateRange(Date from, Date to) {
    std::vector<Date> out;
    const auto begin = toSysDays(from);
    const auto end = toSysDays(to);
    if (end < begin) return out;
    for (auto cur = begin; cur <= end; cur += std::chrono::days{1}) out.push_back(fromSysDays(cur));
    return out;
}

Date lastCompletedUtcDate() {
    const auto today = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
    return fromSysDays(today - std::chrono::days{1});
}

bool parseDecimalE8(std::string_view text, std::int64_t& out) noexcept {
    text = trim(text);
    if (text.empty()) return false;
    std::size_t pos = 0;
    bool negative = false;
    if (text[pos] == '+' || text[pos] == '-') {
        negative = text[pos] == '-';
        ++pos;
    }
    if (negative || pos >= text.size()) return false;

    std::int64_t whole = 0;
    bool anyWhole = false;
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
        anyWhole = true;
        const int digit = text[pos] - '0';
        if (whole > (std::numeric_limits<std::int64_t>::max() - digit) / 10) return false;
        whole = whole * 10 + digit;
        ++pos;
    }
    if (!anyWhole) return false;

    std::int64_t frac = 0;
    int fracDigits = 0;
    if (pos < text.size() && text[pos] == '.') {
        ++pos;
        while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
            if (fracDigits >= 8) return false;
            frac = frac * 10 + (text[pos] - '0');
            ++fracDigits;
            ++pos;
        }
    }
    if (pos != text.size()) return false;
    while (fracDigits < 8) {
        frac *= 10;
        ++fracDigits;
    }
    if (whole > (std::numeric_limits<std::int64_t>::max() - frac) / kScaleE8) return false;
    out = whole * kScaleE8 + frac;
    return true;
}

bool parseAggTradeCsvLine(std::string_view line,
                          const ImportIdentity& identity,
                          std::int64_t sequence,
                          replay::TradeRow& out,
                          std::string& error) {
    out = replay::TradeRow{};
    error.clear();
    const auto fields = splitCsv(line);
    if (fields.size() != 7u) {
        error = "aggTrades CSV row must have 7 fields";
        return false;
    }
    std::uint64_t aggId = 0;
    std::uint64_t firstId = 0;
    std::uint64_t lastId = 0;
    std::int64_t tsMs = 0;
    bool buyerMaker = false;
    if (!parseU64(fields[0], aggId)) {
        error = "failed to parse agg_trade_id";
        return false;
    }
    if (!parseDecimalE8(fields[1], out.priceE8)) {
        error = "failed to parse price";
        return false;
    }
    if (!parseDecimalE8(fields[2], out.qtyE8)) {
        error = "failed to parse quantity";
        return false;
    }
    if (!parseU64(fields[3], firstId)) {
        error = "failed to parse first_trade_id";
        return false;
    }
    if (!parseU64(fields[4], lastId)) {
        error = "failed to parse last_trade_id";
        return false;
    }
    if (!parseI64(fields[5], tsMs)) {
        error = "failed to parse transact_time";
        return false;
    }
    if (!parseBool(fields[6], buyerMaker)) {
        error = "failed to parse is_buyer_maker";
        return false;
    }
    if (!msToNs(tsMs, out.tsNs) || !scaledNotionalE8(out.priceE8, out.qtyE8, out.quoteQtyE8)) {
        error = "aggTrades row is out of supported numeric range";
        return false;
    }
    out.tradeId = aggId;
    out.firstTradeId = firstId;
    out.lastTradeId = lastId;
    out.symbol = identity.symbol;
    out.exchange = identity.exchange;
    out.market = identity.market;
    out.captureSeq = sequence;
    out.ingestSeq = sequence;
    out.isBuyerMaker = buyerMaker ? 1u : 0u;
    out.sideBuy = buyerMaker ? 0u : 1u;
    out.side = static_cast<std::int64_t>(out.sideBuy);
    return true;
}

bool parseBookTickerCsvLine(std::string_view line,
                            const ImportIdentity& identity,
                            std::int64_t sequence,
                            replay::BookTickerRow& out,
                            std::string& error) {
    out = replay::BookTickerRow{};
    error.clear();
    const auto fields = splitCsv(line);
    if (fields.size() != 7u) {
        error = "bookTicker CSV row must have 7 fields";
        return false;
    }
    std::uint64_t updateId = 0;
    std::int64_t eventTimeMs = 0;
    if (!parseU64(fields[0], updateId)) {
        error = "failed to parse update_id";
        return false;
    }
    if (!parseDecimalE8(fields[1], out.bidPriceE8)) {
        error = "failed to parse best_bid_price";
        return false;
    }
    if (!parseDecimalE8(fields[2], out.bidQtyE8)) {
        error = "failed to parse best_bid_qty";
        return false;
    }
    if (!parseDecimalE8(fields[3], out.askPriceE8)) {
        error = "failed to parse best_ask_price";
        return false;
    }
    if (!parseDecimalE8(fields[4], out.askQtyE8)) {
        error = "failed to parse best_ask_qty";
        return false;
    }
    if (!parseI64(fields[6], eventTimeMs)) {
        error = "failed to parse event_time";
        return false;
    }
    (void)updateId;
    if (!msToNs(eventTimeMs, out.tsNs)) {
        error = "bookTicker event_time is out of supported numeric range";
        return false;
    }
    out.symbol = identity.symbol;
    out.exchange = identity.exchange;
    out.market = identity.market;
    out.captureSeq = sequence;
    out.ingestSeq = sequence;
    return true;
}

std::string visionChannelName(VisionChannel channel) {
    return channelFolder(channel);
}

std::string visionDailyKey(VisionChannel channel, std::string_view symbol, Date date) {
    const std::string channelName = visionChannelName(channel);
    const std::string dateText = formatDate(date);
    return "data/futures/um/daily/" + channelName + "/" + std::string{symbol} + "/" +
           std::string{symbol} + "-" + channelName + "-" + dateText + ".zip";
}

std::string visionDailyPrefix(VisionChannel channel, std::string_view symbol) {
    return "data/futures/um/daily/" + visionChannelName(channel) + "/" + std::string{symbol} + "/";
}

std::string visionSymbolPrefix(VisionChannel channel) {
    return "data/futures/um/daily/" + visionChannelName(channel) + "/";
}

std::string visionHttpsUrl(std::string_view key) {
    return std::string{kVisionBaseUrl} + std::string{key};
}

std::string visionListUrl(std::string_view prefix, bool delimiter, int maxKeys, std::string_view startAfter) {
    std::string url = "https://s3-ap-northeast-1.amazonaws.com/data.binance.vision?list-type=2&prefix=";
    url += prefix;
    if (maxKeys > 0) url += "&max-keys=" + std::to_string(maxKeys);
    if (delimiter) url += "&delimiter=/";
    if (!startAfter.empty()) {
        url += "&start-after=";
        url += startAfter;
    }
    return url;
}

bool dateFromVisionKey(std::string_view key, VisionChannel channel, std::string_view symbol, Date& out) noexcept {
    const std::string prefix = visionDailyPrefix(channel, symbol) + std::string{symbol} + "-" + visionChannelName(channel) + "-";
    const std::string suffix = ".zip";
    if (key.size() != prefix.size() + 10u + suffix.size()) return false;
    if (key.substr(0, prefix.size()) != prefix) return false;
    if (key.substr(key.size() - suffix.size()) != suffix) return false;
    return parseDate(key.substr(prefix.size(), 10u), out);
}

std::vector<std::string> parseS3Keys(std::string_view xml) {
    return parseTagValues(xml, "Key");
}

std::vector<std::string> parseS3Prefixes(std::string_view xml) {
    return parseTagValues(xml, "Prefix");
}

bool hasVisionZipAndChecksum(std::string_view xml, std::string_view key) {
    bool hasZip = false;
    bool hasChecksum = false;
    const std::string checksum = std::string{key} + ".CHECKSUM";
    for (const auto& found : parseS3Keys(xml)) {
        if (found == key) hasZip = true;
        else if (found == checksum) hasChecksum = true;
    }
    return hasZip && hasChecksum;
}

std::vector<Date> parseAvailableVisionDates(std::string_view xml, VisionChannel channel, std::string_view symbol) {
    std::vector<Date> zips;
    std::vector<Date> checksums;
    for (const auto& key : parseS3Keys(xml)) {
        Date date{};
        if (dateFromVisionKey(key, channel, symbol, date)) {
            zips.push_back(date);
            continue;
        }
        constexpr std::string_view checksumSuffix{".CHECKSUM"};
        if (key.size() > checksumSuffix.size() &&
            key.substr(key.size() - checksumSuffix.size()) == checksumSuffix &&
            dateFromVisionKey(std::string_view{key}.substr(0, key.size() - checksumSuffix.size()), channel, symbol, date)) {
            checksums.push_back(date);
        }
    }
    std::sort(zips.begin(), zips.end(), dateLess);
    zips.erase(std::unique(zips.begin(), zips.end(), dateEqual), zips.end());
    std::sort(checksums.begin(), checksums.end(), dateLess);
    checksums.erase(std::unique(checksums.begin(), checksums.end(), dateEqual), checksums.end());

    std::vector<Date> out;
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < zips.size() && j < checksums.size()) {
        if (dateEqual(zips[i], checksums[j])) {
            out.push_back(zips[i]);
            ++i;
            ++j;
        } else if (dateLess(zips[i], checksums[j])) {
            ++i;
        } else {
            ++j;
        }
    }
    return out;
}

std::vector<Date> latestCommonDates(std::vector<Date> lhs, std::vector<Date> rhs, Date notAfter, std::size_t limit) {
    std::sort(lhs.begin(), lhs.end(), dateLess);
    lhs.erase(std::unique(lhs.begin(), lhs.end(), dateEqual), lhs.end());
    std::sort(rhs.begin(), rhs.end(), dateLess);
    rhs.erase(std::unique(rhs.begin(), rhs.end(), dateEqual), rhs.end());

    std::vector<Date> common;
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < lhs.size() && j < rhs.size()) {
        if (dateEqual(lhs[i], rhs[j])) {
            if (!dateLess(notAfter, lhs[i])) common.push_back(lhs[i]);
            ++i;
            ++j;
        } else if (dateLess(lhs[i], rhs[j])) {
            ++i;
        } else {
            ++j;
        }
    }
    if (common.size() > limit) {
        common.erase(common.begin(), common.end() - static_cast<std::ptrdiff_t>(limit));
    }
    return common;
}

bool parseChecksumSha256(std::string_view text, std::string& outHex) {
    outHex.clear();
    text = trim(text);
    if (text.size() < 64u) return false;
    const std::string_view hash = text.substr(0, 64u);
    for (char ch : hash) {
        if (!std::isxdigit(static_cast<unsigned char>(ch))) return false;
        outHex.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return true;
}

}  // namespace hftrec::history
