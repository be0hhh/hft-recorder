#include "gui/api/ExecutionChartAdapter.hpp"

#include <QMetaObject>
#include <QString>
#include <Qt>

#include "canon/Enums.hpp"
#include "gui/viewer/ChartController.hpp"

namespace hftrec::gui::api {
namespace {

QString orderTypeName(std::uint8_t typeRaw) {
    const auto type = static_cast<canon::OrderType>(typeRaw);
    switch (type) {
        case canon::OrderType::Market: return QStringLiteral("market");
        case canon::OrderType::Limit: return QStringLiteral("limit");
        case canon::OrderType::Stop: return QStringLiteral("stop");
        case canon::OrderType::StopLoss: return QStringLiteral("stop_loss");
        default: return QStringLiteral("order");
    }
}

QString sideName(std::uint8_t sideRaw) {
    return sideRaw == 1u ? QStringLiteral("buy") : QStringLiteral("sell");
}

QString labelFor(const hftrec::execution::ExecutionEvent& event) {
    const QString prefix = event.kind == hftrec::execution::ExecutionEventKind::Reject
        ? QStringLiteral("reject")
        : QStringLiteral("order");
    return QStringLiteral("%1 %2 %3 %4")
        .arg(prefix, sideName(event.sideRaw), orderTypeName(event.typeRaw), QString::fromStdString(event.symbol));
}

}  // namespace

ExecutionChartAdapter::ExecutionChartAdapter(QObject* parent) : QObject(parent) {}

void ExecutionChartAdapter::setChartController(hftrec::gui::viewer::ChartController* controller) noexcept {
    controller_ = controller;
}

void ExecutionChartAdapter::onExecutionEvent(const hftrec::execution::ExecutionEvent& event) noexcept {
    auto* controller = hftrec::gui::viewer::ChartController::activeInstance();
    if (controller == nullptr) controller = controller_;
    if (controller == nullptr || event.tsNs == 0u) return;
    if (event.kind != hftrec::execution::ExecutionEventKind::Ack &&
        event.kind != hftrec::execution::ExecutionEventKind::Reject) {
        return;
    }

    const qint64 tsNs = static_cast<qint64>(event.tsNs);
    const QString label = labelFor(event);
    QMetaObject::invokeMethod(controller,
                              [controller, tsNs, label]() { controller->addVerticalMarker(tsNs, label); },
                              Qt::QueuedConnection);
}

}  // namespace hftrec::gui::api
