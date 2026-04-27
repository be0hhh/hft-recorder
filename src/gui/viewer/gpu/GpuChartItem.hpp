#pragma once

#include <cstdint>
#include <memory>

#include <QPointF>
#include <QRectF>
#include <QQuickFramebufferObject>

namespace hftrec::gui::viewer {

class ChartController;
struct RenderSnapshot;
struct HoverInfo;

namespace gpu {

class GpuChartRenderer;

class GpuChartItem : public QQuickFramebufferObject {
    Q_OBJECT
    Q_PROPERTY(hftrec::gui::viewer::ChartController* controller
                   READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(bool tradesVisible READ tradesVisible WRITE setTradesVisible NOTIFY tradesVisibleChanged)
    Q_PROPERTY(bool liquidationsVisible READ liquidationsVisible WRITE setLiquidationsVisible NOTIFY liquidationsVisibleChanged)
    Q_PROPERTY(bool orderbookVisible READ orderbookVisible WRITE setOrderbookVisible NOTIFY orderbookVisibleChanged)
    Q_PROPERTY(bool bookTickerVisible READ bookTickerVisible WRITE setBookTickerVisible NOTIFY bookTickerVisibleChanged)
    Q_PROPERTY(qreal tradeAmountScale READ tradeAmountScale WRITE setTradeAmountScale NOTIFY tradeAmountScaleChanged)
    Q_PROPERTY(qreal bookOpacityGain READ bookOpacityGain WRITE setBookOpacityGain NOTIFY bookOpacityGainChanged)
    Q_PROPERTY(qreal bookRenderDetail READ bookRenderDetail WRITE setBookRenderDetail NOTIFY bookRenderDetailChanged)
    Q_PROPERTY(qreal bookDepthWindowPct READ bookDepthWindowPct WRITE setBookDepthWindowPct NOTIFY bookDepthWindowPctChanged)
    Q_PROPERTY(bool interactiveMode READ interactiveMode WRITE setInteractiveMode NOTIFY interactiveModeChanged)
    Q_PROPERTY(bool overlayOnly READ overlayOnly WRITE setOverlayOnly NOTIFY overlayOnlyChanged)

  public:
    explicit GpuChartItem(QQuickItem* parent = nullptr);
    ~GpuChartItem() override;

    ChartController* controller() const noexcept { return controller_; }
    void setController(ChartController* c);
    bool tradesVisible() const noexcept { return tradesVisible_; }
    void setTradesVisible(bool value);
    bool liquidationsVisible() const noexcept { return liquidationsVisible_; }
    void setLiquidationsVisible(bool value);
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

    Renderer* createRenderer() const override;
    Q_INVOKABLE void setHoverPoint(qreal x, qreal y);
    Q_INVOKABLE void activateContextPoint(qreal x, qreal y);
    Q_INVOKABLE void clearHover();

  signals:
    void controllerChanged();
    void tradesVisibleChanged();
    void liquidationsVisibleChanged();
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

  private:
    friend class GpuChartRenderer;

    bool shouldSkipHoverRecompute_(const QPointF& point, bool contextActive) const noexcept;
    void updateHover_();
    void invalidateSnapshotCache_();
    bool ensureSnapshot_();
    HoverInfo buildHoverInfo_() const;
    RenderSnapshot snapshotCopy_() const;
    HoverInfo hoverInfoCopy_() const;

    ChartController* controller_{nullptr};
    QPointF hoverPoint_{};
    bool hoverActive_{false};
    bool contextActive_{false};
    int hoveredTradeIndex_{-1};
    int hoveredBookKind_{0};
    std::int64_t hoveredBookPriceE8_{0};
    std::int64_t hoveredBookQtyE8_{0};
    std::int64_t hoveredBookTsNs_{0};
    std::int64_t hoveredBookTsStartNs_{0};
    std::int64_t hoveredBookTsEndNs_{0};
    bool tradesVisible_{true};
    bool liquidationsVisible_{true};
    bool orderbookVisible_{false};
    bool bookTickerVisible_{false};
    qreal tradeAmountScale_{0.45};
    qreal bookOpacityGain_{15000.0};
    qreal bookRenderDetail_{5000.0};
    qreal bookDepthWindowPct_{5.0};
    bool interactiveMode_{false};
    bool overlayOnly_{false};

    std::unique_ptr<RenderSnapshot> cachedSnap_{};
    std::unique_ptr<HoverInfo> hoverInfo_{};
    qreal cachedW_{0.0};
    qreal cachedH_{0.0};
    bool snapshotDirty_{true};
};

}  // namespace gpu
}  // namespace hftrec::gui::viewer
