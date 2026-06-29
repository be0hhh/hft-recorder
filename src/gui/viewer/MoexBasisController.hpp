#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include <cstdint>
#include <filesystem>
#include <vector>

#include "gui/viewer/MoexBasisSeries.hpp"

namespace hftrec::gui::viewer {

class MoexBasisController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList groupRows READ groupRows NOTIFY groupsChanged)
    Q_PROPERTY(QVariantList legRows READ legRows NOTIFY dataChanged)
    Q_PROPERTY(QString groupPath READ groupPath NOTIFY dataChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY dataChanged)
    Q_PROPERTY(int enabledFutureCount READ enabledFutureCount NOTIFY dataChanged)
    Q_PROPERTY(int basisPointCount READ basisPointCount NOTIFY dataChanged)
    Q_PROPERTY(QString displayMode READ displayMode WRITE setDisplayMode NOTIFY dataChanged)
    Q_PROPERTY(QVariantList displayModeChoices READ displayModeChoices CONSTANT)
    Q_PROPERTY(int frontRank READ frontRank WRITE setFrontRank NOTIFY dataChanged)
    Q_PROPERTY(qint64 tsMin READ tsMin NOTIFY viewportChanged)
    Q_PROPERTY(qint64 tsMax READ tsMax NOTIFY viewportChanged)

  public:
    explicit MoexBasisController(QObject* parent = nullptr);

    QVariantList groupRows() const { return groupRows_; }
    QVariantList legRows() const;
    QString groupPath() const { return groupPath_; }
    QString statusText() const { return statusText_; }
    bool ready() const noexcept { return spot_.metadataReady && !spot_.candles.empty() && enabledFutureCount() > 0 && basisPointCount() > 0; }
    int enabledFutureCount() const noexcept;
    int basisPointCount() const noexcept;
    QString displayMode() const { return displayMode_; }
    QVariantList displayModeChoices() const;
    int frontRank() const noexcept { return frontRank_; }
    qint64 tsMin() const noexcept { return tsMin_; }
    qint64 tsMax() const noexcept { return tsMax_; }
    double priceZoom() const noexcept { return priceZoom_; }
    double pricePan() const noexcept { return pricePan_; }
    double basisZoom() const noexcept { return basisZoom_; }
    double basisPan() const noexcept { return basisPan_; }

    struct LegState {
        struct RollMarker {
            std::int64_t tsNs{0};
            QString label{};
        };

        QString role{};
        QString label{};
        QString symbol{};
        QString exchange{};
        QString market{};
        QString sessionPath{};
        QString status{};
        bool enabled{true};
        bool metadataReady{false};
        std::int64_t expiryUtcNs{0};
        std::int64_t priceBasisQtyE8{0};
        std::vector<hftrec::replay::CandleRow> candles{};
        std::vector<MoexBasisPoint> basisPoints{};
        std::vector<RollMarker> rollMarkers{};
    };

    const LegState& spotLeg() const noexcept { return spot_; }
    const std::vector<LegState>& futureLegs() const noexcept { return futures_; }
    const std::vector<LegState>& renderFutureLegs() const noexcept { return renderFutures_; }

    Q_INVOKABLE void reloadGroups();
    Q_INVOKABLE bool loadGroup(const QString& path);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void setFutureEnabled(int index, bool enabled);
    Q_INVOKABLE void setDisplayMode(const QString& mode);
    Q_INVOKABLE void setFrontRank(int rank);
    Q_INVOKABLE void autoFit();
    Q_INVOKABLE void panTime(double fraction);
    Q_INVOKABLE void zoomTimeAt(double factor, double anchorFraction);
    Q_INVOKABLE void panPrice(double fraction);
    Q_INVOKABLE void zoomPriceAt(double factor, double anchorFraction);
    Q_INVOKABLE void panBasis(double fraction);
    Q_INVOKABLE void zoomBasisAt(double factor, double anchorFraction);

  signals:
    void groupsChanged();
    void dataChanged();
    void viewportChanged();
    void statusChanged();

  private:
    void setStatus_(const QString& statusText);
    void rebuildGroupRows_();
    void rebuildBasis_();
    void rebuildRenderFutures_();
    LegState buildFrontRankLeg_(int rank) const;
    void updateFullRange_() noexcept;
    void resetValueScale_() noexcept;

    QVariantList groupRows_{};
    QString groupPath_{};
    QString statusText_{QStringLiteral("Select a MOEX basis group")};
    QString displayMode_{QStringLiteral("front_rank")};
    int frontRank_{1};
    LegState spot_{};
    std::vector<LegState> futures_{};
    std::vector<LegState> renderFutures_{};
    qint64 fullTsMin_{0};
    qint64 fullTsMax_{1};
    qint64 tsMin_{0};
    qint64 tsMax_{1};
    double priceZoom_{1.0};
    double pricePan_{0.0};
    double basisZoom_{1.0};
    double basisPan_{0.0};
};

}  // namespace hftrec::gui::viewer
