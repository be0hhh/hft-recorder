#pragma once

#include <QPointF>
#include <QQuickPaintedItem>

class QPainter;

namespace hftrec::gui::viewer {

class MoexBasisController;

class MoexBasisItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(hftrec::gui::viewer::MoexBasisController* controller READ controller WRITE setController NOTIFY controllerChanged)

  public:
    explicit MoexBasisItem(QQuickItem* parent = nullptr);

    MoexBasisController* controller() const noexcept { return controller_; }
    void setController(MoexBasisController* controller);
    void paint(QPainter* painter) override;

    Q_INVOKABLE void setHoverPoint(qreal x, qreal y);
    Q_INVOKABLE void clearHover();
    Q_INVOKABLE bool isPricePanelPoint(qreal x, qreal y) const;
    Q_INVOKABLE bool isBasisPanelPoint(qreal x, qreal y) const;
    Q_INVOKABLE double priceAnchorFraction(qreal y) const;
    Q_INVOKABLE double basisAnchorFraction(qreal y) const;

  signals:
    void controllerChanged();

  private:
    MoexBasisController* controller_{nullptr};
    bool hoverActive_{false};
    QPointF hoverPoint_{};
};

}  // namespace hftrec::gui::viewer
