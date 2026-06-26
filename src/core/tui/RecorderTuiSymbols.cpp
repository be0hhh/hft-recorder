#include "core/tui/RecorderTuiSymbols.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace hftrec::tui {

namespace {

struct ParsedSymbol {
    std::string base;
    std::string quote;
};

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
    for (char ch : text) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    return out;
}

std::string upper(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char ch : text) out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    return out;
}

bool endsWithNoCase(std::string_view text, std::string_view suffix) noexcept {
    if (text.size() < suffix.size()) return false;
    const std::string tail = lower(text.substr(text.size() - suffix.size()));
    return tail == lower(suffix);
}

std::vector<std::string> splitSymbolTokens(std::string_view text) {
    std::vector<std::string> out;
    std::string current;
    for (char ch : text) {
        if (ch == ',' || std::isspace(static_cast<unsigned char>(ch))) {
            if (!current.empty()) {
                out.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

void appendUniqueSymbol(std::vector<std::string>& symbols, std::string_view raw) {
    const std::string symbol = trim(raw);
    if (symbol.empty()) return;
    const std::string key = lower(symbol);
    const auto exists = std::any_of(symbols.begin(), symbols.end(), [&](const std::string& existing) {
        return lower(existing) == key;
    });
    if (!exists) symbols.push_back(symbol);
}

ParsedSymbol parseGlobalSymbol(std::string_view raw) {
    std::string symbol = upper(trim(raw));
    constexpr std::string_view swapMarker = "-SWAP-";
    const auto swapMarkerIndex = symbol.find(swapMarker);
    if (swapMarkerIndex != std::string::npos) {
        const std::string base = symbol.substr(0u, swapMarkerIndex);
        const std::string quote = symbol.substr(swapMarkerIndex + swapMarker.size());
        if (!base.empty() && !quote.empty()) {
            return ParsedSymbol{base, quote};
        }
    }
    constexpr std::string_view swapSuffix = "-SWAP";
    if (symbol.size() > swapSuffix.size() + 1u && endsWithNoCase(symbol, swapSuffix)) {
        const auto sepPos = symbol.rfind('-', symbol.size() - swapSuffix.size() - 1u);
        if (sepPos != std::string::npos && sepPos > 0u &&
            sepPos < (symbol.size() - swapSuffix.size())) {
            const std::string base = symbol.substr(0u, sepPos);
            const std::string quote = symbol.substr(sepPos + 1u, symbol.size() - sepPos - swapSuffix.size() - 1u);
            if (!base.empty() && !quote.empty()) {
                return ParsedSymbol{base, quote};
            }
        }
    }
    symbol.erase(std::remove(symbol.begin(), symbol.end(), '-'), symbol.end());
    symbol.erase(std::remove(symbol.begin(), symbol.end(), '_'), symbol.end());
    if (symbol.size() > 4u && symbol.ends_with("SWAP")) symbol.resize(symbol.size() - 4u);

    constexpr std::string_view quotes[] = {"USDT", "USDC", "USD"};
    for (std::string_view quote : quotes) {
        if (symbol.size() > quote.size() && symbol.ends_with(quote)) {
            symbol.resize(symbol.size() - quote.size());
            return ParsedSymbol{symbol, std::string{quote}};
        }
    }
    return ParsedSymbol{symbol, "USDT"};
}

std::string formattedVenueSymbol(const RecorderTuiVenueSpec& venue, const ParsedSymbol& symbol) {
    const std::string exchange = venue.exchange;
    const std::string market = venue.market;
    std::string base = symbol.base;
    std::string quote = symbol.quote;

    if (exchange == "kucoin" && market == "futures" && base == "BTC") base = "XBT";
    if (market == "inverse") quote = "USD";
    if (exchange == "bitget" && market == "swap") quote = "USDC";

    if (exchange == "kucoin") {
        if (market == "futures") return base + quote + "M";
        return base + '-' + quote;
    }
    if (exchange == "xt") {
        return lower(base) + '_' + lower(quote);
    }
    if (exchange == "bingx") {
        return base + '-' + quote;
    }
    if (exchange == "toobit") {
        if (market == "futures" || market == "swap") return base + "-SWAP-" + quote;
        return base + quote;
    }
    if (exchange == "htx") {
        if (market == "futures" || market == "swap") return base + '-' + quote;
        return lower(base) + lower(quote);
    }
    if (exchange == "phemex") {
        if (market == "futures" || market == "swap") return base + quote;
        return std::string{"s"} + base + quote;
    }
    if (exchange == "gate") return base + '_' + quote;
    if (exchange == "okx") {
        const std::string result = base + '-' + quote;
        return market == "futures" ? result + "-SWAP" : result;
    }
    if (exchange == "mexc" && market == "futures") return base + '_' + quote;
    if (exchange == "binance" && market == "inverse") return base + quote + "_PERP";
    return base + quote;
}

std::filesystem::path resolveSymbolListPath(std::string_view token, const std::filesystem::path& listDir) {
    std::filesystem::path path{trim(token)};
    const std::string value = path.string();
    const bool explicitPath = !value.empty() && (value.front() == '.' || value.find('/') != std::string::npos ||
                                                 value.find('\\') != std::string::npos);
    if (path.is_absolute() || explicitPath) return path;
    const std::filesystem::path root = listDir.empty() ? symbolListConfigDir() : listDir;
    return root / path;
}

bool appendSymbolsFromListFile(std::string_view token,
                               const std::filesystem::path& listDir,
                               SymbolBatchInput& out,
                               std::string& error) {
    if (!endsWithNoCase(trim(token), ".ini")) {
        error = "symbol list file must use .ini extension";
        return false;
    }

    const std::filesystem::path path = resolveSymbolListPath(token, listDir);
    std::ifstream file(path);
    if (!file) {
        error = "failed to open symbol list: " + path.string();
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        const std::size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        for (const std::string& tokenInFile : splitSymbolTokens(line)) {
            appendUniqueSymbol(out.symbols, tokenInFile);
        }
    }
    out.loadedFiles.push_back(path);
    return true;
}

std::string symbolSlug(std::string_view raw) {
    std::string out;
    for (char ch : raw) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch)) out.push_back(static_cast<char>(std::tolower(uch)));
    }
    return out.empty() ? std::string{"symbol"} : out;
}

}  // namespace

