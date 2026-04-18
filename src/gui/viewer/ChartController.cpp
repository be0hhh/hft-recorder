#include "gui/viewer/ChartController.hpp"

#include <algorithm>
#include <filesystem>

namespace hftrec::gui::viewer {

namespace {

std::string stripFileUrl(const QString& path) {
    QString p = path;
    if (p.startsWith(QStringLiteral("file:///"))) p.remove(0, 8);
    else if (p.startsWith(QStringLiteral("file://"))) p.remove(0, 7);
    return p.toStdString();
}

}  // namespace

ChartController::ChartController(QObject* parent) : QObject(parent) {}

void ChartController::resetSession() {
    replay_.reset();
    loaded_ = false;
    sessionDir_.clear();
    tsMin_ = tsMax_ = priceMinE8_ = priceMaxE8_ = 0;
    statusText_ = QStringLiteral("Empty — add files and press Finalize");
    emit sessionChanged();
    emit statusChanged();
    emit viewportChanged();
}

bool ChartController::addTradesFile(const QString& path) {
    if (path.trimmed().isEmpty()) {
        statusText_ = QStringLiteral("No path — type a trades.jsonl path into the Path field first");
        emit statusChanged();
        return false;
    }
    const auto st = replay_.addTradesFile(stripFileUrl(path));
    if (!isOk(st)) {
        statusText_ = QStringLiteral("trades load failed: %1")
                          .arg(QString::fromUtf8(statusToString(st).data()));
        emit statusChanged();
        return false;
    }
    statusText_ = QStringLiteral("+ trades (now %1 rows)").arg(replay_.trades().size());
    emit statusChanged();
    return true;
}

bool ChartController::addBookTickerFile(const QString& path) {
    if (path.trimmed().isEmpty()) {
        statusText_ = QStringLiteral("No path — type a bookticker.jsonl path into the Path field first");
        emit statusChanged();
        return false;
    }
    const auto st = replay_.addBookTickerFile(stripFileUrl(path));
    if (!isOk(st)) {
        statusText_ = QStringLiteral("bookticker load failed: %1")
                          .arg(QString::fromUtf8(statusToString(st).data()));
        emit statusChanged();
        return false;
    }
    statusText_ = QStringLiteral("+ bookticker (now %1 rows)").arg(replay_.bookTickers().size());
    emit statusChanged();
    return true;
}

bool ChartController::addDepthFile(const QString& path) {
    if (path.trimmed().isEmpty()) {
        statusText_ = QStringLiteral("No path — type a depth.jsonl path into the Path field first");
        emit statusChanged();
        return false;
    }
    const auto st = replay_.addDepthFile(stripFileUrl(path));
    if (!isOk(st)) {
        statusText_ = QStringLiteral("depth load failed: %1")
                          .arg(QString::fromUtf8(statusToString(st).data()));
        emit statusChanged();
        return false;
    }
    statusText_ = QStringLiteral("+ depth (now %1 rows)").arg(replay_.depths().size());
    emit statusChanged();
    return true;
}

bool ChartController::addSnapshotFile(const QString& path) {
    if (path.trimmed().isEmpty()) {
        statusText_ = QStringLiteral("No path — type a snapshot_*.json path into the Path field first");
        emit statusChanged();
        return false;
    }
    const auto st = replay_.addSnapshotFile(stripFileUrl(path));
    if (!isOk(st)) {
        statusText_ = QStringLiteral("snapshot load failed: %1")
                          .arg(QString::fromUtf8(statusToString(st).data()));
        emit statusChanged();
        return false;
    }
    statusText_ = QStringLiteral("+ snapshot (bids=%1 asks=%2)")
                      .arg(replay_.book().bids().size())
                      .arg(replay_.book().asks().size());
    emit statusChanged();
    return true;
}

void ChartController::finalizeFiles() {
    replay_.finalize();
    loaded_ = (replay_.events().size() > 0) || (replay_.book().bids().size() + replay_.book().asks().size() > 0);
    if (loaded_) computeInitialViewport_();
    statusText_ = QStringLiteral("Finalized. events=%1 trades=%2 depth=%3 bookticker=%4 bids=%5 asks=%6")
                      .arg(replay_.events().size())
                      .arg(replay_.trades().size())
                      .arg(replay_.depths().size())
                      .arg(replay_.bookTickers().size())
                      .arg(replay_.book().bids().size())
                      .arg(replay_.book().asks().size());
    emit sessionChanged();
    emit statusChanged();
    emit viewportChanged();
}

bool ChartController::loadSession(const QString& dir) {
    sessionDir_ = dir;
    loaded_ = false;
    replay_ = hftrec::replay::SessionReplay{};
    const auto path = std::filesystem::path(stripFileUrl(dir));
    const auto st = replay_.open(path);
    if (!isOk(st)) {
        statusText_ = QStringLiteral("Failed to load: %1").arg(QString::fromUtf8(statusToString(st).data()));
        emit sessionChanged();
        emit statusChanged();
        return false;
    }
    loaded_ = true;
    statusText_ = QStringLiteral("Loaded %1 (trades=%2 depth=%3)")
                      .arg(sessionDir_)
                      .arg(replay_.trades().size())
                      .arg(replay_.depths().size());
    computeInitialViewport_();
    emit sessionChanged();
    emit statusChanged();
    emit viewportChanged();
    return true;
}

void ChartController::computeInitialViewport_() {
    // Time: full session span.
    tsMin_ = replay_.firstTsNs();
    tsMax_ = replay_.lastTsNs();
    if (tsMax_ == tsMin_) tsMax_ = tsMin_ + 1;

    // Price: centre around best bid/ask and widen to include any trade price
    // that falls in the session.
    const auto& book = replay_.book();
    std::int64_t pMin = 0;
    std::int64_t pMax = 0;
    if (!book.bids().empty() && !book.asks().empty()) {
        const std::int64_t bb = book.bestBidPrice();
        const std::int64_t aa = book.bestAskPrice();
        pMin = bb;
        pMax = aa;
    } else if (!replay_.trades().empty()) {
        pMin = replay_.trades().front().priceE8;
        pMax = pMin;
    } else {
        pMin = 0;
        pMax = 1;
    }
    for (const auto& t : replay_.trades()) {
        pMin = std::min(pMin, t.priceE8);
        pMax = std::max(pMax, t.priceE8);
    }
    if (pMax <= pMin) pMax = pMin + 1;
    // 10 % padding on both ends.
    const std::int64_t pad = (pMax - pMin) / 10 + 1;
    priceMinE8_ = pMin - pad;
    priceMaxE8_ = pMax + pad;
}

void ChartController::setViewport(qint64 tsMin, qint64 tsMax,
                                  qint64 priceMinE8, qint64 priceMaxE8) {
    if (tsMax <= tsMin || priceMaxE8 <= priceMinE8) return;
    tsMin_ = tsMin;
    tsMax_ = tsMax;
    priceMinE8_ = priceMinE8;
    priceMaxE8_ = priceMaxE8;
    emit viewportChanged();
}

void ChartController::panTime(double fraction) {
    const qint64 w = tsMax_ - tsMin_;
    const qint64 dt = static_cast<qint64>(static_cast<double>(w) * fraction);
    tsMin_ += dt;
    tsMax_ += dt;
    emit viewportChanged();
}

void ChartController::panPrice(double fraction) {
    const qint64 h = priceMaxE8_ - priceMinE8_;
    const qint64 dp = static_cast<qint64>(static_cast<double>(h) * fraction);
    priceMinE8_ += dp;
    priceMaxE8_ += dp;
    emit viewportChanged();
}

void ChartController::zoomTime(double factor) {
    if (factor <= 0.0) return;
    const qint64 centre = (tsMin_ + tsMax_) / 2;
    const qint64 halfW  = static_cast<qint64>(static_cast<double>(tsMax_ - tsMin_) / (2.0 * factor));
    tsMin_ = centre - halfW;
    tsMax_ = centre + halfW;
    if (tsMax_ <= tsMin_) tsMax_ = tsMin_ + 1;
    emit viewportChanged();
}

void ChartController::zoomPrice(double factor) {
    if (factor <= 0.0) return;
    const qint64 centre = (priceMinE8_ + priceMaxE8_) / 2;
    const qint64 halfH  = static_cast<qint64>(static_cast<double>(priceMaxE8_ - priceMinE8_) / (2.0 * factor));
    priceMinE8_ = centre - halfH;
    priceMaxE8_ = centre + halfH;
    if (priceMaxE8_ <= priceMinE8_) priceMaxE8_ = priceMinE8_ + 1;
    emit viewportChanged();
}

void ChartController::autoFit() {
    if (!loaded_) return;
    // Reset book cursor and recompute bounds.
    replay_.seek(replay_.firstTsNs());
    computeInitialViewport_();
    emit viewportChanged();
}

void ChartController::jumpToStart() {
    const qint64 w = tsMax_ - tsMin_;
    tsMin_ = replay_.firstTsNs();
    tsMax_ = tsMin_ + w;
    emit viewportChanged();
}

void ChartController::jumpToEnd() {
    const qint64 w = tsMax_ - tsMin_;
    tsMax_ = replay_.lastTsNs();
    tsMin_ = tsMax_ - w;
    emit viewportChanged();
}

}  // namespace hftrec::gui::viewer
