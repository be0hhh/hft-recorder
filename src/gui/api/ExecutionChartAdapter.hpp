#pragma once

#include "core/execution/ExecutionVenue.hpp"

#include <QObject>

namespace hftrec::gui::viewer {
class ChartController;
}

namespace hftrec::gui::api {

class ExecutionChartAdapter final : public QObject, public hftrec::execution::IExecutionEventSink {
    Q_OBJECT

  public:
    explicit ExecutionChartAdapter(QObject* parent = nullptr);

    void setChartController(hftrec::gui::viewer::ChartController* controller) noexcept;
    void onExecutionEvent(const hftrec::execution::ExecutionEvent& event) noexcept override;

  private:
    hftrec::gui::viewer::ChartController* controller_{nullptr};
};

}  // namespace hftrec::gui::api
