#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>


#include "core/replay/SessionReplay.hpp"
#include "gui/viewer/RenderSnapshot.hpp"

namespace hftrec::gui::viewer {

class ChartController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString sessionDir READ sessionDir NOTIFY sessionChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY sessionChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)

    Q_PROPERTY(qint64 firstTsNs READ firstTsNs NOTIFY sessionChanged)
    Q_PROPERTY(qint64 lastTsNs READ lastTsNs NOTIFY sessionChanged)
    Q_PROPERTY(qint64 tsMin READ tsMin NOTIFY viewportChanged)
    Q_PROPERTY(qint64 tsMax READ tsMax NOTIFY viewportChanged)
    Q_PROPERTY(qint64 priceMinE8 READ priceMinE8 NOTIFY viewportChanged)
    Q_PROPERTY(qint64 priceMaxE8 READ priceMaxE8 NOTIFY viewportChanged)

    Q_PROPERTY(bool hasTrades READ hasTrades NOTIFY sessionChanged)
    Q_PROPERTY(bool hasBookTicker READ hasBookTicker NOTIFY sessionChanged)
    Q_PROPERTY(bool hasOrderbook READ hasOrderbook NOTIFY sessionChanged)
    Q_PROPERTY(bool gpuRendererAvailable READ gpuRendererAvailable CONSTANT)
    Q_PROPERTY(int tradeCount READ tradeCount NOTIFY sessionChanged)
    Q_PROPERTY(int depthCount READ depthCount NOTIFY sessionChanged)
    Q_PROPERTY(bool selectionActive READ selectionActive NOTIFY selectionChanged)
    Q_PROPERTY(QString selectionSummaryText READ selectionSummaryText NOTIFY selectionChanged)

  public:
    struct LiveJsonBatch {
        std::uint64_t id{0};
        std::vector<hftrec::replay::TradeRow> trades{};
        std::vector<hftrec::replay::BookTickerRow> bookTickers{};
        std::vector<hftrec::replay::DepthRow> depths{};
    };

    struct LiveJsonCache {
        std::vector<hftrec::replay::TradeRow> allTrades{};
        std::vector<hftrec::replay::BookTickerRow> allBookTickers{};
        std::vector<hftrec::replay::DepthRow> allDepths{};
        LiveJsonBatch lastBatch{};
        std::uint64_t version{0};
    };

    explicit ChartController(QObject* parent = nullptr);

    QString sessionDir() const { return sessionDir_; }
    bool loaded() const { return loaded_; }
    QString statusText() const { return statusText_; }

    qint64 firstTsNs() const { return replay_.firstTsNs(); }
    qint64 lastTsNs() const { return replay_.lastTsNs(); }
    qint64 tsMin() const { return tsMin_; }
    qint64 tsMax() const { return tsMax_; }
    qint64 priceMinE8() const { return priceMinE8_; }
    qint64 priceMaxE8() const { return priceMaxE8_; }
    bool hasTrades() const noexcept { return !replay_.trades().empty(); }
    bool hasBookTicker() const noexcept { return !replay_.bookTickers().empty(); }
    bool hasOrderbook() const noexcept {
        return !replay_.depths().empty() || !replay_.book().bids().empty() || !replay_.book().asks().empty();
    }
    bool gpuRendererAvailable() const noexcept { return gpuRendererAvailable_; }

    int tradeCount() const { return static_cast<int>(replay_.trades().size()); }
    int depthCount() const { return static_cast<int>(replay_.depths().size()); }
    bool selectionActive() const noexcept { return selectionActive_; }
    QString selectionSummaryText() const { return selectionSummaryText_; }

    Q_INVOKABLE bool loadSession(const QString& dir);

    Q_INVOKABLE void resetSession();
    Q_INVOKABLE bool addTradesFile(const QString& path);
    Q_INVOKABLE bool addBookTickerFile(const QString& path);
    Q_INVOKABLE bool addDepthFile(const QString& path);
    Q_INVOKABLE bool addSnapshotFile(const QString& path);
    Q_INVOKABLE void finalizeFiles();
    Q_INVOKABLE void setLiveUpdateIntervalMs(int intervalMs);
    Q_INVOKABLE int liveUpdateIntervalMs() const noexcept;

    Q_INVOKABLE void setViewport(qint64 tsMin, qint64 tsMax,
                                 qint64 priceMinE8, qint64 priceMaxE8);
    Q_INVOKABLE void panTime(double fraction);
    Q_INVOKABLE void panPrice(double fraction);
    Q_INVOKABLE void zoomTime(double factor);
    Q_INVOKABLE void zoomPrice(double factor);
    Q_INVOKABLE void autoFit();
    Q_INVOKABLE void jumpToStart();
    Q_INVOKABLE void jumpToEnd();
    Q_INVOKABLE QString formatPriceAt(double ratio) const;
    Q_INVOKABLE QString formatTimeAt(double ratio) const;
    Q_INVOKABLE QVariantList priceScaleTicks(int tickCount) const;
    Q_INVOKABLE QVariantList timeScaleTicks(int tickCount) const;
    Q_INVOKABLE QString formatPriceScaleLabel(int index, int tickCount) const;
    Q_INVOKABLE QString formatTimeScaleLabel(int index, int tickCount) const;
    Q_INVOKABLE bool commitSelectionRect(qreal plotWidthPx, qreal plotHeightPx,
                                         qreal x0, qreal y0, qreal x1, qreal y1);
    Q_INVOKABLE void clearSelection();

    void syncReplayCursorToViewport();
    std::int64_t viewportCursorTs() const noexcept;
    const hftrec::replay::BookTickerRow* currentBookTicker() const noexcept;
    const LiveJsonCache& liveJsonCache() const noexcept { return liveJsonCache_; }

    // Builds a POD snapshot of what renderers need to draw one frame. Mutates
    // replay cursor internally (advances through events in the viewport), but
    // leaves no Qt-thread-visible state the renderers then observe.
    RenderSnapshot buildSnapshot(qreal widthPx, qreal heightPx, const SnapshotInputs& in);

    hftrec::replay::SessionReplay& replay() noexcept { return replay_; }
    const hftrec::replay::SessionReplay& replay() const noexcept { return replay_; }

  signals:
    void sessionChanged();
    void liveDataChanged();
    void viewportChanged();
    void statusChanged();
    void selectionChanged();

  private:
    struct SelectionRange {
        bool valid{false};
        std::int64_t timeStartNs{0};
        std::int64_t timeEndNs{0};
        std::int64_t priceMinE8{0};
        std::int64_t priceMaxE8{0};
    };

    struct SelectionSummary {
        std::int64_t durationUs{0};
        std::int64_t tradeCount{0};
        std::int64_t buyQtyE8{0};
        std::int64_t sellQtyE8{0};
        std::int64_t buyNotionalE8{0};
        std::int64_t sellNotionalE8{0};
        std::int64_t bookTickerCount{0};
        std::int64_t depthEventCount{0};
        std::int64_t bidLevelUpdates{0};
        std::int64_t askLevelUpdates{0};
        std::int64_t bidRemovals{0};
        std::int64_t askRemovals{0};
        std::int64_t bidQtyUpdatedE8{0};
        std::int64_t askQtyUpdatedE8{0};
        bool hasMovePct{false};
        std::int64_t movePctE8{0};
        bool hasBookStart{false};
        std::int64_t bestBidStartE8{0};
        std::int64_t bestAskStartE8{0};
        std::int64_t spreadStartE8{0};
        bool hasBookEnd{false};
        std::int64_t bestBidEndE8{0};
        std::int64_t bestAskEndE8{0};
        std::int64_t spreadEndE8{0};
        bool hasSpreadMin{false};
        std::int64_t spreadMinE8{0};
        bool hasSpreadMax{false};
        std::int64_t spreadMaxE8{0};
        bool hasBestBidMax{false};
        std::int64_t bestBidMaxE8{0};
        bool hasBestAskMin{false};
        std::int64_t bestAskMinE8{0};
    };

    void computeInitialViewport_();
    void startLiveTail_(const std::filesystem::path& sessionDir);
    void stopLiveTail_() noexcept;
    void pollLiveTail_();
    void clearLiveJsonCache_() noexcept;
    void markUserViewportControl_() noexcept;
    SelectionRange selectionFromRect_(qreal plotWidthPx, qreal plotHeightPx,
                                      qreal x0, qreal y0, qreal x1, qreal y1) const noexcept;
    SelectionSummary buildSelectionSummary_(const SelectionRange& range);
    QString formatSelectionSummary_(const SelectionRange& range, const SelectionSummary& summary) const;

    hftrec::replay::SessionReplay replay_{};
    QString sessionDir_{};
    QString statusText_{"No session loaded"};
    bool loaded_{false};
    struct LiveTailFile {
        std::filesystem::path path{};
        std::uintmax_t offset{0};
        std::string pending{};
    };

    QTimer* liveTailTimer_{nullptr};
    LiveTailFile liveTrades_{};
    LiveTailFile liveBookTicker_{};
    LiveTailFile liveDepth_{};
    std::filesystem::path liveSnapshotPath_{};
    bool liveSnapshotLoaded_{false};
    bool liveFollowEdge_{true};
    bool liveOrderbookHealthy_{true};
    int liveUpdateIntervalMs_{100};
    LiveJsonCache liveJsonCache_{};
    std::uint64_t liveJsonBatchSeq_{0};

    qint64 tsMin_{0};
    qint64 tsMax_{0};
    qint64 priceMinE8_{0};
    qint64 priceMaxE8_{0};
    std::int64_t currentBookTickerIndex_{-1};
    bool gpuRendererAvailable_{true};
    bool selectionActive_{false};
    QString selectionSummaryText_{};
};

}  // namespace hftrec::gui::viewer
