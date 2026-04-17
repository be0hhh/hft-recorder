#include "gui/models/SessionListModel.hpp"

#include <QDir>

namespace hftrec::gui {

SessionListModel::SessionListModel(QObject* parent)
    : QAbstractListModel(parent) {
    reload();
}

void SessionListModel::reload() {
    beginResetModel();
    sessions_.clear();

    QDir recordingsDir("./recordings");
    if (recordingsDir.exists()) {
        const auto entries = recordingsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time | QDir::Reversed);
        for (const auto& entry : entries) {
            sessions_.push_back(entry);
        }
    }

    endResetModel();
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