const std::vector<RecorderTuiVenueSpec>& allCryptoVenueSpecs() {
    static const std::vector<RecorderTuiVenueSpec> venues{
        {"binance_futures", "Binance Futures", "binance", "futures"},
        {"binance_spot", "Binance Spot", "binance", "spot"},
        {"bybit_futures", "Bybit Futures", "bybit", "futures"},
        {"bybit_spot", "Bybit Spot", "bybit", "spot"},
        {"kucoin_futures", "KuCoin Futures", "kucoin", "futures"},
        {"kucoin_spot", "KuCoin Spot", "kucoin", "spot"},
        {"gate_futures", "Gate Futures", "gate", "futures"},
        {"gate_spot", "Gate Spot", "gate", "spot"},
        {"bitget_futures", "Bitget Futures", "bitget", "futures"},
        {"bitget_spot", "Bitget Spot", "bitget", "spot"},
        {"aster_futures", "Aster Futures", "aster", "futures"},
        {"aster_spot", "Aster Spot", "aster", "spot"},
        {"okx_futures", "OKX Futures", "okx", "futures"},
        {"okx_spot", "OKX Spot", "okx", "spot"},
        {"mexc_spot", "MEXC Spot", "mexc", "spot"},
        {"mexc_futures", "MEXC Futures", "mexc", "futures"},
        {"xt_futures", "XT Futures", "xt", "futures"},
        {"xt_spot", "XT Spot", "xt", "spot"},
        {"bingx_futures", "BingX Futures", "bingx", "futures"},
        {"bingx_spot", "BingX Spot", "bingx", "spot"},
        {"toobit_futures", "Toobit Futures", "toobit", "futures"},
        {"toobit_spot", "Toobit Spot", "toobit", "spot"},
        {"htx_futures", "HTX Futures", "htx", "futures"},
        {"htx_spot", "HTX Spot", "htx", "spot"},
        {"phemex_futures", "Phemex Futures", "phemex", "futures"},
        {"phemex_spot", "Phemex Spot", "phemex", "spot"},
    };
    return venues;
}

