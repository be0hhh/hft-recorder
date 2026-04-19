#pragma once

#include <memory>

#include <QPointF>
#include <QQuickPaintedItem>

class QPainter;

namespace hftrec::gui::viewer {

class ChartController;
struct RenderSnapshot;

// CPU QPainter-backed chart. Draws into a QImage target; the scene graph
// composites that image. Simple and portable — no Qt RHI scene-graph geometry
// nodes — accepting the lower framerate in exchange for predictable behaviour
// on any Qt 6 backend (XCB + Wayland + software).
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
    Q_PROPERTY(bool overlayOnly READ overlayOnly WRITE setOverlayOnly NOTIFY overlayOnlyChanged)

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
    bool overlayOnly() const noexcept { return overlayOnly_; }
    void setOverlayOnly(bool value);

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
    void overlayOnlyChanged();

  protected:
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

  private slots:
    void requestRepaint();

  private:
    void updateHover_();
    void invalidateSnapshotCache_();
    const RenderSnapshot& ensureSnapshot_();

    ChartController* controller_{nullptr};
    QPointF hoverPoint_{};
    bool hoverActive_{false};
    bool contextActive_{false};
    int hoveredTradeIndex_{-1};
    int hoveredBookKind_{0};
    std::int64_t hoveredBookPriceE8_{0};
    std::int64_t hoveredBookQtyE8_{0};
    std::int64_t hoveredBookTsNs_{0};
    bool tradesVisible_{true};
    bool orderbookVisible_{false};
    bool bookTickerVisible_{false};
    qreal tradeAmountScale_{0.45};
    qreal bookOpacityGain_{15000.0};
    qreal bookRenderDetail_{5000.0};
    bool interactiveMode_{false};
    bool overlayOnly_{false};

    // Snapshot cache. Invalidated on viewport / session / visibility / tuning
    // / geometry changes. Reused across hover events so mouse-move repaints
    // skip the buildSnapshot rebuild.
    std::unique_ptr<RenderSnapshot> cachedSnap_{};
    qreal cachedW_{0.0};
    qreal cachedH_{0.0};
};

}  // namespace hftrec::gui::viewer
