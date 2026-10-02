#include "BookTickerCompareInternal.hpp"

namespace hftrec::gui::viewer {
using namespace compare_detail;

BookTickerCompareItem::BookTickerCompareItem(QQuickItem* parent)
    : QQuickPaintedItem(parent) {
    setAntialiasing(false);
    setAcceptedMouseButtons(Qt::NoButton);
}
void BookTickerCompareItem::setController(BookTickerCompareController* controller) {
    if (controller_ == controller) return;
    if (controller_ != nullptr) disconnect(controller_, nullptr, this, nullptr);
    controller_ = controller;
    if (controller_ != nullptr) {
        connect(controller_, &BookTickerCompareController::dataChanged, this, [this]() { update(); });
        connect(controller_, &BookTickerCompareController::statusChanged, this, [this]() { update(); });
        connect(controller_, &BookTickerCompareController::viewportChanged, this, [this]() { update(); });
    }
    emit controllerChanged();
    update();
}

}  // namespace hftrec::gui::viewer
