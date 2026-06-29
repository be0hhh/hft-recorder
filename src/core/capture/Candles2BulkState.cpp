#include "core/capture/Candles2BulkState.hpp"

#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>
#include <utility>

#include "core/common/JsonString.hpp"
#include "core/common/MiniJsonParser.hpp"

namespace hftrec::capture {

namespace {

void appendStringField(std::ostringstream& out, const char* name, std::string_view value, bool comma = true) {
    out << "  \"" << name << "\": " << json::quote(value);
    out << (comma ? ",\n" : "\n");
}

template <typename Value>
void appendNumberField(std::ostringstream& out, const char* name, Value value, bool comma = true) {
    out << "  \"" << name << "\": " << value;
    out << (comma ? ",\n" : "\n");
}

bool parseUInt32(hftrec::json::MiniJsonParser& parser, std::uint32_t& out) noexcept {
    std::uint64_t value = 0;
    if (!parser.parseUInt64(value) ||
        value > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
        return false;
    }
    out = static_cast<std::uint32_t>(value);
    return true;
}

bool parseStateObject(hftrec::json::MiniJsonParser& parser, Candles2BulkState& out) noexcept {
    if (!parser.parseObjectStart()) return false;
    if (parser.peek('}')) return parser.parseObjectEnd();

    do {
        std::string key;
        if (!parser.parseKey(key)) return false;

        if (key == "schema") {
            std::string ignored;
            if (!parser.parseString(ignored)) return false;
        } else if (key == "status") {
            if (!parser.parseString(out.status)) return false;
        } else if (key == "exchange") {
            if (!parser.parseString(out.exchange)) return false;
        } else if (key == "market") {
            if (!parser.parseString(out.market)) return false;
        } else if (key == "symbol") {
            if (!parser.parseString(out.symbol)) return false;
        } else if (key == "timeframe") {
            if (!parser.parseString(out.timeframe)) return false;
        } else if (key == "candles2_path") {
            if (!parser.parseString(out.candles2Path)) return false;
        } else if (key == "compatibility_candles_path") {
            if (!parser.parseString(out.compatibilityCandlesPath)) return false;
        } else if (key == "requested_limit") {
            if (!parseUInt32(parser, out.requestedLimit)) return false;
        } else if (key == "page_limit") {
            if (!parseUInt32(parser, out.pageLimit)) return false;
        } else if (key == "requested_end_ns") {
            if (!parser.parseInt64(out.requestedEndNs)) return false;
        } else if (key == "cursor_end_ns") {
            if (!parser.parseUInt64(out.cursorEndNs)) return false;
        } else if (key == "oldest_ts_ns") {
            if (!parser.parseUInt64(out.oldestTsNs)) return false;
        } else if (key == "newest_ts_ns") {
            if (!parser.parseUInt64(out.newestTsNs)) return false;
        } else if (key == "rows_written") {
            if (!parser.parseUInt64(out.rowsWritten)) return false;
        } else if (key == "pages_ok") {
            if (!parser.parseUInt64(out.pagesOk)) return false;
        } else if (key == "rows_raw") {
            if (!parser.parseUInt64(out.rowsRaw)) return false;
        } else if (key == "empty_windows_skipped") {
            if (!parser.parseUInt64(out.emptyWindowsSkipped)) return false;
        } else if (key == "callback_stops") {
            if (!parser.parseUInt64(out.callbackStops)) return false;
        } else if (key == "last_error") {
            if (!parser.parseString(out.lastError)) return false;
        } else if (!parser.skipValue()) {
            return false;
        }

        if (parser.peek('}')) break;
    } while (parser.parseComma());

    return parser.parseObjectEnd();
}

}  // namespace

std::string renderCandles2BulkStateJson(const Candles2BulkState& state) {
    std::ostringstream out;
    out << "{\n";
    appendStringField(out, "schema", "hftrec.candles2_bulk_state.v1");
    appendStringField(out, "status", state.status);
    appendStringField(out, "exchange", state.exchange);
    appendStringField(out, "market", state.market);
    appendStringField(out, "symbol", state.symbol);
    appendStringField(out, "timeframe", state.timeframe);
    appendStringField(out, "candles2_path", state.candles2Path);
    appendStringField(out, "compatibility_candles_path", state.compatibilityCandlesPath);
    appendNumberField(out, "requested_limit", state.requestedLimit);
    appendNumberField(out, "page_limit", state.pageLimit);
    appendNumberField(out, "requested_end_ns", state.requestedEndNs);
    appendNumberField(out, "cursor_end_ns", state.cursorEndNs);
    appendNumberField(out, "oldest_ts_ns", state.oldestTsNs);
    appendNumberField(out, "newest_ts_ns", state.newestTsNs);
    appendNumberField(out, "rows_written", state.rowsWritten);
    appendNumberField(out, "pages_ok", state.pagesOk);
    appendNumberField(out, "rows_raw", state.rowsRaw);
    appendNumberField(out, "empty_windows_skipped", state.emptyWindowsSkipped);
    appendNumberField(out, "callback_stops", state.callbackStops);
    appendStringField(out, "last_error", state.lastError, false);
    out << "}\n";
    return out.str();
}

Status parseCandles2BulkStateJson(std::string_view document, Candles2BulkState& out) noexcept {
    if (document.empty()) return Status::CorruptData;
    hftrec::json::MiniJsonParser parser{document};
    Candles2BulkState parsed{};
    if (!parseStateObject(parser, parsed) || !parser.finish()) return Status::CorruptData;
    out = std::move(parsed);
    return Status::Ok;
}

Status writeCandles2BulkStateFile(const std::filesystem::path& sessionDir,
                                  const Candles2BulkState& state,
                                  std::string* error) noexcept {
    if (sessionDir.empty()) {
        if (error != nullptr) *error = "empty session dir";
        return Status::InvalidArgument;
    }
    const std::filesystem::path path = sessionDir / kCandles2BulkStateRelativePath;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        if (error != nullptr) *error = "create candles2 bulk reports dir failed: " + ec.message();
        return Status::IoError;
    }

    std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        if (error != nullptr) *error = "open candles2 bulk state failed";
        return Status::IoError;
    }
    out << renderCandles2BulkStateJson(state);
    out.flush();
    if (!out.good()) {
        if (error != nullptr) *error = "write candles2 bulk state failed";
        return Status::IoError;
    }
    return Status::Ok;
}

}  // namespace hftrec::capture
