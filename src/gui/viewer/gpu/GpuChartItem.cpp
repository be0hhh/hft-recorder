#include "gui/viewer/gpu/GpuChartItem.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>

#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFramebufferObjectFormat>
#include <QOpenGLFunctions>
#include <QOpenGLPaintDevice>
#include <QOpenGLShaderProgram>
#include <QPainter>
#include <QQuickWindow>
#include <QVector2D>

#include "gui/viewer/ChartController.hpp"
#include "gui/viewer/ColorScheme.hpp"
#include "gui/viewer/RenderContext.hpp"
#include "gui/viewer/RenderSnapshot.hpp"
#include "gui/viewer/detail/Formatters.hpp"
#include "gui/viewer/hit_test/HoverDetection.hpp"
#include "gui/viewer/renderers/BookTickerRenderer.hpp"
#include "gui/viewer/renderers/OverlayRenderer.hpp"
#include "gui/viewer/renderers/TradeRenderer.hpp"

namespace hftrec::gui::viewer::gpu {

namespace {

SnapshotInputs collectInputs(const GpuChartItem& item) {
    return SnapshotInputs{
        item.tradesVisible(),
        item.orderbookVisible(),
        item.bookTickerVisible(),
        item.interactiveMode(),
        item.overlayOnly(),
        true,
        item.tradeAmountScale(),
        item.bookOpacityGain(),
        item.bookRenderDetail(),
        item.bookDepthWindowPct(),
    };
}

}  // namespace

class GpuChartRenderer final : public QQuickFramebufferObject::Renderer {
  public:
    GpuChartRenderer() : vbo_(QOpenGLBuffer::VertexBuffer) {}
    ~GpuChartRenderer() override {
        if (vbo_.isCreated()) vbo_.destroy();
    }

    QOpenGLFramebufferObject* createFramebufferObject(const QSize& size) override {
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        return new QOpenGLFramebufferObject(size, format);
    }

    void synchronize(QQuickFramebufferObject* item) override {
        const auto* gpuItem = static_cast<GpuChartItem*>(item);
        snapshot_ = gpuItem->snapshotCopy_();
        hover_ = gpuItem->hoverInfoCopy_();
        dpr_ = gpuItem->window() ? gpuItem->window()->effectiveDevicePixelRatio() : 1.0;
    }

    void render() override {
        auto* fbo = framebufferObject();
        if (fbo == nullptr) return;
        auto* gl = QOpenGLContext::currentContext() ? QOpenGLContext::currentContext()->functions() : nullptr;
        if (gl == nullptr) return;

        gl->glViewport(0, 0, fbo->width(), fbo->height());
        gl->glDisable(GL_DEPTH_TEST);
        gl->glDisable(GL_CULL_FACE);
        gl->glDisable(GL_SCISSOR_TEST);
        gl->glEnable(GL_BLEND);
        gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        const auto bg = bgColor();
        const float clearAlpha = snapshot_.overlayOnly ? 0.0F : 1.0F;
        gl->glClearColor(
            static_cast<float>(bg.redF()),
            static_cast<float>(bg.greenF()),
            static_cast<float>(bg.blueF()),
            clearAlpha);
        gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        renderOrderbook_();

        QOpenGLPaintDevice device(fbo->size());
        QPainter painter(&device);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        if (dpr_ > 0.0) {
            // EN: QOpenGLPaintDevice on the FBO uses OpenGL-style bottom-left
            // origin, while the CPU path and the rest of the viewer use Qt's
            // top-left coordinate system. Flip Y once here so all renderers
            // keep the same math in both paths.
            // RU: В FBO через QOpenGLPaintDevice ось Y идёт как в OpenGL
            // (origin снизу), а остальной viewer считает координаты как Qt
            // (origin сверху). Один раз переворачиваем Y здесь, чтобы GPU и
            // CPU пути использовали одинаковую геометрию.
            painter.scale(dpr_, -dpr_);
            painter.translate(0.0, -snapshot_.vp.h);
        }

        if (!snapshot_.loaded) {
            painter.setPen(axisTextColor());
            painter.drawText(QRectF{8, 8, snapshot_.vp.w - 16, 24},
                             Qt::AlignLeft | Qt::AlignVCenter,
                             QStringLiteral("Pick a session, then load Trades."));
        } else if (snapshot_.vp.tMax > snapshot_.vp.tMin && snapshot_.vp.pMax > snapshot_.vp.pMin) {
            RenderContext ctx{&painter, snapshot_, hover_, dpr_};
            renderers::renderBookTicker(ctx);
            renderers::renderTrades(ctx);
            renderers::renderOverlay(ctx);
        }

        painter.end();
    }

