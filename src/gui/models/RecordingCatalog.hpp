#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "core/recordings/RecordingDiscovery.hpp"
#include "gui/backtests/BacktestSessionSummary.hpp"

namespace hftrec::gui {

struct RecordingCatalogSnapshot {
    QString recordingsRoot{};
    hftrec::recordings::RecordingDiscoveryResult discovery{};
    QHash<QString, BacktestLegCounts> backtestCountsBySession{};
    qint64 revision{0};
    bool loaded{false};
    bool fromDisk{false};
    QString statusText{};
};

RecordingCatalogSnapshot buildRecordingCatalogSnapshot(const QString& recordingsRoot);
QString defaultRecordingCatalogCachePath();
bool writeRecordingCatalogCache(const QString& path, const RecordingCatalogSnapshot& snapshot, QString* errorText = nullptr);
RecordingCatalogSnapshot readRecordingCatalogCache(const QString& path, const QString& expectedRecordingsRoot, QString* errorText = nullptr);

class RecordingCatalog : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString recordingsRoot READ recordingsRoot CONSTANT)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(qint64 revision READ revision NOTIFY snapshotChanged)

  public:
    explicit RecordingCatalog(QObject* parent = nullptr);
    ~RecordingCatalog() override;

    QString recordingsRoot() const;
    bool loading() const noexcept { return loading_; }
    QString statusText() const { return statusText_; }
    qint64 revision() const noexcept { return snapshot_.revision; }
    bool hasSnapshot() const noexcept { return snapshot_.loaded; }
    const RecordingCatalogSnapshot& snapshot() const noexcept { return snapshot_; }

    Q_INVOKABLE void refresh();

  signals:
    void snapshotChanged();
    void stateChanged();

  private:
    struct AsyncContext {
        std::atomic_bool alive{true};
    };

    void refreshAsync_(QString reason);
    void applySnapshot_(std::uint64_t generation, RecordingCatalogSnapshot snapshot);
    void loadCache_();
    void setLoading_(bool loading, const QString& statusText);
    void stopWorkers_() noexcept;

    RecordingCatalogSnapshot snapshot_{};
    QString cachePath_{};
    QString statusText_{QStringLiteral("Recording catalog idle")};
    bool loading_{false};
    bool pendingRefresh_{false};
    std::uint64_t loadGeneration_{0};
    qint64 revisionCounter_{0};
    std::shared_ptr<AsyncContext> asyncContext_{std::make_shared<AsyncContext>()};
    std::vector<std::thread> workers_{};
};

}  // namespace hftrec::gui
