#include "gui/viewer/ChartController.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>

#include <core/replay/JsonLineParser.hpp>

namespace hftrec::gui::viewer {

namespace {

std::string stripFileUrl(const QString& path) {
    QString p = path;
    if (p.startsWith(QStringLiteral("file:///"))) p.remove(0, 8);
    else if (p.startsWith(QStringLiteral("file://"))) p.remove(0, 7);
    return p.toStdString();
}

QString replayFailureText(const hftrec::replay::SessionReplay& replay, Status status, QStringView prefix) {
    if (!replay.errorDetail().empty()) {
        return QStringLiteral("%1: %2")
            .arg(prefix, QString::fromStdString(std::string{replay.errorDetail()}));
    }
    return QStringLiteral("%1: %2").arg(prefix, QString::fromUtf8(statusToString(status).data()));
}

std::filesystem::path findLatestSnapshotPath(const std::filesystem::path& sessionDir) {
    std::filesystem::path latest{};
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(sessionDir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        const auto filename = entry.path().filename().string();
        if (!filename.starts_with("snapshot_") || entry.path().extension() != ".json") continue;
        if (latest.empty() || filename > latest.filename().string()) latest = entry.path();
    }
    return latest;
}

QString liveModeLabel(int intervalMs) {
    if (intervalMs <= 16) return QStringLiteral("tick");
    return QStringLiteral("%1 ms").arg(intervalMs);
}

}  // namespace

void ChartController::startLiveTail_(const std::filesystem::path& sessionDir) {
    liveTrades_ = LiveTailFile{sessionDir / "trades.jsonl", 0, {}};
    liveBookTicker_ = LiveTailFile{sessionDir / "bookticker.jsonl", 0, {}};
    liveDepth_ = LiveTailFile{sessionDir / "depth.jsonl", 0, {}};
    liveSnapshotPath_ = findLatestSnapshotPath(sessionDir);
    liveSnapshotLoaded_ = !liveSnapshotPath_.empty();
    liveOrderbookHealthy_ = isOk(replay_.status());
    liveFollowEdge_ = false;
    auto syncTailOffset = [](LiveTailFile& file) {
        std::error_code ec;
        if (std::filesystem::exists(file.path, ec) && !ec) {
            file.offset = std::filesystem::file_size(file.path, ec);
            if (ec) file.offset = 0;
        } else {
            file.offset = 0;
        }
        file.pending.clear();
    };

    syncTailOffset(liveTrades_);
    syncTailOffset(liveBookTicker_);
    syncTailOffset(liveDepth_);
    if (liveTailTimer_ != nullptr) liveTailTimer_->start();
}

void ChartController::stopLiveTail_() noexcept {
    if (liveTailTimer_ != nullptr) liveTailTimer_->stop();
    liveTrades_ = LiveTailFile{};
    liveBookTicker_ = LiveTailFile{};
    liveDepth_ = LiveTailFile{};
    liveSnapshotPath_.clear();
    liveSnapshotLoaded_ = false;
    liveOrderbookHealthy_ = true;
}

void ChartController::markUserViewportControl_() noexcept {
    liveFollowEdge_ = false;
}

void ChartController::pollLiveTail_() {
    if (sessionDir_.isEmpty()) return;

    const auto sessionPath = std::filesystem::path(stripFileUrl(sessionDir_));
    std::error_code ec;
    if (!std::filesystem::exists(sessionPath, ec) || ec) return;

    const auto oldTsMin = tsMin_;
    const auto oldTsMax = tsMax_;
    const auto oldPriceMin = priceMinE8_;
    const auto oldPriceMax = priceMaxE8_;
    const auto oldLoaded = loaded_;
    const auto oldTradeCount = replay_.trades().size();
    const auto oldDepthCount = replay_.depths().size();
    const auto oldBookTickerCount = replay_.bookTickers().size();

    bool appendedRows = false;
    bool needReload = false;
    QString failureText{};

    const auto latestSnapshotPath = findLatestSnapshotPath(sessionPath);
    if ((!latestSnapshotPath.empty() && latestSnapshotPath != liveSnapshotPath_)
        || (!liveSnapshotLoaded_ && !latestSnapshotPath.empty())) {
        needReload = true;
    }

    auto tailRows = [&](LiveTailFile& file, auto&& consumeLine, QStringView label) {
        std::error_code fileEc;
        if (file.path.empty() || !std::filesystem::exists(file.path, fileEc) || fileEc) return;

        const auto fileSize = std::filesystem::file_size(file.path, fileEc);
        if (fileEc) return;
        if (fileSize < file.offset) {
            needReload = true;
            return;
        }
        if (fileSize == file.offset) return;

        std::ifstream in(file.path, std::ios::binary);
        if (!in) {
            failureText = QStringLiteral("Live %1 read failed").arg(label.toString());
            return;
        }

        in.seekg(static_cast<std::streamoff>(file.offset), std::ios::beg);
        std::string chunk(static_cast<std::size_t>(fileSize - file.offset), '\0');
        in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const auto bytesRead = static_cast<std::size_t>(in.gcount());
        chunk.resize(bytesRead);
        if (bytesRead == 0u) return;

        const std::uintmax_t nextOffset = file.offset + bytesRead;
        std::string nextPending = file.pending;
        nextPending += chunk;

        std::size_t lineStart = 0;
        while (true) {
            const auto lineEnd = nextPending.find('\n', lineStart);
            if (lineEnd == std::string::npos) break;

            std::string line = nextPending.substr(lineStart, lineEnd - lineStart);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) {
                const auto st = consumeLine(std::string_view{line});
                if (!isOk(st)) {
                    needReload = true;
                    failureText = QStringLiteral("Live %1 parse failed, scheduling reload: %2")
                        .arg(label.toString(), QString::fromUtf8(statusToString(st).data()));
                    return;
                }
                appendedRows = true;
            }
            lineStart = lineEnd + 1;
        }

        nextPending.erase(0, lineStart);
        file.offset = nextOffset;
        file.pending = std::move(nextPending);
    };