  private:
    void ensureGlResources_() {
        if (program_ && vbo_.isCreated()) return;

        auto program = std::make_unique<QOpenGLShaderProgram>();
        // EN: Orderbook geometry is prepacked in logical Qt pixel space.
        // The shader converts those logical coordinates to clip space so the
        // GL pass can render the dense book layer in one batched draw. The Y
        // axis is intentionally inverted here to match the user's expected
        // vertical orientation for the OpenGL orderbook layer.
        // RU: Геометрия книги заранее упакована в логических Qt-координатах.
        // Шейдер переводит их в clip-space, чтобы плотный слой orderbook
        // рисовался одним батчем без QPainter drawLine на каждый уровень.
        // Ось Y здесь специально перевёрнута, чтобы GL-слой orderbook имел
        // ожидаемую вертикальную ориентацию.
        static constexpr const char* kVertexShader = R"(#version 330
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec4 aColor;
uniform vec2 uViewportSize;
out vec4 vColor;
void main() {
    vec2 clip = vec2(
        (aPosition.x / uViewportSize.x) * 2.0 - 1.0,
        (aPosition.y / uViewportSize.y) * 2.0 - 1.0);
    gl_Position = vec4(clip, 0.0, 1.0);
    vColor = aColor;
}
)";
        static constexpr const char* kFragmentShader = R"(#version 330
in vec4 vColor;
out vec4 fragColor;
void main() {
    fragColor = vColor;
}
)";
        if (!program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader)) return;
        if (!program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader)) return;
        if (!program->link()) return;

        if (!vbo_.isCreated()) {
            if (!vbo_.create()) return;
            vbo_.setUsagePattern(QOpenGLBuffer::DynamicDraw);
        }

        program_ = std::move(program);
    }

    void renderOrderbook_() {
        if (snapshot_.overlayOnly) return;
        if (!snapshot_.loaded || !snapshot_.orderbookVisible || snapshot_.gpuBookVertices.empty()) return;
        if (snapshot_.vp.w <= 0.0 || snapshot_.vp.h <= 0.0) return;

        ensureGlResources_();
        if (!program_ || !vbo_.isCreated()) return;

        auto* gl = QOpenGLContext::currentContext() ? QOpenGLContext::currentContext()->functions() : nullptr;
        if (gl == nullptr) return;

        program_->bind();
        program_->setUniformValue("uViewportSize", QVector2D{
            static_cast<float>(snapshot_.vp.w),
            static_cast<float>(snapshot_.vp.h),
        });

        vbo_.bind();
        vbo_.allocate(
            snapshot_.gpuBookVertices.data(),
            static_cast<int>(snapshot_.gpuBookVertices.size() * sizeof(GpuBookLineVertex)));

        gl->glEnableVertexAttribArray(0);
        gl->glEnableVertexAttribArray(1);
        gl->glVertexAttribPointer(
            0,
            2,
            GL_FLOAT,
            GL_FALSE,
            sizeof(GpuBookLineVertex),
            reinterpret_cast<const void*>(offsetof(GpuBookLineVertex, x)));
        gl->glVertexAttribPointer(
            1,
            4,
            GL_UNSIGNED_BYTE,
            GL_TRUE,
            sizeof(GpuBookLineVertex),
            reinterpret_cast<const void*>(offsetof(GpuBookLineVertex, r)));
        gl->glLineWidth(1.0F);
        gl->glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(snapshot_.gpuBookVertices.size()));
        gl->glDisableVertexAttribArray(0);
        gl->glDisableVertexAttribArray(1);
        vbo_.release();
        program_->release();
    }

    RenderSnapshot snapshot_{};
    HoverInfo hover_{};
    double dpr_{1.0};
    std::unique_ptr<QOpenGLShaderProgram> program_{};
    QOpenGLBuffer vbo_;
};

GpuChartItem::GpuChartItem(QQuickItem* parent) : QQuickFramebufferObject(parent) {
    setFlag(ItemHasContents, true);
}

GpuChartItem::~GpuChartItem() = default;

