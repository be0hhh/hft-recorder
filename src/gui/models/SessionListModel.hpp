#pragma once

#include <QAbstractListModel>
#include <QStringList>

namespace hftrec::gui {

class SessionListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString recordingsRoot READ recordingsRoot CONSTANT)

  public:
    enum Roles {
        SessionIdRole = Qt::UserRole + 1,
    };

    explicit SessionListModel(QObject* parent = nullptr);

    Q_INVOKABLE void reload();
    Q_INVOKABLE QString sessionPath(const QString& sessionId) const;

    QString recordingsRoot() const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

  private:
    QStringList sessions_{};
};

}  // namespace hftrec::gui
