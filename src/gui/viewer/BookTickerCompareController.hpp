#pragma once

#include <QObject>
#include <QTimer>
#include <QString>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

#include "core/arbitrage/BookTickerSpread.hpp"
#include "core/arbitrage/BookTickerSpreadMean.hpp"
#include "core/replay/EventRows.hpp"
#include "gui/viewer/LiveDataProvider.hpp"

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
    Q_PROPERTY(double primaryFeeActionBps READ primaryFeeActionBps WRITE setPrimaryFeeActionBps NOTIFY feesChanged)
    Q_PROPERTY(double secondaryFeeActionBps READ secondaryFeeActionBps WRITE setSecondaryFeeActionBps NOTIFY feesChanged)
    Q_PROPERTY(double totalFeePenaltyBps READ totalFeePenaltyBps NOTIFY feesChanged)
    Q_PROPERTY(double meanWindowSeconds READ meanWindowSeconds WRITE setMeanWindowSeconds NOTIFY meanChanged)
    Q_PROPERTY(qint64 tsMin READ tsMin NOTIFY viewportChanged)
    Q_PROPERTY(qint64 tsMax READ tsMax NOTIFY viewportChanged)

  public:
    explicit BookTickerCompareController(QObject* parent = nullptr);

    QString primarySourceId() const { return primarySourceId_; }
    QString secondarySourceId() const { return secondarySourceId_; }
    QString statusText() const { return statusText_; }
    bool ready() const noexcept { return !primaryRows_.empty() && !secondaryRows_.empty() && !spreadPoints_.empty(); }
    int primaryCount() const noexcept { return static_cast<int>(primaryRows_.size()); }
    int secondaryCount() const noexcept { return static_cast<int>(secondaryRows_.size()); }
    int spreadCount() const noexcept { return static_cast<int>(spreadPoints_.size()); }
    double primaryFeeActionBps() const noexcept { return primaryFeeActionBps_; }
    double secondaryFeeActionBps() const noexcept { return secondaryFeeActionBps_; }
    double totalFeePenaltyBps() const noexcept { return 2.0 * primaryFeeActionBps_ + 2.0 * secondaryFeeActionBps_; }
    double meanWindowSeconds() const noexcept { return meanWindowSeconds_; }
    qint64 tsMin() const noexcept { return tsMin_; }
    qint64 tsMax() const noexcept { return tsMax_; }
    qint64 currentTs() const noexcept { return fullTsMax_; }

    const std::vector<hftrec::replay::BookTickerRow>& primaryRows() const noexcept { return primaryRows_; }
    const std::vector<hftrec::replay::BookTickerRow>& secondaryRows() const noexcept { return secondaryRows_; }
    const std::vector<hftrec::arbitrage::BookTickerSpreadPoint>& spreadPoints() const noexcept { return spreadPoints_; }
    const std::vector<hftrec::arbitrage::BookTickerSpreadMeanPoint>& meanPoints() const noexcept { return meanPoints_; }

    Q_INVOKABLE bool setPrimarySource(const QString& sourceId, const QString& sourceKind, const QString& sessionPath);
    Q_INVOKABLE bool setSecondarySource(const QString& sourceId, const QString& sourceKind, const QString& sessionPath);
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
    };

    bool setSource_(SourceState& state, const QString& sourceId, const QString& sourceKind, const QString& sessionPath);
    void reloadRecorded_(SourceState& state);
    void pollLive_();
    void rebuild_();
    void updateFullRange_() noexcept;
    void initializeViewportIfNeeded_() noexcept;
    void setStatus_(const QString& statusText);

    SourceState primary_{};
    SourceState secondary_{};
    QString primarySourceId_{};
    QString secondarySourceId_{};
    QString statusText_{QStringLiteral("Select two bookTicker sessions")};
    std::vector<hftrec::replay::BookTickerRow> primaryRows_{};
    std::vector<hftrec::replay::BookTickerRow> secondaryRows_{};
    std::vector<hftrec::arbitrage::BookTickerSpreadPoint> spreadPoints_{};
    std::vector<hftrec::arbitrage::BookTickerSpreadMeanPoint> meanPoints_{};
    double primaryFeeActionBps_{0.0};
    double secondaryFeeActionBps_{0.0};
    double meanWindowSeconds_{5.0};
    qint64 fullTsMin_{0};
    qint64 fullTsMax_{1};
    qint64 tsMin_{0};
    qint64 tsMax_{1};
    bool viewportInitialized_{false};
    bool userViewportControl_{false};
    QTimer liveTimer_{};
};

}  // namespace hftrec::gui::viewer



