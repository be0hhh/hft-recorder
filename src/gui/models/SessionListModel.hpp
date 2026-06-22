#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>

namespace hftrec::gui {

class SessionListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString recordingsRoot READ recordingsRoot CONSTANT)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)

  public:
    enum Roles {
        SessionIdRole = Qt::UserRole + 1,
        SessionSummaryRole,
        LabelRole,
        PathRole,
        SearchTextRole,
        IsGroupRole,
        IndentRole,
    };

    explicit SessionListModel(QObject* parent = nullptr);

    Q_INVOKABLE void reload();
    Q_INVOKABLE QString sessionPath(const QString& sessionId) const;

    QString recordingsRoot() const;
    QString searchText() const { return searchText_; }
    void setSearchText(const QString& searchText);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

  signals:
    void searchTextChanged();

  private:
    struct Entry {
        QString sessionId{};
        QString label{};
        QString summary{};
        QString path{};
        QString searchText{};
        bool isGroup{false};
        int indent{0};
    };

    void applyFilter_();

    QList<Entry> allSessions_{};
    QList<Entry> sessions_{};
    QString searchText_{};
};

}  // namespace hftrec::gui
