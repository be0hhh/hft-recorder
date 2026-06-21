#pragma once

#include <QObject>
#include <QTimer>
#include <QString>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/arbitrage/BookTickerSpread.hpp"
#include "core/arbitrage/BookTickerSpreadMean.hpp"
#include "core/arbitrage/CandleSpread.hpp"
#include "core/replay/EventRows.hpp"
#include "gui/viewer/CompareLowerPane.hpp"
#include "gui/viewer/LiveDataProvider.hpp"
#include "gui/viewer/StrategyIndicator.hpp"
#include "gui/viewer/StrategyOverlay.hpp"

namespace hftrec::gui::viewer {

class BookTickerCompareController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString primarySourceId READ primarySourceId NOTIFY sourcesChanged)
    Q_PROPERTY(QString secondarySourceId READ secondarySourceId NOTIFY sourcesChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY dataChanged)
    Q_PROPERTY(int primaryCount READ primaryCount NOTIFY dataChanged)
    Q_PROPERTY(int secondaryCount READ secondaryCount NOTIFY dataChanged)
    Q_PROPERTY(int spreadCount READ spreadCount NOTIFY dataChanged)
    Q_PROPERTY(int primaryCandleCount READ primaryCandleCount NOTIFY dataChanged)
    Q_PROPERTY(int secondaryCandleCount READ secondaryCandleCount NOTIFY dataChanged)
    Q_PROPERTY(int candleSpreadCount READ candleSpreadCount NOTIFY dataChanged)
    Q_PROPERTY(QString lowerPaneMode READ lowerPaneMode NOTIFY dataChanged)
    Q_PROPERTY(QString lowerPaneTitle READ lowerPaneTitle NOTIFY dataChanged)
    Q_PROPERTY(QString selectedBacktestResult READ selectedBacktestResult NOTIFY dataChanged)
    Q_PROPERTY(double primaryFeeActionBps READ primaryFeeActionBps WRITE setPrimaryFeeActionBps NOTIFY feesChanged)
    Q_PROPERTY(double secondaryFeeActionBps READ secondaryFeeActionBps WRITE setSecondaryFeeActionBps NOTIFY feesChanged)
    Q_PROPERTY(double totalFeePenaltyBps READ totalFeePenaltyBps NOTIFY feesChanged)
    Q_PROPERTY(double meanWindowSeconds READ meanWindowSeconds WRITE setMeanWindowSeconds NOTIFY meanChanged)
    Q_PROPERTY(qint64 tsMin READ tsMin NOTIFY viewportChanged)
    Q_PROPERTY(qint64 tsMax READ tsMax NOTIFY viewportChanged)
    Q_PROPERTY(double priceZoom READ priceZoom NOTIFY viewportChanged)
    Q_PROPERTY(double spreadZoom READ spreadZoom NOTIFY viewportChanged)

  public:
    explicit BookTickerCompareController(QObject* parent = nullptr);

    QString primarySourceId() const { return primarySourceId_; }
    QString secondarySourceId() const { return secondarySourceId_; }
    QString statusText() const { return statusText_; }
    bool ready() const noexcept {
        return (!primaryRows_.empty() || !primaryCandles_.empty())
            && (!secondaryRows_.empty() || !secondaryCandles_.empty())
            && lowerPaneState_.hasData;
    }
    int primaryCount() const noexcept { return static_cast<int>(primaryRows_.size()); }
    int secondaryCount() const noexcept { return static_cast<int>(secondaryRows_.size()); }
    int spreadCount() const noexcept { return static_cast<int>(spreadPoints_.size()); }
    int primaryCandleCount() const noexcept { return static_cast<int>(primaryCandles_.size()); }
    int secondaryCandleCount() const noexcept { return static_cast<int>(secondaryCandles_.size()); }
    int candleSpreadCount() const noexcept { return static_cast<int>(candleSpreadPoints_.size()); }
    QString lowerPaneMode() const { return compareLowerPaneKindId(lowerPaneState_.kind); }
    QString lowerPaneTitle() const { return lowerPaneState_.title; }
    QString selectedBacktestResult() const { return selectedBacktestResult_; }
    double primaryFeeActionBps() const noexcept { return primaryFeeActionBps_; }
    double secondaryFeeActionBps() const noexcept { return secondaryFeeActionBps_; }
    double totalFeePenaltyBps() const noexcept { return 2.0 * primaryFeeActionBps_ + 2.0 * secondaryFeeActionBps_; }
    double meanWindowSeconds() const noexcept { return meanWindowSeconds_; }
    qint64 tsMin() const noexcept { return tsMin_; }
    qint64 tsMax() const noexcept { return tsMax_; }
    qint64 currentTs() const noexcept { return fullTsMax_; }
    double priceZoom() const noexcept { return priceZoom_; }
    double pricePan() const noexcept { return pricePan_; }
    double spreadZoom() const noexcept { return spreadZoom_; }
    double spreadPan() const noexcept { return spreadPan_; }

    const std::vector<hftrec::replay::BookTickerRow>& primaryRows() const noexcept { return primaryRows_; }
    const std::vector<hftrec::replay::BookTickerRow>& secondaryRows() const noexcept { return secondaryRows_; }
    const std::vector<hftrec::replay::FundingRow>& primaryFundingRows() const noexcept { return primaryFundingRows_; }
    const std::vector<hftrec::replay::FundingRow>& secondaryFundingRows() const noexcept { return secondaryFundingRows_; }
    const std::vector<hftrec::replay::CandleRow>& primaryCandles() const noexcept { return primaryCandles_; }
    const std::vector<hftrec::replay::CandleRow>& secondaryCandles() const noexcept { return secondaryCandles_; }
    const std::vector<hftrec::arbitrage::BookTickerSpreadPoint>& spreadPoints() const noexcept { return spreadPoints_; }
    const std::vector<hftrec::arbitrage::BookTickerSpreadMeanPoint>& meanPoints() const noexcept { return meanPoints_; }
    const std::vector<hftrec::arbitrage::CandleSpreadPoint>& candleSpreadPoints() const noexcept { return candleSpreadPoints_; }
    const StrategyOverlayData& strategyOverlay() const noexcept { return strategyOverlay_; }
    const StrategyIndicatorData& strategyIndicator() const noexcept { return strategyIndicator_; }
    CompareLowerPaneKind lowerPaneKind() const noexcept { return lowerPaneState_.kind; }

    Q_INVOKABLE bool setPrimarySource(const QString& sourceId, const QString& sourceKind, const QString& sessionPath);
    Q_INVOKABLE bool setSecondarySource(const QString& sourceId, const QString& sourceKind, const QString& sessionPath);
    Q_INVOKABLE bool setBacktestResult(const QString& resultPath);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void setPrimaryFeeActionBps(double bps);
    Q_INVOKABLE void setSecondaryFeeActionBps(double bps);
    Q_INVOKABLE void setMeanWindowSeconds(double seconds);
    Q_INVOKABLE double savedFeeActionBps(const QString& exchange, const QString& market) const;
    Q_INVOKABLE void saveFeeActionBps(const QString& exchange, const QString& market, double bps);
    Q_INVOKABLE void setLiveUpdateIntervalMs(int intervalMs);
    Q_INVOKABLE void autoFit();
    Q_INVOKABLE void panTime(double fraction);
    Q_INVOKABLE void zoomTime(double factor);
    Q_INVOKABLE void zoomTimeAt(double factor, double anchorFraction);
    Q_INVOKABLE void panPrice(double fraction);
    Q_INVOKABLE void panSpread(double fraction);
    Q_INVOKABLE void zoomPrice(double factor);
    Q_INVOKABLE void zoomSpread(double factor);
    Q_INVOKABLE void zoomPriceAt(double factor, double anchorFraction);
    Q_INVOKABLE void zoomSpreadAt(double factor, double anchorFraction);
    Q_INVOKABLE void resetValueScale();

  signals:
    void sourcesChanged();
    void dataChanged();
    void viewportChanged();
    void statusChanged();
    void feesChanged();
    void meanChanged();

  private:
    struct SourceState {
        QString sourceId{};
        QString sourceKind{};
        std::filesystem::path sessionPath{};
        std::unique_ptr<ILiveDataProvider> liveProvider{};
        std::uint64_t nextBatchId{1};
        std::vector<hftrec::replay::BookTickerRow> rows{};
        std::vector<hftrec::replay::FundingRow> fundings{};
        std::vector<hftrec::replay::CandleRow> candles{};
        std::string marketHint{};
        std::int64_t priceBasisQtyE8{hftrec::arbitrage::kPriceBasisScaleE8};
    };

    bool setSource_(SourceState& state, const QString& sourceId, const QString& sourceKind, const QString& sessionPath);
    void reloadRecorded_(SourceState& state);
    void pollLive_();
    void rebuild_();
    void updateLowerPane_();
    void updateFullRange_() noexcept;
    void initializeViewportIfNeeded_() noexcept;
    void setStatus_(const QString& statusText);
    void resetValueScale_() noexcept;

    SourceState primary_{};
    SourceState secondary_{};
    QString primarySourceId_{};
    QString secondarySourceId_{};
    QString statusText_{QStringLiteral("Select two market sessions")};
    std::vector<hftrec::replay::BookTickerRow> primaryRows_{};
    std::vector<hftrec::replay::BookTickerRow> secondaryRows_{};
    std::vector<hftrec::replay::FundingRow> primaryFundingRows_{};
    std::vector<hftrec::replay::FundingRow> secondaryFundingRows_{};
    std::vector<hftrec::replay::CandleRow> primaryCandles_{};
    std::vector<hftrec::replay::CandleRow> secondaryCandles_{};
    std::vector<hftrec::arbitrage::BookTickerSpreadPoint> spreadPoints_{};
    std::vector<hftrec::arbitrage::BookTickerSpreadMeanPoint> meanPoints_{};
    std::vector<hftrec::arbitrage::CandleSpreadPoint> candleSpreadPoints_{};
    QString selectedBacktestResult_{};
    StrategyOverlayData strategyOverlay_{};
    StrategyIndicatorData strategyIndicator_{};
    CompareLowerPaneState lowerPaneState_{};
    double primaryFeeActionBps_{0.0};
    double secondaryFeeActionBps_{0.0};
    double meanWindowSeconds_{5.0};
    qint64 fullTsMin_{0};
    qint64 fullTsMax_{1};
    qint64 tsMin_{0};
    qint64 tsMax_{1};
    double priceZoom_{1.0};
    double pricePan_{0.0};
    double spreadZoom_{1.0};
    double spreadPan_{0.0};
    bool viewportInitialized_{false};
    bool userViewportControl_{false};
    QTimer liveTimer_{};
};

}  // namespace hftrec::gui::viewer



