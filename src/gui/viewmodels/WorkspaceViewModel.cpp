#include "gui/viewmodels/WorkspaceViewModel.hpp"

#include <QLatin1StringView>
#include <QGuiApplication>
#include <QScreen>
#include <QVariantMap>

#include <algorithm>

namespace hftrec::gui {

namespace {

constexpr QLatin1StringView kMainHostId{"main"};

bool containsTab(const QStringList& tabs, const QString& tabId) noexcept {
    return tabs.contains(tabId);
}

QRect saneGeometry(const QRect& geometry) noexcept {
    QRect out = geometry;
    if (out.width() < 480) out.setWidth(1120);
    if (out.height() < 320) out.setHeight(760);
    if (out.x() < -20000 || out.x() > 20000) out.moveLeft(160);
    if (out.y() < -20000 || out.y() > 20000) out.moveTop(120);
    QRect visibleArea{};
    const auto screens = QGuiApplication::screens();
    for (auto* screen : screens) {
        if (screen == nullptr) continue;
        visibleArea = visibleArea.isNull() ? screen->availableGeometry() : visibleArea.united(screen->availableGeometry());
    }
    if (!visibleArea.isNull() && !visibleArea.adjusted(-80, -80, 80, 80).intersects(out)) {
        out.moveTopLeft(visibleArea.topLeft() + QPoint{80, 80});
    }
    return out;
}

}  // namespace

WorkspaceViewModel::WorkspaceViewModel(QObject* parent) : QObject(parent) {
    loadLayout_();
    repairLayout_();
}

QVariantList WorkspaceViewModel::hosts() const {
    QVariantList out;
    for (const auto& host : hosts_) out.push_back(hostToVariant_(host));
    return out;
}

QVariantList WorkspaceViewModel::floatingHosts() const {
    QVariantList out;
    for (const auto& host : hosts_) {
        if (host.floating) out.push_back(hostToVariant_(host));
    }
    return out;
}

QStringList WorkspaceViewModel::hostTabs(const QString& hostId) const {
    const auto* host = host_(hostId);
    return host == nullptr ? QStringList{} : host->tabs;
}

QString WorkspaceViewModel::activeTab(const QString& hostId) const {
    const auto* host = host_(hostId);
    return host == nullptr ? QString{} : host->activeTab;
}

QString WorkspaceViewModel::tabHost(const QString& tabId) const {
    for (const auto& host : hosts_) {
        if (containsTab(host.tabs, tabId)) return host.id;
    }
    return QString{};
}

QString WorkspaceViewModel::tabTitle(const QString& tabId) const {
    if (tabId == QStringLiteral("capture")) return QStringLiteral("Capture");
    if (tabId == QStringLiteral("viewer")) return QStringLiteral("Viewer");
    if (tabId == QStringLiteral("compress")) return QStringLiteral("Compress");
    return tabId;
}

bool WorkspaceViewModel::isFloatingHost(const QString& hostId) const {
    const auto* host = host_(hostId);
    return host != nullptr && host->floating;
}

bool WorkspaceViewModel::tabExists(const QString& tabId) const {
    return tabOrder_.contains(tabId);
}

QString WorkspaceViewModel::hostAtGlobal(int x, int y) const {
    const QPoint point{x, y};
    for (auto it = hosts_.rbegin(); it != hosts_.rend(); ++it) {
        if (it->geometry.contains(point)) return it->id;
    }
    return QString{};
}

void WorkspaceViewModel::setActiveTab(const QString& hostId, const QString& tabId) {
    auto* host = host_(hostId);
    if (host == nullptr || !containsTab(host->tabs, tabId)) return;
    if (host->activeTab == tabId) return;
    host->activeTab = tabId;
    emitLayoutChanged_();
}

QString WorkspaceViewModel::detachTab(const QString& tabId, int x, int y, int width, int height) {
    if (!tabExists(tabId)) return QString{};
    const QString existingHostId = tabHost(tabId);
    const auto* existingHost = host_(existingHostId);
    if (existingHost != nullptr && existingHost->floating && existingHost->tabs.size() == 1) {
        return existingHostId;
    }

    removeTabFromHost_(tabId);
    HostState host{};
    host.id = nextFloatingHostId_();
    const QString newHostId = host.id;
    host.floating = true;
    host.tabs = QStringList{tabId};
    host.activeTab = tabId;
    host.geometry = saneGeometry(QRect{x, y, width, height});
    hosts_.push_back(std::move(host));
    repairLayout_();
    emitLayoutChanged_();
    return newHostId;
}

void WorkspaceViewModel::dockTab(const QString& tabId, const QString& targetHostId) {
    if (!tabExists(tabId)) return;
    auto* targetHost = host_(targetHostId.isEmpty() ? QString{kMainHostId} : targetHostId);
    if (targetHost == nullptr) targetHost = host_(QString{kMainHostId});
    if (targetHost == nullptr) return;

    const QString currentHostId = tabHost(tabId);
    if (currentHostId == targetHost->id) {
        setActiveTab(targetHost->id, tabId);
        return;
    }

    removeTabFromHost_(tabId);
    appendTabIfMissing_(*targetHost, tabId);
    targetHost->activeTab = tabId;
    repairLayout_();
    emitLayoutChanged_();
}

void WorkspaceViewModel::closeHost(const QString& hostId) {
    if (hostId == kMainHostId) return;
    const int index = hostIndex_(hostId);
    if (index < 0 || !hosts_[static_cast<std::size_t>(index)].floating) return;

    auto* mainHost = host_(QString{kMainHostId});
    if (mainHost == nullptr) return;
    const QStringList tabsToMove = hosts_[static_cast<std::size_t>(index)].tabs;
    hosts_.erase(hosts_.begin() + index);
    for (const auto& tabId : tabsToMove) appendTabIfMissing_(*mainHost, tabId);
    if (!tabsToMove.empty()) mainHost->activeTab = tabsToMove.constFirst();
    repairLayout_();
    emitLayoutChanged_();
}

void WorkspaceViewModel::restoreAllTabsToMain() {
    HostState main{};
    main.id = QString{kMainHostId};
    main.floating = false;
    main.tabs = tabOrder_;
    main.activeTab = tabOrder_.isEmpty() ? QString{} : tabOrder_.constFirst();
    hosts_.clear();
    hosts_.push_back(std::move(main));
    emitLayoutChanged_();
}

void WorkspaceViewModel::setHostGeometry(const QString& hostId, int x, int y, int width, int height) {
    auto* host = host_(hostId);
    if (host == nullptr) return;
    const QRect next = saneGeometry(QRect{x, y, width, height});
    if (host->geometry == next) return;
    host->geometry = next;
    saveLayout_();
}

const WorkspaceViewModel::HostState* WorkspaceViewModel::host_(const QString& hostId) const {
    const auto it = std::find_if(hosts_.begin(), hosts_.end(), [&hostId](const HostState& host) {
        return host.id == hostId;
    });
    return it == hosts_.end() ? nullptr : &(*it);
}

WorkspaceViewModel::HostState* WorkspaceViewModel::host_(const QString& hostId) {
    const auto it = std::find_if(hosts_.begin(), hosts_.end(), [&hostId](const HostState& host) {
        return host.id == hostId;
    });
    return it == hosts_.end() ? nullptr : &(*it);
}

int WorkspaceViewModel::hostIndex_(const QString& hostId) const noexcept {
    for (std::size_t i = 0; i < hosts_.size(); ++i) {
        if (hosts_[i].id == hostId) return static_cast<int>(i);
    }
    return -1;
}

bool WorkspaceViewModel::removeTabFromHost_(const QString& tabId, QString* previousHostId) {
    for (auto& host : hosts_) {
        const int index = host.tabs.indexOf(tabId);
        if (index < 0) continue;
        if (previousHostId != nullptr) *previousHostId = host.id;
        host.tabs.removeAt(index);
        if (host.activeTab == tabId) host.activeTab = host.tabs.isEmpty() ? QString{} : host.tabs.constFirst();
        return true;
    }
    return false;
}

void WorkspaceViewModel::appendTabIfMissing_(HostState& host, const QString& tabId) {
    if (!tabExists(tabId) || containsTab(host.tabs, tabId)) return;
    host.tabs.push_back(tabId);
    if (host.activeTab.isEmpty()) host.activeTab = tabId;
}

void WorkspaceViewModel::repairLayout_() {
    if (host_(QString{kMainHostId}) == nullptr) {
        HostState main{};
        main.id = QString{kMainHostId};
        main.floating = false;
        hosts_.insert(hosts_.begin(), std::move(main));
    }

    QStringList seenTabs;
    for (auto hostIt = hosts_.begin(); hostIt != hosts_.end();) {
        QStringList repairedTabs;
        for (const auto& tabId : hostIt->tabs) {
            if (!tabExists(tabId) || seenTabs.contains(tabId)) continue;
            repairedTabs.push_back(tabId);
            seenTabs.push_back(tabId);
        }
        hostIt->tabs = repairedTabs;
        hostIt->geometry = saneGeometry(hostIt->geometry);
        if (!containsTab(hostIt->tabs, hostIt->activeTab)) {
            hostIt->activeTab = hostIt->tabs.isEmpty() ? QString{} : hostIt->tabs.constFirst();
        }
        if (hostIt->floating && hostIt->tabs.isEmpty()) hostIt = hosts_.erase(hostIt);
        else ++hostIt;
    }

    auto* mainHost = host_(QString{kMainHostId});
    if (mainHost == nullptr) return;
    for (const auto& tabId : tabOrder_) {
        if (!seenTabs.contains(tabId)) appendTabIfMissing_(*mainHost, tabId);
    }
    if (mainHost->activeTab.isEmpty() && !mainHost->tabs.isEmpty()) mainHost->activeTab = mainHost->tabs.constFirst();
}

void WorkspaceViewModel::emitLayoutChanged_() {
    ++layoutRevision_;
    saveLayout_();
    emit layoutChanged();
}

void WorkspaceViewModel::loadLayout_() {
    hosts_.clear();
    const int count = settings_.beginReadArray(QStringLiteral("workspace/hosts"));
    for (int i = 0; i < count; ++i) {
        settings_.setArrayIndex(i);
        HostState host{};
        host.id = settings_.value(QStringLiteral("id")).toString();
        host.floating = settings_.value(QStringLiteral("floating"), host.id != kMainHostId).toBool();
        host.tabs = settings_.value(QStringLiteral("tabs")).toStringList();
        host.activeTab = settings_.value(QStringLiteral("activeTab")).toString();
        host.geometry = settings_.value(QStringLiteral("geometry"), host.geometry).toRect();
        if (!host.id.isEmpty()) hosts_.push_back(std::move(host));
    }
    settings_.endArray();

    const int nextIndex = settings_.value(QStringLiteral("workspace/nextFloatingIndex"), nextFloatingIndex_).toInt();
    nextFloatingIndex_ = std::max(1, nextIndex);
}

void WorkspaceViewModel::saveLayout_() {
    settings_.beginWriteArray(QStringLiteral("workspace/hosts"));
    for (int i = 0; i < static_cast<int>(hosts_.size()); ++i) {
        settings_.setArrayIndex(i);
        const auto& host = hosts_[static_cast<std::size_t>(i)];
        settings_.setValue(QStringLiteral("id"), host.id);
        settings_.setValue(QStringLiteral("floating"), host.floating);
        settings_.setValue(QStringLiteral("tabs"), host.tabs);
        settings_.setValue(QStringLiteral("activeTab"), host.activeTab);
        settings_.setValue(QStringLiteral("geometry"), host.geometry);
    }
    settings_.endArray();
    settings_.setValue(QStringLiteral("workspace/nextFloatingIndex"), nextFloatingIndex_);
    settings_.sync();
}

QVariantMap WorkspaceViewModel::hostToVariant_(const HostState& host) const {
    return QVariantMap{
        {QStringLiteral("id"), host.id},
        {QStringLiteral("floating"), host.floating},
        {QStringLiteral("tabs"), host.tabs},
        {QStringLiteral("activeTab"), host.activeTab},
        {QStringLiteral("x"), host.geometry.x()},
        {QStringLiteral("y"), host.geometry.y()},
        {QStringLiteral("width"), host.geometry.width()},
        {QStringLiteral("height"), host.geometry.height()},
    };
}

QString WorkspaceViewModel::nextFloatingHostId_() {
    QString id;
    do {
        id = QStringLiteral("floating-%1").arg(nextFloatingIndex_++);
    } while (host_(id) != nullptr);
    return id;
}

}  // namespace hftrec::gui
