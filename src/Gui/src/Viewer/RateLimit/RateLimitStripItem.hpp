#pragma once

#include <QPointer>
#include <QQuickPaintedItem>

namespace hftrec::gui::viewer {

class ChartController;

class RateLimitStripItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(hftrec::gui::viewer::ChartController* controller READ controller WRITE setController NOTIFY controllerChanged)

  public:
    explicit RateLimitStripItem(QQuickItem* parent = nullptr);

    ChartController* controller() const noexcept { return controller_; }
    void setController(ChartController* controller);

    void paint(QPainter* painter) override;

  signals:
    void controllerChanged();

  private:
    QPointer<ChartController> controller_{};
};

}  // namespace hftrec::gui::viewer
