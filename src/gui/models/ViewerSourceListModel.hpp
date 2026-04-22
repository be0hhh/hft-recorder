#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QString>
#include <QVariantList>

namespace hftrec::gui {

class CaptureViewModel;

class ViewerSourceListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString recordingsRoot READ recordingsRoot CONSTANT)
    Q_PROPERTY(QObject* captureViewModel READ captureViewModel WRITE setCaptureViewModel NOTIFY captureViewModelChanged)

  public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        LabelRole,
        GroupRole,
        GroupTitleRole,
        SourceKindRole,
        SessionPathRole,
        SymbolRole,
        ExchangeRole,
        MarketRole,
        LiveAvailableRole,
    };

    explicit ViewerSourceListModel(QObject* parent = nullptr);

    Q_INVOKABLE void reload();
    Q_INVOKABLE QString sessionPath(const QString& sourceId) const;
    Q_INVOKABLE QString sourceKind(const QString& sourceId) const;
    Q_INVOKABLE QString groupAt(int index) const;
    Q_INVOKABLE bool hasSource(const QString& sourceId) const;
    Q_INVOKABLE int indexOfSource(const QString& sourceId) const;

    QString recordingsRoot() const;
    QObject* captureViewModel() const;
    void setCaptureViewModel(QObject* captureViewModel);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

  signals:
    void captureViewModelChanged();

  private:
    struct Entry {
        QString id{};
        QString label{};
        QString group{};
        QString groupTitle{};
        QString sourceKind{};
        QString sessionPath{};
        QString symbol{};
        QString exchange{};
        QString market{};
        bool liveAvailable{false};
    };

    void reconnectCaptureVm_();
    QVariantList currentLiveSources_() const;
    void rebuildEntries_();

    CaptureViewModel* captureVm_{nullptr};
    QMetaObject::Connection captureSourcesConnection_{};
    QList<Entry> entries_{};
};

}  // namespace hftrec::gui
