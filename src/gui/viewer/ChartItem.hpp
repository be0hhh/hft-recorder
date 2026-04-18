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
    Q_PROPERTY(bool orderbookVisible READ orderbookVisible WRITE setOrderbookVisible NOTIFY orderbookVisibleChanged)
    Q_PROPERTY(bool bookTickerVisible READ bookTickerVisible WRITE setBookTickerVisible NOTIFY bookTickerVisibleChanged)
    Q_PROPERTY(qreal tradeAmountScale READ tradeAmountScale WRITE setTradeAmountScale NOTIFY tradeAmountScaleChanged)
    Q_PROPERTY(qreal bookOpacityGain READ bookOpacityGain WRITE setBookOpacityGain NOTIFY bookOpacityGainChanged)
    Q_PROPERTY(qreal bookRenderDetail READ bookRenderDetail WRITE setBookRenderDetail NOTIFY bookRenderDetailChanged)
    Q_PROPERTY(bool interactiveMode READ interactiveMode WRITE setInteractiveMode NOTIFY interactiveModeChanged)

  public:
    explicit ChartItem(QQuickItem* parent = nullptr);
    ~ChartItem() override;

    ChartController* controller() const noexcept { return controller_; }
    void setController(ChartController* c);
    bool tradesVisible() const noexcept { return tradesVisible_; }
    void setTradesVisible(bool value);
    bool orderbookVisible() const noexcept { return orderbookVisible_; }
    void setOrderbookVisible(bool value);
    bool bookTickerVisible() const noexcept { return bookTickerVisible_; }
    void setBookTickerVisible(bool value);
    qreal tradeAmountScale() const noexcept { return tradeAmountScale_; }
    void setTradeAmountScale(qreal value);
    qreal bookOpacityGain() const noexcept { return bookOpacityGain_; }
    void setBookOpacityGain(qreal value);
    qreal bookRenderDetail() const noexcept { return bookRenderDetail_; }
    void setBookRenderDetail(qreal value);
    bool interactiveMode() const noexcept { return interactiveMode_; }
    void setInteractiveMode(bool value);

    void paint(QPainter* painter) override;
    Q_INVOKABLE void setHoverPoint(qreal x, qreal y);
    Q_INVOKABLE void activateContextPoint(qreal x, qreal y);
    Q_INVOKABLE void clearHover();

  signals:
    void controllerChanged();
    void tradesVisibleChanged();
    void orderbookVisibleChanged();
    void bookTickerVisibleChanged();
    void tradeAmountScaleChanged();
    void bookOpacityGainChanged();
    void bookRenderDetailChanged();
    void interactiveModeChanged();

  private slots:
    void requestRepaint();

  private:
    void updateHover_();

    ChartController* controller_{nullptr};
    QPointF hoverPoint_{};
    bool hoverActive_{false};
    bool contextActive_{false};
    int hoveredTradeIndex_{-1};
    int hoveredBookKind_{0};  // 0 none, 1 bid ticker, 2 ask ticker, 3 bid book, 4 ask book
    std::int64_t hoveredBookPriceE8_{0};
    std::int64_t hoveredBookQtyE8_{0};
    std::int64_t hoveredBookTsNs_{0};
    bool tradesVisible_{true};
    bool orderbookVisible_{false};
    bool bookTickerVisible_{false};
    qreal tradeAmountScale_{0.45};
    qreal bookOpacityGain_{0.55};
    qreal bookRenderDetail_{0.7};
    bool interactiveMode_{false};
};

}  // namespace hftrec::gui::viewer
