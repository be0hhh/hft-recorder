#pragma once

#include <QAbstractListModel>
#include <QStringList>

namespace hftrec::gui {

class SessionListModel : public QAbstractListModel {
    Q_OBJECT

  public:
    enum Roles {
        SessionIdRole = Qt::UserRole + 1,
    };

    explicit SessionListModel(QObject* parent = nullptr);

    Q_INVOKABLE void reload();

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

  private:
    QStringList sessions_{};
};

}  // namespace hftrec::gui
