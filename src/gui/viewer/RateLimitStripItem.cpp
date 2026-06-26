#include "gui/viewer/RateLimitStripItem.hpp"

#include <QPainter>

#include "gui/viewer/ChartController.hpp"
#include "gui/viewer/RateLimitGraphPainter.hpp"
#include "gui/viewer/RateLimitUsage.hpp"

namespace hftrec::gui::viewer {
RateLimitStripItem::RateLimitStripItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
}

void RateLimitStripItem::setController(ChartController* controller) {
    if (controller_ == controller) return;
    if (controller_) disconnect(controller_, nullptr, this, nullptr);
    controller_ = controller;
    if (controller_) {
        connect(controller_, &ChartController::viewportChanged, this, [this]() { update(); });
        connect(controller_, &ChartController::backtestResultChanged, this, [this]() { update(); });
    }
    emit controllerChanged();
    update();
}

void RateLimitStripItem::paint(QPainter* painter) {
    if (painter == nullptr) return;
    if (!controller_ || !controller_->hasRateLimitUsage()) return;
    drawRateLimitGraph(*painter, boundingRect(), controller_->rateLimitUsage(), controller_->tsMin(), controller_->tsMax());
}

}  // namespace hftrec::gui::viewer
