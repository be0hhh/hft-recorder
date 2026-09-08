#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QMetaObject>
#include <QString>

namespace hftrec::gui {

class RecordingCatalog;

class SessionListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString recordingsRoot READ recordingsRoot CONSTANT)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    Q_PROPERTY(QObject* recordingCatalog READ recordingCatalog WRITE setRecordingCatalog NOTIFY recordingCatalogChanged)

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
    QObject* recordingCatalog() const;
    void setRecordingCatalog(QObject* recordingCatalog);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

  signals:
    void searchTextChanged();
    void recordingCatalogChanged();

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

    void reconnectRecordingCatalog_();
    void rebuildFromCatalog_();
    void applyFilter_();

    RecordingCatalog* recordingCatalog_{nullptr};
    QMetaObject::Connection catalogSnapshotConnection_{};
    QList<Entry> allSessions_{};
    QList<Entry> sessions_{};
    QString searchText_{};
};

}  // namespace hftrec::gui
