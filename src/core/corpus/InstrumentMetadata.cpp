#include "core/corpus/InstrumentMetadata.hpp"

#include <array>
#include <sstream>

#include "core/common/JsonString.hpp"
#include "core/common/MiniJsonParser.hpp"

namespace hftrec::corpus {

namespace {

std::optional<std::string> inferQuoteAsset(std::string_view symbol) noexcept {
    constexpr std::array<std::string_view, 4> kQuotes{
        "USDT", "USDC", "BUSD", "USD"
    };
    for (const auto quote : kQuotes) {
        if (symbol.size() <= quote.size()) continue;
        if (symbol.substr(symbol.size() - quote.size()) == quote) {
            return std::string{quote};
        }
    }
    return std::nullopt;
}

std::optional<std::string> inferBaseAsset(std::string_view symbol,
                                          std::string_view quoteAsset) noexcept {
    if (symbol.size() <= quoteAsset.size()) return std::nullopt;
    return std::string{symbol.substr(0, symbol.size() - quoteAsset.size())};
}

std::string inferInstrumentType(std::string_view market) {
    if (market == "futures_usd") return "perpetual_linear_future";
    if (market == "spot") return "spot";
    return "unknown";
}

void appendOptionalString(std::ostringstream& out,
                          std::string_view key,
                          const std::optional<std::string>& value,
                          bool trailingComma = true) {
    out << "  \"" << key << "\": ";
    if (value.has_value()) out << json::quote(*value);
    else out << "null";
    if (trailingComma) out << ',';
    out << '\n';
}

void appendOptionalI64(std::ostringstream& out,
                       std::string_view key,
                       const std::optional<std::int64_t>& value,
                       bool trailingComma = true) {
    out << "  \"" << key << "\": ";
    if (value.has_value()) out << *value;
    else out << "null";
    if (trailingComma) out << ',';
    out << '\n';
}

bool parseOptionalString(json::MiniJsonParser& parser, std::optional<std::string>& out) noexcept {
    if (parser.peek('n')) {
        if (!parser.skipValue()) return false;
        out.reset();
        return true;
    }
    std::string value;
    if (!parser.parseString(value)) return false;
    out = std::move(value);
    return true;
}

bool parseOptionalI64(json::MiniJsonParser& parser, std::optional<std::int64_t>& out) noexcept {
    if (parser.peek('n')) {
        if (!parser.skipValue()) return false;
        out.reset();
        return true;
    }
    std::int64_t value = 0;
    if (!parser.parseInt64(value)) return false;
    out = value;
    return true;
}

}  // namespace

InstrumentMetadata makeInstrumentMetadata(std::string_view exchange,
                                          std::string_view market,
                                          std::string_view symbol) noexcept {
    InstrumentMetadata metadata{};
    metadata.exchange = std::string{exchange};
    metadata.exchangeSource = "capture_config";
    metadata.market = std::string{market};
    metadata.marketSource = "capture_config";
    metadata.symbol = std::string{symbol};
    metadata.symbolSource = "capture_config";
    metadata.instrumentType = inferInstrumentType(market);
    metadata.instrumentTypeSource = "recorder_inference";
    metadata.priceScaleDigits = 8;
    metadata.priceScaleDigitsSource = "recorder_default";
    metadata.qtyScaleDigits = 8;
    metadata.qtyScaleDigitsSource = "recorder_default";

    const auto quote = inferQuoteAsset(symbol);
    const auto base = quote.has_value() ? inferBaseAsset(symbol, *quote) : std::nullopt;
    metadata.quoteAsset = quote;
    metadata.quoteAssetSource = quote.has_value() ? "symbol_inference" : "unknown";
    metadata.baseAsset = base;
    metadata.baseAssetSource = base.has_value() ? "symbol_inference" : "unknown";
    if (market == "futures_usd" && quote.has_value()) {
        metadata.settlementAsset = quote;
        metadata.settlementAssetSource = "market_inference";
    }
    return metadata;
}

std::string renderInstrumentMetadataJson(const InstrumentMetadata& metadata) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"schema_version\": " << json::quote(metadata.schemaVersion) << ",\n";
    out << "  \"exchange\": " << json::quote(metadata.exchange) << ",\n";
    out << "  \"exchange_source\": " << json::quote(metadata.exchangeSource) << ",\n";
    out << "  \"market\": " << json::quote(metadata.market) << ",\n";
    out << "  \"market_source\": " << json::quote(metadata.marketSource) << ",\n";
    out << "  \"symbol\": " << json::quote(metadata.symbol) << ",\n";
    out << "  \"symbol_source\": " << json::quote(metadata.symbolSource) << ",\n";
    out << "  \"instrument_type\": " << json::quote(metadata.instrumentType) << ",\n";
    out << "  \"instrument_type_source\": " << json::quote(metadata.instrumentTypeSource) << ",\n";
    appendOptionalString(out, "base_asset", metadata.baseAsset);
    out << "  \"base_asset_source\": " << json::quote(metadata.baseAssetSource) << ",\n";
    appendOptionalString(out, "quote_asset", metadata.quoteAsset);
    out << "  \"quote_asset_source\": " << json::quote(metadata.quoteAssetSource) << ",\n";
    appendOptionalString(out, "settlement_asset", metadata.settlementAsset);
    out << "  \"settlement_asset_source\": " << json::quote(metadata.settlementAssetSource) << ",\n";
    appendOptionalI64(out, "price_scale_digits", metadata.priceScaleDigits);
    out << "  \"price_scale_digits_source\": " << json::quote(metadata.priceScaleDigitsSource) << ",\n";
    appendOptionalI64(out, "qty_scale_digits", metadata.qtyScaleDigits);
    out << "  \"qty_scale_digits_source\": " << json::quote(metadata.qtyScaleDigitsSource) << ",\n";
    appendOptionalI64(out, "tick_size_e8", metadata.tickSizeE8);
    out << "  \"tick_size_source\": " << json::quote(metadata.tickSizeSource) << ",\n";
    appendOptionalI64(out, "lot_size_e8", metadata.lotSizeE8);
    out << "  \"lot_size_source\": " << json::quote(metadata.lotSizeSource) << ",\n";
    appendOptionalString(out, "instrument_status", metadata.instrumentStatus);
    out << "  \"instrument_status_source\": " << json::quote(metadata.instrumentStatusSource) << '\n';
    out << "}\n";
    return out.str();
}

