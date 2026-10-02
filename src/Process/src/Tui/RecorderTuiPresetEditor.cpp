#include "RecorderTuiInternal.hpp"

namespace hftrec::app::tui_detail {

std::string jobLabel(const tui::RecorderTuiJob& job) {
    const std::string route = tui::routeSymbolForJob(job);
    const std::string routeNote = route != job.symbol ? " -> " + route : std::string{};
    return job.name + " | " + job.exchange + "/" + job.market + " " + job.symbol + routeNote +
           " | " + (job.durationMin == 0 ? "until stop" : std::to_string(job.durationMin) + "m") +
           " | " + tui::renderChannelSelection(job.channels);
}

bool sameCaptureJob(const tui::RecorderTuiJob& lhs, const tui::RecorderTuiJob& rhs) {
    return lower(lhs.exchange) == lower(rhs.exchange)
        && lower(lhs.market) == lower(rhs.market)
        && lower(lhs.symbol) == lower(rhs.symbol)
        && lower(tui::routeSymbolForJob(lhs)) == lower(tui::routeSymbolForJob(rhs));
}

bool containsCaptureJob(const std::vector<tui::RecorderTuiJob>& jobs, const tui::RecorderTuiJob& candidate) {
    return std::any_of(jobs.begin(), jobs.end(), [&](const tui::RecorderTuiJob& existing) {
        return sameCaptureJob(existing, candidate);
    });
}

void addDefaultJob(tui::RecorderTuiPreset& preset) {
    tui::RecorderTuiJob job{};
    job.name = "job" + std::to_string(preset.jobs.size() + 1u);
    job.channels = tui::allLiveChannels();
    preset.jobs.push_back(std::move(job));
}

void duplicateJob(tui::RecorderTuiPreset& preset, std::size_t index) {
    if (index >= preset.jobs.size()) return;
    auto copy = preset.jobs[index];
    copy.name += "_copy";
    preset.jobs.insert(preset.jobs.begin() + static_cast<std::ptrdiff_t>(index + 1u), std::move(copy));
}

GeneratedJobsAppendResult appendGeneratedSymbolJobs(tui::RecorderTuiPreset& preset, std::string_view input) {
    GeneratedJobsAppendResult result{};
    tui::SymbolBatchInput batch{};
    std::string error;
    if (!tui::loadSymbolBatchInput(input, tui::symbolListConfigDir(), batch, error)) {
        result.error = error;
        return result;
    }

    result.symbols = batch.symbols.size();
    result.loadedFiles = std::move(batch.loadedFiles);
    std::vector<std::string> uniqueSymbols;
    uniqueSymbols.reserve(preset.jobs.size() + batch.symbols.size());
    for (const auto& job : preset.jobs) {
        if (std::find(uniqueSymbols.begin(), uniqueSymbols.end(), job.symbol) == uniqueSymbols.end()) {
            uniqueSymbols.push_back(job.symbol);
        }
    }
    for (const auto& symbol : batch.symbols) {
        if (std::find(uniqueSymbols.begin(), uniqueSymbols.end(), symbol) == uniqueSymbols.end()) {
            uniqueSymbols.push_back(symbol);
        }
    }
    if (uniqueSymbols.size() > 20u) {
        result.error = "venue multiplex supports at most 20 unique symbols";
        return result;
    }
    const auto generated = tui::generateJobsForSymbols(batch.symbols, tui::allCryptoVenueSpecs(), preset.jobs.size());
    preset.executionMode = tui::RecorderTuiExecutionMode::VenueMultiplex;
    preset.memoryLimitMiB = 18 * 1024;
    for (const auto& job : generated) {
        if (containsCaptureJob(preset.jobs, job)) {
            ++result.skipped;
            continue;
        }
        preset.jobs.push_back(job);
        ++result.added;
    }
    return result;
}

std::string generatedJobsMessage(const GeneratedJobsAppendResult& result, std::string_view action) {
    if (!result.error.empty()) return result.error;
    std::ostringstream out;
    out << action << ' ' << result.added << " job(s) from " << result.symbols << " symbol(s)";
    if (result.skipped != 0u) out << ", skipped " << result.skipped << " duplicate(s)";
    if (!result.loadedFiles.empty()) out << ", loaded " << result.loadedFiles.front().string();
    return out.str();
}

void toggleChannelByIndex(tui::ChannelSelection& channels,int index) {
    switch (index) {
        case 0:channels.trades=!channels.trades;break;
        case 1:channels.bookTicker=!channels.bookTicker;break;
        case 2:channels.orderbook=!channels.orderbook;break;
        default:break;
    }
    if (!tui::anyChannelSelected(channels)) channels.trades=true;
}

bool channelByIndex(const tui::ChannelSelection& channels,int index) {
    switch (index) {
        case 0:return channels.trades;
        case 1:return channels.bookTicker;
        case 2:return channels.orderbook;
        default:return false;
    }
}

const char* channelNameByIndex(int index) {
    switch (index) {
        case 0:return "trades";
        case 1:return "bookticker";
        case 2:return "orderbook";
        default:return "";
    }
}

void editJob(TerminalGuard& terminal, tui::RecorderTuiJob& job) {
    int row = 0;
    std::string message;
    while (!gInterrupted) {
        renderEditJob(job, row, message);
        message.clear();
        const Key key = readKey(250);
        if (key.kind == KeyKind::Up) row = std::max(0, row - 1);
        else if (key.kind == KeyKind::Down) row = std::min(12, row + 1);
        else if (key.kind == KeyKind::Escape) return;
        else if (key.kind == KeyKind::Enter || (key.kind == KeyKind::Character && key.ch == ' ')) {
            if (row == 0) {
                const std::string nextSymbol = promptLine(terminal, "symbol", job.symbol);
                if (lower(nextSymbol) != lower(job.symbol)) job.routeSymbol.clear();
                job.symbol = nextSymbol;
            }
            else if (row == 1) job.name = promptLine(terminal, "name", job.name);
            else if (row == 2) job.exchange = lower(promptLine(terminal, "exchange", job.exchange));
            else if (row == 3) job.market = lower(promptLine(terminal, "market", job.market));
            else if (row == 4) {
                const std::string value = promptLine(terminal, "duration minutes (0/none = until stop)",
                                                     job.durationMin == 0 ? "0" : std::to_string(job.durationMin));
                std::string error;
                std::int64_t minutes = 0;
                if (tui::parseDurationMinutes(value, minutes, error)) job.durationMin = minutes;
                else message = error;
            } else {
                toggleChannelByIndex(job.channels, row - 5);
            }
        }
    }
}

}  // namespace hftrec::app::tui_detail
