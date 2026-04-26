#pragma once

#include <QObject>
#include <QRect>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <vector>

namespace hftrec::gui {

class WorkspaceViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(int layoutRevision READ layoutRevision NOTIFY layoutChanged)
    Q_PROPERTY(QString mainHostId READ mainHostId CONSTANT)

  public:
    explicit WorkspaceViewModel(QObject* parent = nullptr);

    int layoutRevision() const noexcept { return layoutRevision_; }
    QString mainHostId() const { return QStringLiteral("main"); }

    Q_INVOKABLE QVariantList hosts() const;
    Q_INVOKABLE QVariantList floatingHosts() const;
    Q_INVOKABLE QStringList hostTabs(const QString& hostId) const;
    Q_INVOKABLE QString activeTab(const QString& hostId) const;
    Q_INVOKABLE QString tabHost(const QString& tabId) const;
    Q_INVOKABLE QString tabTitle(const QString& tabId) const;
    Q_INVOKABLE bool isFloatingHost(const QString& hostId) const;
    Q_INVOKABLE bool tabExists(const QString& tabId) const;
    Q_INVOKABLE QString hostAtGlobal(int x, int y) const;

    Q_INVOKABLE void setActiveTab(const QString& hostId, const QString& tabId);
    Q_INVOKABLE QString detachTab(const QString& tabId, int x, int y, int width, int height);
    Q_INVOKABLE void dockTab(const QString& tabId, const QString& targetHostId);
    Q_INVOKABLE void closeHost(const QString& hostId);
    Q_INVOKABLE void restoreAllTabsToMain();
    Q_INVOKABLE void setHostGeometry(const QString& hostId, int x, int y, int width, int height);

  signals:
    void layoutChanged();

  private:
    struct HostState {
        QString id{};
        bool floating{false};
        QStringList tabs{};
        QString activeTab{};
        QRect geometry{160, 120, 1120, 760};
    };

    const HostState* host_(const QString& hostId) const;
    HostState* host_(const QString& hostId);
    int hostIndex_(const QString& hostId) const noexcept;
    bool removeTabFromHost_(const QString& tabId, QString* previousHostId = nullptr);
    void appendTabIfMissing_(HostState& host, const QString& tabId);
    void repairLayout_();
    void emitLayoutChanged_();
    void loadLayout_();
    void saveLayout_();
    QVariantMap hostToVariant_(const HostState& host) const;
    QString nextFloatingHostId_();

    std::vector<HostState> hosts_{};
    QStringList tabOrder_{QStringLiteral("capture"), QStringLiteral("viewer"), QStringLiteral("compress")};
    int layoutRevision_{0};
    int nextFloatingIndex_{1};
    QSettings settings_{};
};

}  // namespace hftrec::gui
