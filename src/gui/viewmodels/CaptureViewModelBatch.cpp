#include "gui/viewmodels/CaptureViewModel.hpp"

#include <memory>

#include "gui/viewmodels/CaptureViewModelInternal.hpp"

namespace hftrec::gui {

bool CaptureViewModel::startTrades() {
    if (!ensureCoordinatorBatch_()) {
        refreshState();
        return false;
    }

    const auto configs = makeConfigs();
    for (std::size_t i = 0; i < coordinators_.size() && i < configs.size(); ++i) {
        const auto status = coordinators_[i]->startTrades(configs[i]);
        if (!isOk(status)) {
            abortCoordinatorBatch_(joinCoordinatorErrors_());
            refreshState();
            return false;
        }
    }

    setStatusText(QStringLiteral("Trades capture started for %1 symbol(s)").arg(coordinators_.size()));
    refreshState();
    return true;
}

void CaptureViewModel::stopTrades() {
    for (auto& coordinator : coordinators_) {
        if (coordinator) coordinator->stopTrades();
    }
    setStatusText(QStringLiteral("Trades capture stopped"));
    refreshState();
}

bool CaptureViewModel::startBookTicker() {
    if (!ensureCoordinatorBatch_()) {
        refreshState();
        return false;
    }

    const auto configs = makeConfigs();
    for (std::size_t i = 0; i < coordinators_.size() && i < configs.size(); ++i) {
        const auto status = coordinators_[i]->startBookTicker(configs[i]);
        if (!isOk(status)) {
            abortCoordinatorBatch_(joinCoordinatorErrors_());
            refreshState();
            return false;
        }
    }

    setStatusText(QStringLiteral("BookTicker capture started for %1 symbol(s)").arg(coordinators_.size()));
    refreshState();
    return true;
}

void CaptureViewModel::stopBookTicker() {
    for (auto& coordinator : coordinators_) {
        if (coordinator) coordinator->stopBookTicker();
    }
    setStatusText(QStringLiteral("BookTicker capture stopped"));
    refreshState();
}

bool CaptureViewModel::startOrderbook() {
    if (!ensureCoordinatorBatch_()) {
        refreshState();
        return false;
    }

    const auto configs = makeConfigs();
    for (std::size_t i = 0; i < coordinators_.size() && i < configs.size(); ++i) {
        const auto status = coordinators_[i]->startOrderbook(configs[i]);
        if (!isOk(status)) {
            abortCoordinatorBatch_(joinCoordinatorErrors_());
            refreshState();
            return false;
        }
    }

    setStatusText(QStringLiteral("Orderbook capture started for %1 symbol(s)").arg(coordinators_.size()));
    refreshState();
    return true;
}

void CaptureViewModel::stopOrderbook() {
    for (auto& coordinator : coordinators_) {
        if (coordinator) coordinator->stopOrderbook();
    }
    setStatusText(QStringLiteral("Orderbook capture stopped"));
    refreshState();
}

void CaptureViewModel::finalizeSession() {
    bool ok = true;
    for (auto& coordinator : coordinators_) {
        if (!coordinator) continue;
        const auto status = coordinator->finalizeSession();
        if (!isOk(status)) ok = false;
    }
    setStatusText(ok ? QStringLiteral("Session batch finalized") : joinCoordinatorErrors_());
    clearCoordinatorBatch_();
    refreshState();
}

bool CaptureViewModel::ensureCoordinatorBatch_() {
    if (!coordinators_.empty()) return true;

    const auto configs = makeConfigs();
    if (configs.empty()) {
        setStatusText(QStringLiteral("Enter at least one symbol, e.g. ETH or BTC ETH"));
        return false;
    }

    coordinators_.clear();
    coordinators_.reserve(configs.size());
    for (std::size_t i = 0; i < configs.size(); ++i) {
        coordinators_.push_back(std::make_unique<capture::CaptureCoordinator>());
    }
    return true;
}

void CaptureViewModel::clearCoordinatorBatch_() {
    coordinators_.clear();
}

void CaptureViewModel::abortCoordinatorBatch_(const QString& fallbackStatus) {
    QStringList errors;
    for (auto& coordinator : coordinators_) {
        if (!coordinator) continue;
        const auto preFinalizeError = QString::fromStdString(coordinator->lastError()).trimmed();
        coordinator->stopTrades();
        coordinator->stopBookTicker();
        coordinator->stopOrderbook();
        const auto status = coordinator->finalizeSession();
        if (!preFinalizeError.isEmpty() && !errors.contains(preFinalizeError)) errors.push_back(preFinalizeError);
        if (!isOk(status) && preFinalizeError.isEmpty()) {
            const auto statusText = QString::fromUtf8(hftrec::statusToString(status).data());
            if (!errors.contains(statusText)) errors.push_back(statusText);
        }
    }

    clearCoordinatorBatch_();
    setStatusText(errors.isEmpty() ? fallbackStatus : errors.join(QStringLiteral(" | ")));
}

QString CaptureViewModel::joinCoordinatorErrors_() const {
    QStringList errors;
    for (const auto& coordinator : coordinators_) {
        if (!coordinator) continue;
        const auto error = QString::fromStdString(coordinator->lastError()).trimmed();
        if (!error.isEmpty() && !errors.contains(error)) errors.push_back(error);
    }
    if (errors.isEmpty()) return QStringLiteral("Operation failed");
    return errors.join(QStringLiteral(" | "));
}

}  // namespace hftrec::gui
