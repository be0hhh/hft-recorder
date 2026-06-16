#pragma once

#include <QQuickPaintedItem>

class QPainter;

namespace hftrec::gui::viewer {

class BookTickerCompareController;

class BookTickerCompareItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(hftrec::gui::viewer::BookTickerCompareController* controller READ controller WRITE setController NOTIFY controllerChanged)

  public:
    explicit BookTickerCompareItem(QQuickItem* parent = nullptr);

    BookTickerCompareController* controller() const noexcept { return controller_; }
    void setController(BookTickerCompareController* controller);
    void paint(QPainter* painter) override;

    Q_INVOKABLE void setHoverPoint(qreal x, qreal y);
    Q_INVOKABLE void clearHover();
    Q_INVOKABLE void beginMeasure(qreal x, qreal y);
    Q_INVOKABLE void updateMeasure(qreal x, qreal y);
    Q_INVOKABLE void endMeasure();
    Q_INVOKABLE void clearMeasure();
    Q_INVOKABLE bool isPricePanelPoint(qreal x, qreal y) const;
    Q_INVOKABLE bool isSpreadPanelPoint(qreal x, qreal y) const;
    Q_INVOKABLE double priceAnchorFraction(qreal y) const;
    Q_INVOKABLE double spreadAnchorFraction(qreal y) const;

  signals:
    void controllerChanged();

  private:
    BookTickerCompareController* controller_{nullptr};
    bool hoverActive_{false};
    QPointF hoverPoint_{};
    bool measureActive_{false};
    bool measureVisible_{false};
    QPointF measureStart_{};
    QPointF measureEnd_{};
};

}  // namespace hftrec::gui::viewer