    if (!needReload) {
        tailRows(liveTrades_,
                 [this](std::string_view line) {
                     hftrec::replay::TradeRow row{};
                     const auto st = hftrec::replay::parseTradeLine(line, row);
                     if (isOk(st)) replay_.appendTradeRow(std::move(row));
                     return st;
                 },
                 QStringLiteral("trades"));
    }
    if (!needReload && failureText.isEmpty()) {
        tailRows(liveBookTicker_,
                 [this](std::string_view line) {
                     hftrec::replay::BookTickerRow row{};
                     const auto st = hftrec::replay::parseBookTickerLine(line, row);
                     if (isOk(st)) replay_.appendBookTickerRow(std::move(row));
                     return st;
                 },
                 QStringLiteral("bookticker"));
    }
    if (!needReload && failureText.isEmpty()) {
        tailRows(liveDepth_,
                 [this](std::string_view line) {
                     hftrec::replay::DepthRow row{};
                     const auto st = hftrec::replay::parseDepthLine(line, row);
                     if (isOk(st)) replay_.appendDepthRow(std::move(row));
                     return st;
                 },
                 QStringLiteral("depth"));
    }

    if (needReload) {
        const auto st = replay_.open(sessionPath);
        if (!isOk(st)) {
            const auto nextStatus = replayFailureText(replay_, st, QStringLiteral("Live reload failed"));
            if (statusText_ != nextStatus) {
                statusText_ = nextStatus;
                emit statusChanged();
            }
            return;
        }
        startLiveTail_(sessionPath);
        liveSnapshotPath_ = findLatestSnapshotPath(sessionPath);
        liveSnapshotLoaded_ = !liveSnapshotPath_.empty();
        appendedRows = true;
        failureText.clear();
    }

    if (!failureText.isEmpty()) {
        if (statusText_ != failureText) {
            statusText_ = failureText;
            emit statusChanged();
        }
        return;
    }

    if (!appendedRows) return;

    replay_.refreshLiveTimeline();
    liveOrderbookHealthy_ = isOk(replay_.status());
    loaded_ = !replay_.buckets().empty() || !replay_.book().bids().empty() || !replay_.book().asks().empty();
    currentBookTickerIndex_ = -1;

    const auto nextStatus = isOk(replay_.status())
        ? QStringLiteral("Live %1 | trades=%2 depth=%3 bookticker=%4")
              .arg(liveModeLabel(liveUpdateIntervalMs_))
              .arg(replay_.trades().size())
              .arg(replay_.depths().size())
              .arg(replay_.bookTickers().size())
        : replayFailureText(replay_, replay_.status(), QStringLiteral("Live integrity failed"));
    const bool viewportChangedFlag = (tsMin_ != oldTsMin) || (tsMax_ != oldTsMax)
        || (priceMinE8_ != oldPriceMin) || (priceMaxE8_ != oldPriceMax);
    const bool sessionChangedFlag = (loaded_ != oldLoaded)
        || (replay_.trades().size() != oldTradeCount)
        || (replay_.depths().size() != oldDepthCount)
        || (replay_.bookTickers().size() != oldBookTickerCount);

    if (sessionChangedFlag) emit liveDataChanged();
    if (viewportChangedFlag) emit viewportChanged();
    if (statusText_ != nextStatus) {
        statusText_ = nextStatus;
        emit statusChanged();
    }
}

void ChartController::resetSession() {
    stopLiveTail_();
    replay_.reset();
    loaded_ = false;
    sessionDir_.clear();
    tsMin_ = tsMax_ = priceMinE8_ = priceMaxE8_ = 0;
    currentBookTickerIndex_ = -1;
    selectionActive_ = false;
    selectionSummaryText_.clear();
    statusText_ = QStringLiteral("Choose a session.");
    emit sessionChanged();
    emit statusChanged();
    emit viewportChanged();
    emit selectionChanged();
}