Status parseInstrumentMetadataJson(std::string_view document, InstrumentMetadata& out) noexcept {
    json::MiniJsonParser parser{document};
    InstrumentMetadata parsed{};
    if (!parser.parseObjectStart()) return Status::CorruptData;
    if (!parser.peek('}')) {
        std::string key;
        do {
            if (!parser.parseKey(key)) return Status::CorruptData;
            if (key == "schema_version") {
                if (!parser.parseString(parsed.schemaVersion)) return Status::CorruptData;
            } else if (key == "exchange") {
                if (!parser.parseString(parsed.exchange)) return Status::CorruptData;
            } else if (key == "exchange_source") {
                if (!parser.parseString(parsed.exchangeSource)) return Status::CorruptData;
            } else if (key == "market") {
                if (!parser.parseString(parsed.market)) return Status::CorruptData;
            } else if (key == "market_source") {
                if (!parser.parseString(parsed.marketSource)) return Status::CorruptData;
            } else if (key == "symbol") {
                if (!parser.parseString(parsed.symbol)) return Status::CorruptData;
            } else if (key == "symbol_source") {
                if (!parser.parseString(parsed.symbolSource)) return Status::CorruptData;
            } else if (key == "instrument_type") {
                if (!parser.parseString(parsed.instrumentType)) return Status::CorruptData;
            } else if (key == "instrument_type_source") {
                if (!parser.parseString(parsed.instrumentTypeSource)) return Status::CorruptData;
            } else if (key == "base_asset") {
                if (!parseOptionalString(parser, parsed.baseAsset)) return Status::CorruptData;
            } else if (key == "base_asset_source") {
                if (!parser.parseString(parsed.baseAssetSource)) return Status::CorruptData;
            } else if (key == "quote_asset") {
                if (!parseOptionalString(parser, parsed.quoteAsset)) return Status::CorruptData;
            } else if (key == "quote_asset_source") {
                if (!parser.parseString(parsed.quoteAssetSource)) return Status::CorruptData;
            } else if (key == "settlement_asset") {
                if (!parseOptionalString(parser, parsed.settlementAsset)) return Status::CorruptData;
            } else if (key == "settlement_asset_source") {
                if (!parser.parseString(parsed.settlementAssetSource)) return Status::CorruptData;
            } else if (key == "price_scale_digits") {
                if (!parseOptionalI64(parser, parsed.priceScaleDigits)) return Status::CorruptData;
            } else if (key == "price_scale_digits_source") {
                if (!parser.parseString(parsed.priceScaleDigitsSource)) return Status::CorruptData;
            } else if (key == "qty_scale_digits") {
                if (!parseOptionalI64(parser, parsed.qtyScaleDigits)) return Status::CorruptData;
            } else if (key == "qty_scale_digits_source") {
                if (!parser.parseString(parsed.qtyScaleDigitsSource)) return Status::CorruptData;
            } else if (key == "tick_size_e8") {
                if (!parseOptionalI64(parser, parsed.tickSizeE8)) return Status::CorruptData;
            } else if (key == "tick_size_source") {
                if (!parser.parseString(parsed.tickSizeSource)) return Status::CorruptData;
            } else if (key == "lot_size_e8") {
                if (!parseOptionalI64(parser, parsed.lotSizeE8)) return Status::CorruptData;
            } else if (key == "lot_size_source") {
                if (!parser.parseString(parsed.lotSizeSource)) return Status::CorruptData;
            } else if (key == "instrument_status") {
                if (!parseOptionalString(parser, parsed.instrumentStatus)) return Status::CorruptData;
            } else if (key == "instrument_status_source") {
                if (!parser.parseString(parsed.instrumentStatusSource)) return Status::CorruptData;
            } else {
                if (!parser.skipValue()) return Status::CorruptData;
            }
            if (parser.peek('}')) break;
        } while (parser.parseComma());
    }
    if (!parser.parseObjectEnd() || !parser.finish()) return Status::CorruptData;
    out = std::move(parsed);
    return Status::Ok;
}

}  // namespace hftrec::corpus