void GpuChartItem::setController(ChartController* c) {
    if (controller_ == c) return;
    if (controller_) disconnect(controller_, nullptr, this, nullptr);
    controller_ = c;
    if (controller_) {
        connect(controller_, &ChartController::viewportChanged, this, &GpuChartItem::requestRepaint);
        connect(controller_, &ChartController::sessionChanged, this, &GpuChartItem::requestRepaint);
    }
    invalidateSnapshotCache_();
    ensureSnapshot_();
    emit controllerChanged();
    update();
}

void GpuChartItem::setTradesVisible(bool value) {
    if (tradesVisible_ == value) return;
    tradesVisible_ = value;
    if (!tradesVisible_) clearHover();
    invalidateSnapshotCache_();
    ensureSnapshot_();
    emit tradesVisibleChanged();
    update();
}

void GpuChartItem::setOrderbookVisible(bool value) {
    if (orderbookVisible_ == value) return;
    orderbookVisible_ = value;
    invalidateSnapshotCache_();
    ensureSnapshot_();
    updateHover_();
    emit orderbookVisibleChanged();
    update();
}

void GpuChartItem::setBookTickerVisible(bool value) {
    if (bookTickerVisible_ == value) return;
    bookTickerVisible_ = value;
    invalidateSnapshotCache_();
    ensureSnapshot_();
    updateHover_();
    emit bookTickerVisibleChanged();
    update();
}

void GpuChartItem::setTradeAmountScale(qreal value) {
    value = detail::clampReal(value, 0.0, 1.0);
    if (qFuzzyCompare(tradeAmountScale_ + 1.0, value + 1.0)) return;
    tradeAmountScale_ = value;
    invalidateSnapshotCache_();
    ensureSnapshot_();
    emit tradeAmountScaleChanged();
    update();
}

void GpuChartItem::setBookOpacityGain(qreal value) {
    value = std::clamp<qreal>(value, 1000.0, 1000000.0);
    if (qFuzzyCompare(bookOpacityGain_ + 1.0, value + 1.0)) return;
    bookOpacityGain_ = value;
    invalidateSnapshotCache_();
    ensureSnapshot_();
    emit bookOpacityGainChanged();
    update();
}

void GpuChartItem::setBookRenderDetail(qreal value) {
    value = std::clamp<qreal>(value, 1000.0, 1000000.0);
    if (qFuzzyCompare(bookRenderDetail_ + 1.0, value + 1.0)) return;
    bookRenderDetail_ = value;
    invalidateSnapshotCache_();
    ensureSnapshot_();
    emit bookRenderDetailChanged();
    update();
}

void GpuChartItem::setBookDepthWindowPct(qreal value) {
    value = std::clamp<qreal>(value, 1.0, 25.0);
    if (qFuzzyCompare(bookDepthWindowPct_ + 1.0, value + 1.0)) return;
    bookDepthWindowPct_ = value;
    invalidateSnapshotCache_();
    ensureSnapshot_();
    updateHover_();
    emit bookDepthWindowPctChanged();
    update();
}

void GpuChartItem::setInteractiveMode(bool value) {
    if (interactiveMode_ == value) return;
    interactiveMode_ = value;
    invalidateSnapshotCache_();
    ensureSnapshot_();
    emit interactiveModeChanged();
    update();
}

void GpuChartItem::setOverlayOnly(bool value) {
    if (overlayOnly_ == value) return;
    overlayOnly_ = value;
    invalidateSnapshotCache_();
    ensureSnapshot_();
    emit overlayOnlyChanged();
    update();
}

QQuickFramebufferObject::Renderer* GpuChartItem::createRenderer() const {
    return new GpuChartRenderer{};
}

bool GpuChartItem::shouldSkipHoverRecompute_(const QPointF& point, bool contextActive) const noexcept {
    if (!hoverActive_ || contextActive_ != contextActive) return false;
    const qreal dx = point.x() - hoverPoint_.x();
    const qreal dy = point.y() - hoverPoint_.y();
    return (dx * dx + dy * dy) < 0.25;
}

void GpuChartItem::setHoverPoint(qreal x, qreal y) {
    if (shouldSkipHoverRecompute_(QPointF{x, y}, false)) return;
    hoverPoint_ = QPointF{x, y};
    hoverActive_ = true;
    contextActive_ = false;
    updateHover_();
    update();
}

