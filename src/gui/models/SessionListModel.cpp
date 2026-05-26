#include "gui/models/SessionListModel.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace hftrec::gui {

namespace {

QString resolveRecordingsRoot() {
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString candidate = appDir.absoluteFilePath(QStringLiteral("../../recordings"));
    return QDir::cleanPath(QFileInfo(candidate).absoluteFilePath());
}

QString formatStartedAt(qint64 startedAtNs) {
    if (startedAtNs < 1000000000000000ll) return {};
    return QDateTime::fromMSecsSinceEpoch(startedAtNs / 1000000).toLocalTime().toString(QStringLiteral("dd.MM.yy hh:mm"));
}

int countBacktestResults(const QString& sessionPath) {
    const QDir backtestsDir(QDir(sessionPath).absoluteFilePath(QStringLiteral("backtests")));
    if (!backtestsDir.exists()) return 0;
    int count = 0;
    const QFileInfoList runDirs = backtestsDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& runDir : runDirs) {
        if (runDir.fileName() == QStringLiteral("sweeps")) continue;
        if (QFileInfo::exists(QDir(runDir.absoluteFilePath()).absoluteFilePath(QStringLiteral("manifest.json")))) ++count;
    }
    const QDir sweepsDir(backtestsDir.absoluteFilePath(QStringLiteral("sweeps")));
    const QFileInfoList sweepDirs = sweepsDir.exists() ? sweepsDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot) : QFileInfoList{};
    for (const QFileInfo& sweepDir : sweepDirs) {
        if (QFileInfo::exists(QDir(sweepDir.absoluteFilePath()).absoluteFilePath(QStringLiteral("manifest.json")))) ++count;
    }
    return count;
}

QString sessionSummary(const QString& sessionPath) {
    const int backtestCount = countBacktestResults(sessionPath);
    QFile file(QDir(sessionPath).absoluteFilePath(QStringLiteral("manifest.json")));
    if (!file.open(QIODevice::ReadOnly)) return QStringLiteral("L1 0 | BT %1").arg(backtestCount);
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    const QJsonObject manifest = doc.object();
    const QJsonObject bookTicker = manifest.value(QStringLiteral("channels")).toObject().value(QStringLiteral("bookticker")).toObject();
    QString summary = QStringLiteral("L1 %1 | BT %2").arg(bookTicker.value(QStringLiteral("declared_event_count")).toInt()).arg(backtestCount);
    const QString startedAt = formatStartedAt(manifest.value(QStringLiteral("capture")).toObject().value(QStringLiteral("started_at_ns")).toInteger());
    if (!startedAt.isEmpty()) summary += QStringLiteral(" | %1").arg(startedAt);
    return summary;
}

}  // namespace

SessionListModel::SessionListModel(QObject* parent)
    : QAbstractListModel(parent) {
    reload();
}

void SessionListModel::reload() {
    beginResetModel();
    sessions_.clear();

    QDir recordingsDir(recordingsRoot());
    if (recordingsDir.exists()) {
        const auto entries = recordingsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time | QDir::Reversed);
        for (const auto& entry : entries) {
            sessions_.push_back(Entry{entry, sessionSummary(recordingsDir.absoluteFilePath(entry))});
        }
    }

    endResetModel();
}

QString SessionListModel::sessionPath(const QString& sessionId) const {
    if (sessionId.trimmed().isEmpty()) return {};
    return QDir(recordingsRoot()).absoluteFilePath(sessionId);
}

QString SessionListModel::recordingsRoot() const {
    return resolveRecordingsRoot();
}

int SessionListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : sessions_.size();
}

QVariant SessionListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= sessions_.size()) return {};
    if (role == SessionIdRole) return sessions_.at(index.row()).sessionId;
    if (role == SessionSummaryRole) return sessions_.at(index.row()).summary;
    return {};
}

QHash<int, QByteArray> SessionListModel::roleNames() const {
    return {
        {SessionIdRole, "sessionId"},
        {SessionSummaryRole, "sessionSummary"},
    };
}

}  // namespace hftrec::gui