const RecorderTuiVenueSpec* venueSpecByKey(std::string_view key) noexcept {
    const std::string normalized = lower(trim(key));
    for (const auto& venue : allCryptoVenueSpecs()) {
        if (normalized == venue.key) return &venue;
    }
    return nullptr;
}

std::filesystem::path symbolListConfigDir() {
    return std::filesystem::path{"configs"} / "symbols";
}

std::string venueSymbolsFromGlobalInput(std::string_view venueKey, std::string_view symbolsText) {
    const auto* venue = venueSpecByKey(venueKey);
    if (venue == nullptr) return {};

    std::vector<std::string> formatted;
    for (const std::string& token : splitSymbolTokens(symbolsText)) {
        const ParsedSymbol parsed = parseGlobalSymbol(token);
        if (!parsed.base.empty()) formatted.push_back(formattedVenueSymbol(*venue, parsed));
    }

    std::string out;
    for (std::size_t i = 0; i < formatted.size(); ++i) {
        if (i != 0u) out.push_back('\n');
        out += formatted[i];
    }
    return out;
}

std::string renderSymbolListText(const std::vector<std::string>& symbols) {
    std::string out;
    for (const std::string& symbol : symbols) {
        const std::string value = trim(symbol);
        if (value.empty()) continue;
        out += value;
        out.push_back('\n');
    }
    return out;
}

bool loadSymbolBatchInput(std::string_view text,
                          const std::filesystem::path& listDir,
                          SymbolBatchInput& out,
                          std::string& error) {
    error.clear();
    out = {};

    const std::string value = trim(text);
    if (value.empty()) {
        error = "enter a symbol or .ini symbol list";
        return false;
    }

    const std::string valueLower = lower(value);
    if (valueLower.starts_with("l:")) {
        const std::string listName = trim(std::string_view{value.data(), value.size()}.substr(2u));
        if (listName.empty()) {
            error = "symbol list name is required after l:";
            return false;
        }
        if (!appendSymbolsFromListFile(listName, listDir, out, error)) return false;
    } else {
        for (const std::string& token : splitSymbolTokens(value)) {
            if (endsWithNoCase(token, ".ini")) {
                if (!appendSymbolsFromListFile(token, listDir, out, error)) return false;
            } else {
                appendUniqueSymbol(out.symbols, token);
            }
        }
    }

    if (out.symbols.empty()) {
        error = "symbol batch is empty";
        return false;
    }
    return true;
}

std::vector<RecorderTuiJob> generateJobsForSymbols(const std::vector<std::string>& symbols,
                                                   const std::vector<RecorderTuiVenueSpec>& venues,
                                                   std::size_t startIndex) {
    std::vector<RecorderTuiJob> jobs;
    jobs.reserve(symbols.size() * venues.size());
    std::size_t ordinal = startIndex;
    for (const std::string& symbol : symbols) {
        for (const auto& venue : venues) {
            RecorderTuiJob job{};
            job.name = symbolSlug(symbol) + '_' + venue.exchange + '_' + venue.market;
            if (ordinal != 0u) job.name += '_' + std::to_string(ordinal + 1u);
            job.exchange = venue.exchange;
            job.market = venue.market;
            job.symbol = venueSymbolsFromGlobalInput(venue.key, symbol);
            job.durationMin = 0;
            job.channels = allLiveChannels();
            jobs.push_back(std::move(job));
            ++ordinal;
        }
    }
    return jobs;
}

}  // namespace hftrec::tui
