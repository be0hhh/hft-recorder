#include "gui/viewmodels/AppViewModel.hpp"

#include <QCoreApplication>
#include <QProcessEnvironment>

#include <algorithm>

namespace hftrec::gui {

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
    value = std::clamp<qreal>(value, 100.0, 100000.0);
    if (qFuzzyCompare(bookBrightnessUsdRef_ + 1.0, value + 1.0)) return;
    bookBrightnessUsdRef_ = value;
    markDirty_();
    emit bookBrightnessUsdRefChanged();
}

void AppViewModel::setBookMinVisibleUsd(qreal value) {
    value = std::clamp<qreal>(value, 100.0, 100000.0);
    if (qFuzzyCompare(bookMinVisibleUsd_ + 1.0, value + 1.0)) return;
    bookMinVisibleUsd_ = value;
    markDirty_();
    emit bookMinVisibleUsdChanged();
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

    tradeAmountScale_ = std::clamp<qreal>(tradeScale, 0.0, 1.0);
    bookBrightnessUsdRef_ = std::clamp<qreal>(brightnessUsd, 100.0, 100000.0);
    bookMinVisibleUsd_ = std::clamp<qreal>(minVisibleUsd, 100.0, 100000.0);
    settingsDirty_ = false;
}

void AppViewModel::flushSettings_() {
    if (!settingsDirty_) return;

    settings_.setValue(QStringLiteral("viewer/trade_amount_scale"), tradeAmountScale_);
    settings_.setValue(QStringLiteral("viewer/book_brightness_usd_ref"), bookBrightnessUsdRef_);
    settings_.setValue(QStringLiteral("viewer/book_min_visible_usd"), bookMinVisibleUsd_);
    settings_.sync();
    settingsDirty_ = false;
}

void AppViewModel::refreshRenderDiagnosticsText_() {
    renderDiagnosticsText_ = QStringLiteral("%1 requested | %2 backend | %3")
        .arg(requestedRenderMode_.toUpper(), actualGraphicsApi_, activeChartRenderer_);
}

}  // namespace hftrec::gui