void GpuChartItem::activateContextPoint(qreal x, qreal y) {
    if (shouldSkipHoverRecompute_(QPointF{x, y}, true)) return;
    hoverPoint_ = QPointF{x, y};
    hoverActive_ = true;
    contextActive_ = true;
    updateHover_();
    update();
}

void GpuChartItem::clearHover() {
    hoverActive_ = false;
    contextActive_ = false;
    hoveredTradeIndex_ = -1;
    hoveredBookKind_ = 0;
    hoveredBookPriceE8_ = 0;
    hoveredBookQtyE8_ = 0;
    hoveredBookTsNs_ = 0;
    hoverInfo_ = std::make_unique<HoverInfo>();
    update();
}

void GpuChartItem::updateHover_() {
    hoveredTradeIndex_ = -1;
    hoveredBookKind_ = 0;
    hoveredBookPriceE8_ = 0;
    hoveredBookQtyE8_ = 0;
    hoveredBookTsNs_ = 0;
    hoverInfo_ = std::make_unique<HoverInfo>();
    if (!hoverActive_ || !controller_ || !controller_->loaded() || width() <= 0 || height() <= 0) return;

    if (!ensureSnapshot_()) return;
    if (!cachedSnap_ || !cachedSnap_->loaded) return;

    HoverInfo hover{};
    hit_test::computeHover(*cachedSnap_, hoverPoint_, contextActive_, hover);
    hoveredTradeIndex_ = hover.tradeHit ? hover.tradeOrigIndex : -1;
    hoveredBookKind_ = hover.bookKind;
    hoveredBookPriceE8_ = hover.bookPriceE8;
    hoveredBookQtyE8_ = hover.bookQtyE8;
    hoveredBookTsNs_ = hover.bookTsNs;
    *hoverInfo_ = buildHoverInfo_();
}

void GpuChartItem::requestRepaint() {
    invalidateSnapshotCache_();
    ensureSnapshot_();
    updateHover_();
    update();
}

void GpuChartItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickFramebufferObject::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        invalidateSnapshotCache_();
        ensureSnapshot_();
        updateHover_();
        update();
    }
}

void GpuChartItem::invalidateSnapshotCache_() {
    cachedSnap_.reset();
    snapshotDirty_ = true;
}

bool GpuChartItem::ensureSnapshot_() {
    if (!controller_ || width() <= 0.0 || height() <= 0.0) return false;
    const qreal w = width();
    const qreal h = height();
    if (snapshotDirty_ || !cachedSnap_ || cachedW_ != w || cachedH_ != h) {
        cachedSnap_ = std::make_unique<RenderSnapshot>(
            controller_->buildSnapshot(w, h, collectInputs(*this)));
        cachedW_ = w;
        cachedH_ = h;
        snapshotDirty_ = false;
    }
    if (!hoverInfo_) hoverInfo_ = std::make_unique<HoverInfo>(buildHoverInfo_());
    return cachedSnap_ != nullptr;
}

HoverInfo GpuChartItem::buildHoverInfo_() const {
    HoverInfo hover{};
    hover.active = hoverActive_;
    hover.contextActive = contextActive_;
    hover.point = hoverPoint_;
    hover.bookKind = hoveredBookKind_;
    hover.bookPriceE8 = hoveredBookPriceE8_;
    hover.bookQtyE8 = hoveredBookQtyE8_;
    hover.bookTsNs = hoveredBookTsNs_;

    if (hoveredTradeIndex_ < 0 || controller_ == nullptr) {
        return hover;
    }

    const auto& trades = controller_->replay().trades();
    if (hoveredTradeIndex_ >= static_cast<int>(trades.size())) {
        return hover;
    }

    const auto& trade = trades[static_cast<std::size_t>(hoveredTradeIndex_)];
    hover.tradeHit = true;
    hover.tradeOrigIndex = hoveredTradeIndex_;
    hover.tradeTsNs = trade.tsNs;
    hover.tradePriceE8 = trade.priceE8;
    hover.tradeQtyE8 = trade.qtyE8;
    hover.tradeSideBuy = trade.sideBuy;
    return hover;
}

RenderSnapshot GpuChartItem::snapshotCopy_() const {
    return cachedSnap_ ? *cachedSnap_ : RenderSnapshot{};
}

HoverInfo GpuChartItem::hoverInfoCopy_() const {
    return hoverInfo_ ? *hoverInfo_ : HoverInfo{};
}

}  // namespace hftrec::gui::viewer::gpu
