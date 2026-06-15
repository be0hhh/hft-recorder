#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/replay/SessionReplay.hpp"
#include "gui/viewer/LiveDataProvider.hpp"
#include "gui/viewer/RenderSnapshot.hpp"
#include "gui/viewer/StrategyIndicator.hpp"

namespace hftrec::gui::viewer {

class ChartController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString sessionDir READ sessionDir NOTIFY sessionChanged)
    Q_PROPERTY(QString currentSourceId READ currentSourceId NOTIFY sessionChanged)
    Q_PROPERTY(QString currentSourceKind READ currentSourceKind NOTIFY sessionChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY sessionChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)

    Q_PROPERTY(qint64 firstTsNs READ firstTsNs NOTIFY sessionChanged)
    Q_PROPERTY(qint64 lastTsNs READ lastTsNs NOTIFY sessionChanged)
    Q_PROPERTY(qint64 tsMin READ tsMin NOTIFY viewportChanged)
    Q_PROPERTY(qint64 tsMax READ tsMax NOTIFY viewportChanged)
    Q_PROPERTY(qint64 priceMinE8 READ priceMinE8 NOTIFY viewportChanged)
    Q_PROPERTY(qint64 priceMaxE8 READ priceMaxE8 NOTIFY viewportChanged)

    Q_PROPERTY(bool hasTrades READ hasTrades NOTIFY sessionChanged)
    Q_PROPERTY(bool hasLiquidations READ hasLiquidations NOTIFY sessionChanged)
    Q_PROPERTY(bool hasCandles READ hasCandles NOTIFY sessionChanged)
    Q_PROPERTY(bool hasBookTicker READ hasBookTicker NOTIFY sessionChanged)
    Q_PROPERTY(bool hasOrderbook READ hasOrderbook NOTIFY sessionChanged)
    Q_PROPERTY(bool hasMarkPrice READ hasMarkPrice NOTIFY sessionChanged)
    Q_PROPERTY(bool hasIndexPrice READ hasIndexPrice NOTIFY sessionChanged)
    Q_PROPERTY(bool hasFunding READ hasFunding NOTIFY sessionChanged)
    Q_PROPERTY(bool hasPriceLimit READ hasPriceLimit NOTIFY sessionChanged)
    Q_PROPERTY(bool gpuRendererAvailable READ gpuRendererAvailable CONSTANT)
    Q_PROPERTY(int tradeCount READ tradeCount NOTIFY sessionChanged)
    Q_PROPERTY(int candleCount READ candleCount NOTIFY sessionChanged)
    Q_PROPERTY(int depthCount READ depthCount NOTIFY sessionChanged)
    Q_PROPERTY(bool selectionActive READ selectionActive NOTIFY selectionChanged)
    Q_PROPERTY(QString selectionSummaryText READ selectionSummaryText NOTIFY selectionChanged)
    Q_PROPERTY(int verticalMarkerCount READ verticalMarkerCount NOTIFY markersChanged)
    Q_PROPERTY(int renderWindowSeconds READ renderWindowSeconds WRITE setRenderWindowSeconds NOTIFY renderWindowChanged)
    Q_PROPERTY(QVariantList backtestResults READ backtestResults NOTIFY backtestResultsChanged)
    Q_PROPERTY(QString selectedBacktestResult READ selectedBacktestResult NOTIFY backtestResultChanged)
    Q_PROPERTY(bool hasStrategyIndicator READ hasStrategyIndicator NOTIFY backtestResultChanged)

  public:
    explicit ChartController(QObject* parent = nullptr);
    ~ChartController() override;

    static ChartController* activeInstance() noexcept;

    QString sessionDir() const { return sessionDir_; }
    QString currentSourceId() const { return currentSourceId_; }
    QString currentSourceKind() const { return currentSourceKind_; }
    bool loaded() const { return loaded_; }
    QString statusText() const { return statusText_; }
    bool active() const noexcept { return active_; }

    qint64 firstTsNs() const { return replay_.firstTsNs(); }
    qint64 lastTsNs() const { return replay_.lastTsNs(); }
    qint64 tsMin() const { return tsMin_; }
    qint64 tsMax() const { return tsMax_; }
    qint64 priceMinE8() const { return priceMinE8_; }
    qint64 priceMaxE8() const { return priceMaxE8_; }
    bool hasTrades() const noexcept {
        return !replay_.trades().empty()
            || !liveDataCache_.stableRows.trades.empty()
            || !liveDataCache_.overlayRows.trades.empty()
            || !liveOverlayState_.trades.empty();
    }
    bool hasLiquidations() const noexcept {
        return !replay_.liquidations().empty()
            || !liveDataCache_.stableRows.liquidations.empty()
            || !liveDataCache_.overlayRows.liquidations.empty()
            || !liveOverlayState_.liquidations.empty();
    }
    bool hasCandles() const noexcept { return !replay_.candles().empty(); }
    bool hasBookTicker() const noexcept {
        return !replay_.bookTickers().empty()
            || !liveDataCache_.stableRows.bookTickers.empty()
            || !liveDataCache_.overlayRows.bookTickers.empty()
            || !liveOverlayState_.bookTickers.empty();
    }
    bool hasOrderbook() const noexcept {
        return !replay_.depths().empty()
            || !replay_.book().empty()
            || !liveDataCache_.stableRows.depths.empty()
            || !liveDataCache_.overlayRows.depths.empty()
            || !liveOverlayState_.depths.empty();
    }
    bool hasMarkPrice() const noexcept {
        return !replay_.markPrices().empty()
            || !liveDataCache_.stableRows.markPrices.empty()
            || !liveDataCache_.overlayRows.markPrices.empty()
            || !liveOverlayState_.markPrices.empty();
    }
    bool hasIndexPrice() const noexcept {
        return !replay_.indexPrices().empty()
            || !liveDataCache_.stableRows.indexPrices.empty()
            || !liveDataCache_.overlayRows.indexPrices.empty()
            || !liveOverlayState_.indexPrices.empty();
    }
    bool hasFunding() const noexcept {
        return !replay_.fundings().empty()
            || !liveDataCache_.stableRows.fundings.empty()
            || !liveDataCache_.overlayRows.fundings.empty()
            || !liveOverlayState_.fundings.empty();
    }
    bool hasPriceLimit() const noexcept {
        return !replay_.priceLimits().empty()
            || !liveDataCache_.stableRows.priceLimits.empty()
            || !liveDataCache_.overlayRows.priceLimits.empty()
            || !liveOverlayState_.priceLimits.empty();
    }
    bool gpuRendererAvailable() const noexcept { return gpuRendererAvailable_; }

    int tradeCount() const { return static_cast<int>(replay_.trades().size()); }
    int candleCount() const { return static_cast<int>(replay_.candles().size()); }
    int depthCount() const { return static_cast<int>(replay_.depths().size()); }
    bool selectionActive() const noexcept { return selectionActive_; }
    QString selectionSummaryText() const { return selectionSummaryText_; }
    int verticalMarkerCount() const { return static_cast<int>(verticalMarkers_.size()); }
    int renderWindowSeconds() const noexcept { return renderWindowSeconds_; }
    bool renderWindowActive() const noexcept { return renderWindowSeconds_ != 0; }
    QVariantList backtestResults() const { return backtestResults_; }
    QString selectedBacktestResult() const { return selectedBacktestResult_; }
    bool hasStrategyIndicator() const noexcept { return !strategyIndicator_.empty(); }

    Q_INVOKABLE bool loadSession(const QString& dir);
    Q_INVOKABLE void setActive(bool active);
    Q_INVOKABLE bool activateLiveSource(const QString& sourceId, const QString& sessionPath = QString{});
    Q_INVOKABLE void activateLiveOnlyMode();
    Q_INVOKABLE void resetSession();
    Q_INVOKABLE bool addTradesFile(const QString& path);
    Q_INVOKABLE bool addLiquidationsFile(const QString& path);
    Q_INVOKABLE bool addCandlesFile(const QString& path);
    Q_INVOKABLE bool addBookTickerFile(const QString& path);
    Q_INVOKABLE bool addDepthFile(const QString& path);
    Q_INVOKABLE bool addSnapshotFile(const QString& path);
    Q_INVOKABLE void finalizeFiles();
    Q_INVOKABLE void setLiveUpdateIntervalMs(int intervalMs);
    Q_INVOKABLE int liveUpdateIntervalMs() const noexcept;
    Q_INVOKABLE QString performanceDiagnostics() const;
    void setLiveDataProvider(std::unique_ptr<ILiveDataProvider> provider);

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
    Q_INVOKABLE bool measureSelectionRect(qreal plotWidthPx, qreal plotHeightPx,
                                          qreal x0, qreal y0, qreal x1, qreal y1);
    Q_INVOKABLE bool measureTradeHighLowRect(qreal plotWidthPx, qreal plotHeightPx,
                                             qreal x0, qreal y0, qreal x1, qreal y1);
    Q_INVOKABLE bool measurePointDistance(qreal plotWidthPx, qreal plotHeightPx,
                                          qreal x0, qreal y0, qreal x1, qreal y1);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool addVerticalMarker(qint64 tsNs, const QString& label = QString{});
    Q_INVOKABLE void clearVerticalMarkers();
    Q_INVOKABLE void setRenderWindowSeconds(int seconds);
    Q_INVOKABLE void refreshBacktestResults(const QString& primarySessionPath, const QString& secondarySessionPath = QString{});
    Q_INVOKABLE bool selectBacktestResult(const QString& resultPath);
    Q_INVOKABLE void clearBacktestResult();

    void syncReplayCursorToViewport();
    std::int64_t viewportCursorTs() const noexcept;
    const hftrec::replay::BookTickerRow* currentBookTicker() const noexcept;
    const LiveDataCache& liveDataCache() const noexcept { return liveDataCache_; }
    void refreshLiveDataWindow(std::int64_t tsMin, std::int64_t tsMax);

    RenderSnapshot buildSnapshot(qreal widthPx, qreal heightPx, const SnapshotInputs& in);

    const StrategyIndicatorData& strategyIndicator() const noexcept { return strategyIndicator_; }

    hftrec::replay::SessionReplay& replay() noexcept { return replay_; }
    const hftrec::replay::SessionReplay& replay() const noexcept { return replay_; }

  signals:
    void sessionChanged();
    void liveDataChanged();
    void viewportChanged();
    void statusChanged();
    void selectionChanged();
    void markersChanged();
    void renderWindowChanged();
    void activeChanged();
    void backtestResultsChanged();
    void backtestResultChanged();

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
        std::int64_t candleCount{0};
        std::int64_t candleQuoteAmountE8{0};
        bool hasCandleLow{false};
        std::int64_t candleLowE8{0};
        bool hasCandleHigh{false};
        std::int64_t candleHighE8{0};
        std::int64_t candleM1Count{0};
        std::int64_t candleM15Count{0};
        std::int64_t candleD1Count{0};
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
    void startLiveData_(const std::filesystem::path& sessionDir);
    void stopLiveData_() noexcept;
    void pollLiveData_();
    void clearLiveDataCache_() noexcept;
    void clearStrategyOverlay_() noexcept;
    void refreshProviderFromRegistry_();
    bool appendOverlayBatch_(const LiveDataBatch& batch, QString* failureText = nullptr);
    void reconcileOverlayWithStable_();
    void refreshLoadedStateFromSources_() noexcept;
    void initializeViewportFromLiveDataOnce_() noexcept;
    void applyRecordedRenderWindowViewport_() noexcept;
    void markUserViewportControl_() noexcept;
    std::int64_t latestRenderableTsNs_() const noexcept;
    std::int64_t latestOrderbookTsNs_() const noexcept;
    std::int64_t effectiveRenderMinTs_(std::int64_t latestTsNs) const noexcept;
    bool latestOnlyRenderWindow_() const noexcept { return renderWindowSeconds_ < 0; }
    bool limitedRenderWindow_() const noexcept { return renderWindowSeconds_ > 0; }
    SelectionRange selectionFromRect_(qreal plotWidthPx, qreal plotHeightPx,
                                      qreal x0, qreal y0, qreal x1, qreal y1) const noexcept;
    SelectionSummary buildSelectionSummary_(const SelectionRange& range);
    QString formatSelectionSummary_(const SelectionRange& range, const SelectionSummary& summary) const;

    hftrec::replay::SessionReplay replay_{};
    QString sessionDir_{};
    QString currentSourceId_{};
    QString currentSourceKind_{};
    QString statusText_{"No session loaded"};
    bool loaded_{false};
    bool active_{false};
    QTimer* liveDataTimer_{nullptr};
    std::unique_ptr<ILiveDataProvider> liveDataProvider_{};
    bool liveProviderFromRegistry_{false};
    QString liveProviderSourceId_{};
    bool liveFollowEdge_{true};
    bool liveOrderbookHealthy_{true};
    int liveUpdateIntervalMs_{100};
    LiveDataCache liveDataCache_{};
    LiveDataBatch liveOverlayState_{};
    bool liveInitialViewportApplied_{false};
    std::uint64_t liveDataBatchSeq_{0};
    LiveDataStats liveDataStats_{};
    std::int64_t liveWindowTsMin_{0};
    std::int64_t liveWindowTsMax_{0};
    std::uint64_t liveWindowVersion_{0};

    qint64 tsMin_{0};
    qint64 tsMax_{0};
    qint64 priceMinE8_{0};
    qint64 priceMaxE8_{0};
    std::int64_t currentBookTickerIndex_{-1};
    bool gpuRendererAvailable_{true};
    bool selectionActive_{false};
    QString selectionSummaryText_{};
    std::vector<VerticalMarker> verticalMarkers_{};
    QVariantList backtestResults_{};
    QString selectedBacktestResult_{};
    StrategyOverlayData strategyOverlay_{};
    StrategyIndicatorData strategyIndicator_{};
    int renderWindowSeconds_{0};
};

}  // namespace hftrec::gui::viewer







