#include "gui/models/SessionListModel.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace hftrec::gui {

namespace {

QString resolveRecordingsRoot() {
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString candidate = appDir.absoluteFilePath(QStringLiteral("../../recordings"));
    return QDir::cleanPath(QFileInfo(candidate).absoluteFilePath());
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
            sessions_.push_back(entry);
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
    if (role == SessionIdRole) return sessions_.at(index.row());
    return {};
}

QHash<int, QByteArray> SessionListModel::roleNames() const {
    return {
        {SessionIdRole, "sessionId"},
    };
}

}  // namespace hftrec::gui
