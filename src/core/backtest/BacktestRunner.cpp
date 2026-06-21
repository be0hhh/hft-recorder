#include "core/backtest/BacktestRunner.hpp"
#include "hftrec/backtest.hpp"

#include <chrono>
#include <fstream>
#include <sstream>
#include <string>

#include "core/common/JsonString.hpp"
#include "core/replay/SessionReplay.hpp"

namespace hftrec::backtest {
namespace {

std::string makeRunId() {
    const auto nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return "run-" + std::to_string(nowNs);
}

const char* resultStatusName(Status status) noexcept {
    switch (status) {
        case Status::Ok: return "complete";
        case Status::Cancelled: return "cancelled";
        default: return "error";
    }
}

std::filesystem::path resultPathFor(const BacktestRunRequest& request, const std::string& runId) {
    return request.sessionPath / "backtests" / runId;
}

std::string renderManifestJson(const BacktestRunResult& result) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"type\": \"run.result.v2\",\n";
    out << "  \"request_id\": " << json::quote(result.requestId) << ",\n";
    out << "  \"run_id\": " << json::quote(result.runId) << ",\n";
    out << "  \"status\": " << json::quote(resultStatusName(result.status)) << ",\n";
    out << "  \"strategy\": " << json::quote(result.strategy) << ",\n";
    out << "  \"session_path\": " << json::quote(result.sessionPath.generic_string()) << ",\n";
    out << "  \"streams\": {\n";
    out << "    \"order_lifetimes\": {\"path\": \"order_lifetimes.jsonl\", \"rows\": 0},\n";
    out << "    \"fills\": {\"path\": \"fills.jsonl\", \"rows\": 0},\n";
    out << "    \"equity\": {\"path\": \"equity.jsonl\", \"rows\": 0}\n";
    out << "  },\n";
    out << "  \"summary\": {\n";
    out << "    \"mode\": \"skeleton_session_scan\",\n";
    out << "    \"trades\": " << result.trades << ",\n";
    out << "    \"liquidations\": " << result.liquidations << ",\n";
    out << "    \"booktickers\": " << result.bookTickers << ",\n";
    out << "    \"depths\": " << result.depths << ",\n";
    out << "    \"candles\": " << result.candles << ",\n";
    out << "    \"events\": " << result.events << ",\n";
    out << "    \"buckets\": " << result.buckets << ",\n";
    out << "    \"first_ts_ns\": " << result.firstTsNs << ",\n";
    out << "    \"last_ts_ns\": " << result.lastTsNs << ",\n";
    out << "    \"elapsed_ns\": " << result.elapsedNs << "\n";
    out << "  },\n";
    out << "  \"errors\": ";
    if (result.error.empty()) {
        out << "[]\n";
    } else {
        out << "[{\"message\": " << json::quote(result.error) << "}]\n";
    }
    out << "}\n";
    return out.str();
}

Status writeResult(BacktestRunResult& result) noexcept {
    std::error_code ec;
    std::filesystem::create_directories(result.resultPath, ec);
    if (ec) {
        result.error = "failed to create backtest result directory";
        return Status::IoError;
    }

    static constexpr const char* kStreamFiles[] = {"order_lifetimes.jsonl", "fills.jsonl", "equity.jsonl"};
    for (const char* name : kStreamFiles) {
        std::ofstream stream(result.resultPath / name, std::ios::out | std::ios::trunc);
        if (!stream.is_open() || !stream.good()) {
            result.error = std::string{"failed to write result stream "} + name;
            return Status::IoError;
        }
    }

    std::ofstream out(result.resultPath / "manifest.json", std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        result.error = "failed to open result manifest";
        return Status::IoError;
    }
    out << renderManifestJson(result);
    return out.good() ? Status::Ok : Status::IoError;
}

bool reportProgress(const BacktestRunResult& result,
                    std::uint64_t bucketsDone,
                    std::uint64_t bucketsTotal,
                    std::uint64_t eventsDone,
                    std::uint64_t eventsTotal,
                    std::string stage,
                    BacktestProgressCallback callback,
                    void* userData) noexcept {
    if (callback == nullptr) return true;
    BacktestProgress progress{};
    progress.bucketsDone = bucketsDone;
    progress.bucketsTotal = bucketsTotal;
    progress.eventsDone = eventsDone;
    progress.eventsTotal = eventsTotal;
    progress.stage = std::move(stage);
    if (eventsTotal != 0u) {
        progress.percent = static_cast<std::uint32_t>((eventsDone * 100u) / eventsTotal);
    } else if (bucketsTotal != 0u) {
        progress.percent = static_cast<std::uint32_t>((bucketsDone * 100u) / bucketsTotal);
    } else {
        progress.percent = result.status == Status::Ok ? 100u : 0u;
    }
    if (progress.percent > 100u) progress.percent = 100u;
    return callback(progress, userData);
}

}  // namespace