bool ChartController::addTradesFile(const QString& path) {
    if (path.trimmed().isEmpty()) {
        statusText_ = QStringLiteral("No path. Enter a trades.jsonl path first.");
        emit statusChanged();
        return false;
    }

    const auto st = replay_.addTradesFile(stripFileUrl(path));
    if (!isOk(st)) {
        statusText_ = replayFailureText(replay_, st, QStringLiteral("trades load failed"));
        emit statusChanged();
        return false;
    }

    statusText_ = QStringLiteral("+ trades (now %1 rows)").arg(replay_.trades().size());
    emit statusChanged();
    return true;
}

bool ChartController::addBookTickerFile(const QString& path) {
    if (path.trimmed().isEmpty()) {
        statusText_ = QStringLiteral("No path. Enter a bookticker.jsonl path first.");
        emit statusChanged();
        return false;
    }

    const auto st = replay_.addBookTickerFile(stripFileUrl(path));
    if (!isOk(st)) {
        statusText_ = replayFailureText(replay_, st, QStringLiteral("bookticker load failed"));
        emit statusChanged();
        return false;
    }

    statusText_ = QStringLiteral("+ bookticker (now %1 rows)").arg(replay_.bookTickers().size());
    emit statusChanged();
    return true;
}

bool ChartController::addDepthFile(const QString& path) {
    if (path.trimmed().isEmpty()) {
        statusText_ = QStringLiteral("No path. Enter a depth.jsonl path first.");
        emit statusChanged();
        return false;
    }

    const auto st = replay_.addDepthFile(stripFileUrl(path));
    if (!isOk(st)) {
        statusText_ = replayFailureText(replay_, st, QStringLiteral("depth load failed"));
        emit statusChanged();
        return false;
    }

    statusText_ = QStringLiteral("+ depth (now %1 rows)").arg(replay_.depths().size());
    emit statusChanged();
    return true;
}

bool ChartController::addSnapshotFile(const QString& path) {
    if (path.trimmed().isEmpty()) {
        statusText_ = QStringLiteral("No path. Enter a snapshot_*.json path first.");
        emit statusChanged();
        return false;
    }

    const auto st = replay_.addSnapshotFile(stripFileUrl(path));
    if (!isOk(st)) {
        statusText_ = replayFailureText(replay_, st, QStringLiteral("snapshot load failed"));
        emit statusChanged();
        return false;
    }

    statusText_ = QStringLiteral("+ snapshot");
    emit statusChanged();
    return true;
}

void ChartController::finalizeFiles() {
    stopLiveTail_();
    clearSelection();
    replay_.finalize();
    if (!isOk(replay_.status())) {
        loaded_ = false;
        currentBookTickerIndex_ = -1;
        statusText_ = replayFailureText(replay_, replay_.status(), QStringLiteral("Finalize failed"));
        emit sessionChanged();
        emit statusChanged();
        emit viewportChanged();
        return;
    }

    loaded_ = (!replay_.buckets().empty()) || (replay_.book().bids().size() + replay_.book().asks().size() > 0);
    if (loaded_) computeInitialViewport_();
    currentBookTickerIndex_ = -1;

    statusText_ = QStringLiteral("Finalized.");
    emit sessionChanged();
    emit statusChanged();
    emit viewportChanged();
}

bool ChartController::loadSession(const QString& dir) {
    stopLiveTail_();
    sessionDir_ = dir;
    loaded_ = false;
    replay_ = hftrec::replay::SessionReplay{};
    clearSelection();

    const auto path = std::filesystem::path(stripFileUrl(dir));
    const auto st = replay_.open(path);
    if (!isOk(st)) {
        statusText_ = replayFailureText(replay_, st, QStringLiteral("Failed to load session"));
        emit sessionChanged();
        emit statusChanged();
        return false;
    }

    loaded_ = !replay_.buckets().empty() || !replay_.book().bids().empty() || !replay_.book().asks().empty();
    currentBookTickerIndex_ = -1;
    statusText_ = QStringLiteral("Loaded trades=%1 depth=%2 bookticker=%3")
                      .arg(replay_.trades().size())
                      .arg(replay_.depths().size())
                      .arg(replay_.bookTickers().size());
    if (!replay_.errorDetail().empty()) {
        statusText_ += QStringLiteral(" | %1").arg(QString::fromStdString(std::string{replay_.errorDetail()}));
    }
    if (loaded_) {
        computeInitialViewport_();
    }
    startLiveTail_(path);
    emit sessionChanged();
    emit statusChanged();
    emit viewportChanged();
    return true;
}

}  // namespace hftrec::gui::viewer
