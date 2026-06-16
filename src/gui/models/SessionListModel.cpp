#include "gui/models/SessionListModel.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include "gui/models/BacktestSessionSummary.hpp"

namespace hftrec::gui {

namespace {

QString resolveRecordingsRoot() {
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString candidate = appDir.absoluteFilePath(QStringLiteral("../../recordings"));
    return QDir::cleanPath(QFileInfo(candidate).absoluteFilePath());
}

QString sessionSummary(const QString& recordingsRoot, const QString& sessionId, const QString& sessionPath) {
    const BacktestLegCounts backtestCounts = backtestLegCountsForSession(recordingsRoot, sessionId);
    QFile file(QDir(sessionPath).absoluteFilePath(QStringLiteral("manifest.json")));
    if (!file.open(QIODevice::ReadOnly)) return sessionBacktestSummaryText(0, backtestCounts, 0);
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    const QJsonObject manifest = doc.object();
    const QJsonObject bookTicker = manifest.value(QStringLiteral("channels")).toObject().value(QStringLiteral("bookticker")).toObject();
    return sessionBacktestSummaryText(bookTicker.value(QStringLiteral("declared_event_count")).toInt(),
                                      backtestCounts,
                                      manifest.value(QStringLiteral("capture")).toObject().value(QStringLiteral("started_at_ns")).toInteger());
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
            sessions_.push_back(Entry{entry, sessionSummary(recordingsRoot(), entry, recordingsDir.absoluteFilePath(entry))});
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
