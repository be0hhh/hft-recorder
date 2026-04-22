#include "gui/viewer/ChartController.hpp"

namespace hftrec::gui::viewer {

namespace {

bool envForcesSoftwareRenderer() {
    const auto qsgBackend = qEnvironmentVariable("QSG_RHI_BACKEND").trimmed().toLower();
    const auto quickBackend = qEnvironmentVariable("QT_QUICK_BACKEND").trimmed().toLower();
    const auto libglSoftware = qEnvironmentVariable("LIBGL_ALWAYS_SOFTWARE").trimmed().toLower();
    const auto xcbIntegration = qEnvironmentVariable("QT_XCB_GL_INTEGRATION").trimmed().toLower();

    return qsgBackend == QStringLiteral("software")
        || quickBackend == QStringLiteral("software")
        || libglSoftware == QStringLiteral("1")
        || xcbIntegration == QStringLiteral("none");
}

}  // namespace

ChartController::ChartController(QObject* parent) : QObject(parent) {
    gpuRendererAvailable_ = !envForcesSoftwareRenderer();
    liveTailTimer_ = new QTimer(this);
    liveTailTimer_->setInterval(liveUpdateIntervalMs_);
    liveTailTimer_->setTimerType(Qt::PreciseTimer);
    connect(liveTailTimer_, &QTimer::timeout, this, [this]() { pollLiveTail_(); });
}

void ChartController::setLiveUpdateIntervalMs(int intervalMs) {
    const int clamped = intervalMs <= 16 ? 16
        : (intervalMs <= 100 ? 100
        : (intervalMs <= 250 ? 250 : 500));
    if (liveUpdateIntervalMs_ == clamped) return;
    liveUpdateIntervalMs_ = clamped;
    if (liveTailTimer_ != nullptr) liveTailTimer_->setInterval(liveUpdateIntervalMs_);
}

int ChartController::liveUpdateIntervalMs() const noexcept {
    return liveUpdateIntervalMs_;
}

}  // namespace hftrec::gui::viewer
