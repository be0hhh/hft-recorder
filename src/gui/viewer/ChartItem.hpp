#pragma once

#include <QQuickPaintedItem>

class QPainter;

namespace hftrec::gui::viewer {

class ChartController;

// QPainter-based chart item. Software-renderer friendly (Qt scene-graph
// custom QSGGeometryNodes do not render under the software backend that
// we pin on WSL), and plenty fast for the data sizes we deal with here
// (low tens of thousands of primitives per frame).
class ChartItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(hftrec::gui::viewer::ChartController* controller
                   READ controller WRITE setController NOTIFY controllerChanged)

  public:
    explicit ChartItem(QQuickItem* parent = nullptr);
    ~ChartItem() override;

    ChartController* controller() const noexcept { return controller_; }
    void setController(ChartController* c);

    void paint(QPainter* painter) override;

  signals:
    void controllerChanged();

  private slots:
    void requestRepaint();

  private:
    ChartController* controller_{nullptr};
};

}  // namespace hftrec::gui::viewer
