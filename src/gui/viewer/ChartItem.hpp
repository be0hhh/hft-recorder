#pragma once

#include <cstdint>
#include <memory>

#include <QImage>
#include <QPointF>
#include <QQuickPaintedItem>

#include <core/replay/EventRows.hpp>

class QPainter;

namespace hftrec::gui::viewer {

class ChartController;
struct RenderSnapshot;
struct SnapshotInputs;
struct HoverInfo;
class ChartItem;

namespace detail {
SnapshotInputs collectInputs(const ChartItem& item);
HoverInfo buildHoverInfo(const ChartItem& item);
}

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
    Q_PROPERTY(qreal bookDepthWindowPct READ bookDepthWindowPct WRITE setBookDepthWindowPct NOTIFY bookDepthWindowPctChanged)
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
    qreal bookDepthWindowPct() const noexcept { return bookDepthWindowPct_; }
    void setBookDepthWindowPct(qreal value);
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
    void bookDepthWindowPctChanged();
    void interactiveModeChanged();
    void overlayOnlyChanged();

  protected:
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

  private slots:
    void requestRepaint();
    void requestLiveRepaint();
    void requestSessionRepaint();

  private:
    friend SnapshotInputs detail::collectInputs(const ChartItem& item);
    friend HoverInfo detail::buildHoverInfo(const ChartItem& item);

    void updateHover_();
    void invalidateSnapshotCache_();
    void invalidateBaseImage_();
    const RenderSnapshot& ensureSnapshot_();
    std::unique_ptr<RenderSnapshot>& activeSnapshotCache_() noexcept;
    void mergeLiveSnapshotIntoBaseImage_();
    void ensureLayerImages_(const RenderSnapshot& snap, qreal w, qreal h);
    bool shouldSkipHoverRecompute_(const QPointF& point, bool contextActive) const noexcept;

    ChartController* controller_{nullptr};
    QPointF hoverPoint_{};
    bool hoverActive_{false};
    bool contextActive_{false};
    int hoveredTradeIndex_{-1};
    std::int64_t hoveredTradeTsNs_{0};
    std::int64_t hoveredTradePriceE8_{0};
    std::int64_t hoveredTradeQtyE8_{0};
    bool hoveredTradeSideBuy_{true};
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
    qreal bookDepthWindowPct_{5.0};
    bool interactiveMode_{false};
    bool overlayOnly_{false};

    // Dual snapshot caches: an approximate interactive snapshot and an exact
    // settled snapshot. Hover repaints reuse whichever cache matches the
    // current mode without rebuilding.
    std::unique_ptr<RenderSnapshot> cachedInteractiveSnap_{};
    std::unique_ptr<RenderSnapshot> cachedExactSnap_{};
    std::unique_ptr<RenderSnapshot> cachedLiveSnap_{};
    std::unique_ptr<RenderSnapshot> cachedHitTestSnap_{};
    qreal cachedW_{0.0};
    qreal cachedH_{0.0};
    bool interactiveDirty_{false};
    bool exactDirty_{false};
    std::uint64_t cachedLiveDataBatchId_{0};
    std::uint64_t cachedHitTestBatchId_{0};
    // Heavy historical layers are cached here. The latest event is painted as
    // a live overlay, then folded into these images when the next live batch
    // arrives, so active captures avoid repainting the same history twice.
    QImage cachedOrderbookImage_{};
    QImage cachedBookTickerImage_{};
    QImage cachedTradesImage_{};
    qreal cachedLayerImageW_{0.0};
    qreal cachedLayerImageH_{0.0};
    std::int64_t cachedOrderbookEndTsNs_{0};
    std::int64_t cachedBookTickerEndTsNs_{0};
    std::int64_t cachedTradesEndTsNs_{0};
};

}  // namespace hftrec::gui::viewer

