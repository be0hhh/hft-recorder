#include "gui/viewmodels/AppViewModel.hpp"

#include <QCoreApplication>
#include <QProcessEnvironment>

#include <algorithm>

namespace hftrec::gui {

namespace {

QString normalizeLiveUpdateMode(QString mode) {
    mode = mode.trimmed().toLower();
    if (mode == QStringLiteral("tick") || mode == QStringLiteral("tick-by-tick") || mode == QStringLiteral("16ms")) {
        return QStringLiteral("tick");
    }
    if (mode == QStringLiteral("250") || mode == QStringLiteral("250ms")) {
        return QStringLiteral("250ms");
    }
    if (mode == QStringLiteral("500") || mode == QStringLiteral("500ms")) {
        return QStringLiteral("500ms");
    }
    return QStringLiteral("100ms");
}

int liveUpdateIntervalFromMode(QStringView mode) noexcept {
    if (mode == QStringLiteral("tick")) return 16;
    if (mode == QStringLiteral("250ms")) return 250;
    if (mode == QStringLiteral("500ms")) return 500;
    return 100;
}

}  // namespace

AppViewModel::AppViewModel(QObject* parent)
    : QObject(parent) {
    requestedRenderMode_ = QProcessEnvironment::systemEnvironment().value(
        QStringLiteral("HFTREC_RENDER_MODE"),
        QStringLiteral("cpu")).trimmed().toLower();
    if (requestedRenderMode_.isEmpty()) requestedRenderMode_ = QStringLiteral("cpu");
    refreshRenderDiagnosticsText_();
    loadSettings_();

    saveTimer_.setInterval(1000);
    saveTimer_.setSingleShot(false);
    connect(&saveTimer_, &QTimer::timeout, this, [this]() {
        if (!settingsDirty_) return;
        flushSettings_();
    });
    saveTimer_.start();

    if (auto* app = QCoreApplication::instance()) {
        connect(app, &QCoreApplication::aboutToQuit, this, [this]() {
            flushSettings_();
        });
    }
}

QString AppViewModel::statusText() const {
    return statusText_;
}

void AppViewModel::setRenderDiagnostics(const QString& requestedMode, const QString& actualGraphicsApi) {
    const QString normalizedRequested = requestedMode.trimmed().toLower().isEmpty()
        ? QStringLiteral("cpu")
        : requestedMode.trimmed().toLower();
    const QString normalizedApi = actualGraphicsApi.trimmed().toLower().isEmpty()
        ? QStringLiteral("unknown")
        : actualGraphicsApi.trimmed();

    if (normalizedRequested == requestedRenderMode_ && normalizedApi == actualGraphicsApi_) {
        return;
    }
    requestedRenderMode_ = normalizedRequested;
    actualGraphicsApi_ = normalizedApi;
    refreshRenderDiagnosticsText_();
    emit renderDiagnosticsChanged();
}

void AppViewModel::setTradeAmountScale(qreal value) {
    value = std::clamp<qreal>(value, 0.0, 1.0);
    if (qFuzzyCompare(tradeAmountScale_ + 1.0, value + 1.0)) return;
    tradeAmountScale_ = value;
    markDirty_();
    emit tradeAmountScaleChanged();
}

void AppViewModel::setBookBrightnessUsdRef(qreal value) {
    value = std::clamp<qreal>(value, 1000.0, 1000000.0);
    if (qFuzzyCompare(bookBrightnessUsdRef_ + 1.0, value + 1.0)) return;
    bookBrightnessUsdRef_ = value;
    markDirty_();
    emit bookBrightnessUsdRefChanged();
}

void AppViewModel::setBookMinVisibleUsd(qreal value) {
    value = std::clamp<qreal>(value, 1000.0, 1000000.0);
    if (qFuzzyCompare(bookMinVisibleUsd_ + 1.0, value + 1.0)) return;
    bookMinVisibleUsd_ = value;
    markDirty_();
    emit bookMinVisibleUsdChanged();
}

void AppViewModel::setBookDepthWindowPct(qreal value) {
    value = std::clamp<qreal>(value, 1.0, 25.0);
    if (qFuzzyCompare(bookDepthWindowPct_ + 1.0, value + 1.0)) return;
    bookDepthWindowPct_ = value;
    markDirty_();
    emit bookDepthWindowPctChanged();
}

int AppViewModel::liveUpdateIntervalMs() const noexcept {
    return liveUpdateIntervalFromMode(liveUpdateMode_);
}

void AppViewModel::setLiveUpdateMode(const QString& mode) {
    const QString normalized = normalizeLiveUpdateMode(mode);
    if (normalized == liveUpdateMode_) return;
    liveUpdateMode_ = normalized;
    markDirty_();
    emit liveUpdateModeChanged();
}

void AppViewModel::setActiveChartRenderer(const QString& rendererName) {
    const QString normalized = rendererName.trimmed().toLower().isEmpty()
        ? QStringLiteral("cpu-chart")
        : rendererName.trimmed().toLower();
    if (normalized == activeChartRenderer_) return;
    activeChartRenderer_ = normalized;
    refreshRenderDiagnosticsText_();
    emit renderDiagnosticsChanged();
}

void AppViewModel::loadSettings_() {
    const auto tradeScale = settings_.value(QStringLiteral("viewer/trade_amount_scale"), tradeAmountScale_).toReal();
    const auto brightnessUsd = settings_.value(QStringLiteral("viewer/book_brightness_usd_ref"), bookBrightnessUsdRef_).toReal();
    const auto minVisibleUsd = settings_.value(QStringLiteral("viewer/book_min_visible_usd"), bookMinVisibleUsd_).toReal();
    const auto depthWindowPct = settings_.value(QStringLiteral("viewer/book_depth_window_pct"), bookDepthWindowPct_).toReal();
    const auto liveMode = settings_.value(QStringLiteral("viewer/live_update_mode"), liveUpdateMode_).toString();

    tradeAmountScale_ = std::clamp<qreal>(tradeScale, 0.0, 1.0);
    bookBrightnessUsdRef_ = std::clamp<qreal>(brightnessUsd, 1000.0, 1000000.0);
    bookMinVisibleUsd_ = std::clamp<qreal>(minVisibleUsd, 1000.0, 1000000.0);
    bookDepthWindowPct_ = std::clamp<qreal>(depthWindowPct, 1.0, 25.0);
    liveUpdateMode_ = normalizeLiveUpdateMode(liveMode);
    settingsDirty_ = false;
}

void AppViewModel::flushSettings_() {
    if (!settingsDirty_) return;

    settings_.setValue(QStringLiteral("viewer/trade_amount_scale"), tradeAmountScale_);
    settings_.setValue(QStringLiteral("viewer/book_brightness_usd_ref"), bookBrightnessUsdRef_);
    settings_.setValue(QStringLiteral("viewer/book_min_visible_usd"), bookMinVisibleUsd_);
    settings_.setValue(QStringLiteral("viewer/book_depth_window_pct"), bookDepthWindowPct_);
    settings_.setValue(QStringLiteral("viewer/live_update_mode"), liveUpdateMode_);
    settings_.sync();
    settingsDirty_ = false;
}

void AppViewModel::refreshRenderDiagnosticsText_() {
    renderDiagnosticsText_ = QStringLiteral("%1 requested | %2 backend | %3")
        .arg(requestedRenderMode_.toUpper(), actualGraphicsApi_, activeChartRenderer_);
}

}  // namespace hftrec::gui