BacktestRunResult BacktestRunner::run(const BacktestRunRequest& request,
                                      BacktestProgressCallback progressCallback,
                                      void* userData) const noexcept {
    const auto startedAt = std::chrono::steady_clock::now();
    BacktestRunResult result{};
    result.requestId = request.requestId.empty() ? request.runId : request.requestId;
    result.runId = request.runId.empty() ? makeRunId() : request.runId;
    result.strategy = request.strategy;
    result.sessionPath = request.sessionPath;
    result.resultPath = resultPathFor(request, result.runId);

    if (result.requestId.empty()) result.requestId = result.runId;
    if (request.sessionPath.empty()) {
        result.status = Status::InvalidArgument;
        result.error = "session_path is empty";
        return result;
    }
    if (request.strategy.empty()) {
        result.status = Status::InvalidArgument;
        result.error = "strategy is empty";
        (void)writeResult(result);
        return result;
    }

    (void)reportProgress(result, 0u, 0u, 0u, 0u, "loading session", progressCallback, userData);

    replay::SessionReplay replay;
    const Status openStatus = replay.open(request.sessionPath);
    if (!isOk(openStatus)) {
        result.status = openStatus;
        result.error = replay.errorDetail().empty()
            ? "failed to open session replay"
            : std::string{replay.errorDetail()};
        (void)writeResult(result);
        return result;
    }

    result.trades = replay.trades().size();
    result.liquidations = replay.liquidations().size();
    result.bookTickers = replay.bookTickers().size();
    result.depths = replay.depths().size();
    result.candles = replay.candles().size() + replay.candles2().size();
    result.events = replay.events().size();
    result.buckets = replay.buckets().size();
    result.firstTsNs = replay.firstTsNs();
    result.lastTsNs = replay.lastTsNs();

    const std::uint64_t bucketsTotal = result.buckets;
    const std::uint64_t eventsTotal = result.events;
    std::uint64_t eventsDone = 0u;
    if (!reportProgress(result, 0u, bucketsTotal, 0u, eventsTotal, "scanning events", progressCallback, userData)) {
        result.status = Status::Cancelled;
        result.error = "cancelled";
        (void)writeResult(result);
        return result;
    }

    for (std::uint64_t i = 0u; i < replay.buckets().size(); ++i) {
        eventsDone += replay.buckets()[static_cast<std::size_t>(i)].items.size();
        if ((i % 64u) == 0u || i + 1u == bucketsTotal) {
            if (!reportProgress(result, i + 1u, bucketsTotal, eventsDone, eventsTotal, "scanning events", progressCallback, userData)) {
                result.status = Status::Cancelled;
                result.error = "cancelled";
                (void)writeResult(result);
                return result;
            }
        }
    }

    result.status = Status::Ok;
    result.elapsedNs = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - startedAt).count());
    if (!reportProgress(result, bucketsTotal, bucketsTotal, eventsTotal, eventsTotal, "writing result", progressCallback, userData)) {
        result.status = Status::Cancelled;
        result.error = "cancelled";
    }
    const Status writeStatus = writeResult(result);
    if (!isOk(writeStatus)) {
        result.status = writeStatus;
    }
    (void)reportProgress(result, bucketsTotal, bucketsTotal, eventsTotal, eventsTotal, "complete", progressCallback, userData);
    return result;
}

}  // namespace hftrec::backtest

namespace hftrec {

BacktestRunResult runBacktest(const BacktestRunRequest& request,
                                BacktestProgressCallback progressCallback,
                                void* userData) noexcept {
    backtest::BacktestRunner runner;
    return runner.run(request, progressCallback, userData);
}

}  // namespace hftrec
