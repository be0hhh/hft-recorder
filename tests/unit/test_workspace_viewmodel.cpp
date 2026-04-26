#include <gtest/gtest.h>

#include <cstdlib>

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include "gui/viewmodels/WorkspaceViewModel.hpp"

namespace {

void isolateSettings(QStringView suffix) {
    QCoreApplication::setOrganizationName(QStringLiteral("hftrec_workspace_tests"));
    QCoreApplication::setApplicationName(QStringLiteral("case_%1_%2").arg(suffix, QString::number(std::rand())));
}

TEST(WorkspaceViewModel, StartsWithSingletonTabsInMainHost) {
    isolateSettings(QStringLiteral("default"));

    hftrec::gui::WorkspaceViewModel workspace;

    EXPECT_EQ(workspace.hostTabs(QStringLiteral("main")), QStringList({QStringLiteral("capture"), QStringLiteral("viewer"), QStringLiteral("compress")}));
    EXPECT_EQ(workspace.activeTab(QStringLiteral("main")), QStringLiteral("capture"));
    EXPECT_EQ(workspace.tabHost(QStringLiteral("viewer")), QStringLiteral("main"));
    EXPECT_TRUE(workspace.floatingHosts().empty());
}

TEST(WorkspaceViewModel, DetachesAndDocksSingletonTabWithoutDuplication) {
    isolateSettings(QStringLiteral("detach"));

    hftrec::gui::WorkspaceViewModel workspace;

    const QString floatingHost = workspace.detachTab(QStringLiteral("viewer"), 300, 240, 900, 600);

    ASSERT_FALSE(floatingHost.isEmpty());
    EXPECT_TRUE(workspace.isFloatingHost(floatingHost));
    EXPECT_EQ(workspace.hostTabs(floatingHost), QStringList({QStringLiteral("viewer")}));
    EXPECT_EQ(workspace.tabHost(QStringLiteral("viewer")), floatingHost);
    EXPECT_FALSE(workspace.hostTabs(QStringLiteral("main")).contains(QStringLiteral("viewer")));

    workspace.dockTab(QStringLiteral("viewer"), QStringLiteral("main"));

    EXPECT_EQ(workspace.tabHost(QStringLiteral("viewer")), QStringLiteral("main"));
    EXPECT_EQ(workspace.hostTabs(QStringLiteral("main")).count(QStringLiteral("viewer")), 1);
}

TEST(WorkspaceViewModel, ClosingFloatingHostReturnsTabsToMain) {
    isolateSettings(QStringLiteral("close"));

    hftrec::gui::WorkspaceViewModel workspace;
    const QString floatingHost = workspace.detachTab(QStringLiteral("compress"), 340, 260, 900, 600);
    ASSERT_FALSE(floatingHost.isEmpty());

    workspace.closeHost(floatingHost);

    EXPECT_EQ(workspace.tabHost(QStringLiteral("compress")), QStringLiteral("main"));
    EXPECT_TRUE(workspace.floatingHosts().empty());
    EXPECT_EQ(workspace.hostTabs(QStringLiteral("main")).count(QStringLiteral("compress")), 1);
}

TEST(WorkspaceViewModel, RestoreAllTabsToMainClearsFloatingHosts) {
    isolateSettings(QStringLiteral("restore"));

    hftrec::gui::WorkspaceViewModel workspace;
    ASSERT_FALSE(workspace.detachTab(QStringLiteral("viewer"), 340, 260, 900, 600).isEmpty());
    ASSERT_FALSE(workspace.floatingHosts().empty());

    workspace.restoreAllTabsToMain();

    EXPECT_TRUE(workspace.floatingHosts().empty());
    EXPECT_EQ(workspace.hostTabs(QStringLiteral("main")), QStringList({QStringLiteral("capture"), QStringLiteral("viewer"), QStringLiteral("compress")}));
    EXPECT_EQ(workspace.tabHost(QStringLiteral("viewer")), QStringLiteral("main"));
}

TEST(WorkspaceViewModel, PersistsFloatingLayoutAcrossInstances) {
    isolateSettings(QStringLiteral("persist"));
    QString floatingHost;

    {
        hftrec::gui::WorkspaceViewModel workspace;
        floatingHost = workspace.detachTab(QStringLiteral("viewer"), 420, 280, 1000, 640);
        workspace.setHostGeometry(floatingHost, 500, 320, 1100, 700);
    }

    hftrec::gui::WorkspaceViewModel restored;
    EXPECT_EQ(restored.tabHost(QStringLiteral("viewer")), floatingHost);
    EXPECT_EQ(restored.hostTabs(floatingHost), QStringList({QStringLiteral("viewer")}));
    EXPECT_EQ(restored.hostAtGlobal(520, 340), floatingHost);
}

}  // namespace
