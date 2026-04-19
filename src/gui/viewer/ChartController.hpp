#pragma once

#include <QObject>
#include <QString>

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

  public:
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

    Q_INVOKABLE bool loadSession(const QString& dir);

    Q_INVOKABLE void resetSession();
    Q_INVOKABLE bool addTradesFile(const QString& path);
    Q_INVOKABLE bool addBookTickerFile(const QString& path);
    Q_INVOKABLE bool addDepthFile(const QString& path);
    Q_INVOKABLE bool addSnapshotFile(const QString& path);
    Q_INVOKABLE void finalizeFiles();

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
    Q_INVOKABLE QString formatPriceScaleLabel(int index, int tickCount) const;
    Q_INVOKABLE QString formatTimeScaleLabel(int index, int tickCount) const;

    void syncReplayCursorToViewport();
    std::int64_t viewportCursorTs() const noexcept;
    const hftrec::replay::BookTickerRow* currentBookTicker() const noexcept;

    // Builds a POD snapshot of what renderers need to draw one frame. Mutates
    // replay cursor internally (advances through events in the viewport), but
    // leaves no Qt-thread-visible state the renderers then observe.
    RenderSnapshot buildSnapshot(qreal widthPx, qreal heightPx, const SnapshotInputs& in);

    hftrec::replay::SessionReplay& replay() noexcept { return replay_; }
    const hftrec::replay::SessionReplay& replay() const noexcept { return replay_; }

  signals:
    void sessionChanged();
    void viewportChanged();
    void statusChanged();

  private:
    void computeInitialViewport_();

    hftrec::replay::SessionReplay replay_{};
    QString sessionDir_{};
    QString statusText_{"No session loaded"};
    bool loaded_{false};

    qint64 tsMin_{0};
    qint64 tsMax_{0};
    qint64 priceMinE8_{0};
    qint64 priceMaxE8_{0};
    std::int64_t currentBookTickerIndex_{-1};
    bool gpuRendererAvailable_{true};
};

}  // namespace hftrec::gui::viewer
