#pragma once

#include "gui/backtests/BacktestSessionSummary.hpp"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>

namespace hftrec::gui {

enum class SessionManifestStatus {
    Ready,
    Missing,
    Unreadable,
    Malformed,
    ChangedDuringRead,
};

class SessionManifestSnapshot {
  public:
    SessionManifestSnapshot(SessionManifestStatus status,
                            QString sessionPath,
                            QString manifestPath,
                            QJsonObject object,
                            QString error,
                            qint64 size,
                            qint64 lastModifiedMs);

    [[nodiscard]] SessionManifestStatus status() const noexcept { return status_; }
    [[nodiscard]] bool ready() const noexcept { return status_ == SessionManifestStatus::Ready; }
    [[nodiscard]] const QString& sessionPath() const noexcept { return sessionPath_; }
    [[nodiscard]] const QString& manifestPath() const noexcept { return manifestPath_; }
    [[nodiscard]] const QJsonObject& object() const noexcept { return object_; }
    [[nodiscard]] const QString& error() const noexcept { return error_; }
    [[nodiscard]] qint64 size() const noexcept { return size_; }
    [[nodiscard]] qint64 lastModifiedMs() const noexcept { return lastModifiedMs_; }

  private:
    const SessionManifestStatus status_{SessionManifestStatus::Missing};
    const QString sessionPath_{};
    const QString manifestPath_{};
    const QJsonObject object_{};
    const QString error_{};
    const qint64 size_{-1};
    const qint64 lastModifiedMs_{0};
};

SessionManifestSnapshot loadSessionManifestSnapshot(const QString& sessionPath);
bool sessionSupportsCurrentBacktestContract(const SessionManifestSnapshot& manifest,
                                            QString* error = nullptr);
bool sessionSupportsCurrentBacktestContract(const QString& sessionPath,
                                            QString* error = nullptr);

QString resolveRecordingsRoot();
QString sessionSourceSummary(const SessionManifestSnapshot& manifest,
                             const BacktestLegCounts& backtestCounts);
QString sessionSourceSummary(const QString& sessionPath, const BacktestLegCounts& backtestCounts);
QString manifestValue(const SessionManifestSnapshot& manifest, const QString& key);
QString manifestValue(const QString& sessionPath, const QString& key);
std::uint64_t manifestChannelDeclaredCount(const SessionManifestSnapshot& manifest,
                                           const QString& channel);
std::uint64_t manifestChannelDeclaredCount(const QString& sessionPath,
                                           const QString& channel);
QString symbolFromSessionId(const QString& sessionId);
QString venueSectionFor(const QString& exchange, const QString& market);
bool isVenueSectionKnown(const QString& exchange, const QString& market);
QString venueSectionForSession(const SessionManifestSnapshot& manifest);
QString venueSectionForSession(const QString& sessionPath);
QString symbolForSessionPath(const SessionManifestSnapshot& manifest);
QString symbolForSessionPath(const QString& sessionPath);
QString sessionPathFromToken(const QString& recordingsRoot, const QString& token);
QVariantMap sessionRowById(const QVariantList& rows, const QString& id);
bool sessionRowSelectable(const QVariantMap& row);
QString firstSelectableSessionId(const QVariantList& rows);
QStringList sessionPathsFromRow(const QVariantMap& row);

}  // namespace hftrec::gui
