#pragma once

#include <QPointF>
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
    Q_PROPERTY(bool tradesVisible READ tradesVisible WRITE setTradesVisible NOTIFY tradesVisibleChanged)

  public:
    explicit ChartItem(QQuickItem* parent = nullptr);
    ~ChartItem() override;

    ChartController* controller() const noexcept { return controller_; }
    void setController(ChartController* c);
    bool tradesVisible() const noexcept { return tradesVisible_; }
    void setTradesVisible(bool value);

    void paint(QPainter* painter) override;
    Q_INVOKABLE void setHoverPoint(qreal x, qreal y);
    Q_INVOKABLE void clearHover();

  signals:
    void controllerChanged();
    void tradesVisibleChanged();

  private slots:
    void requestRepaint();

  private:
    void updateHover_();

    ChartController* controller_{nullptr};
    QPointF hoverPoint_{};
    bool hoverActive_{false};
    int hoveredTradeIndex_{-1};
    bool tradesVisible_{true};
};

}  // namespace hftrec::gui::viewer
