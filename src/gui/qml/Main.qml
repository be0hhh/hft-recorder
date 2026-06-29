import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HftRecorder 1.0

ApplicationWindow {
    id: root
    width: 1600
    height: 980
    visible: true
    title: "hft-recorder"
    color: "#161616"

    property color panelColor: "#2c2c2f"
    property color panelAltColor: "#3c3c3c"
    property color borderColor: "#3c3d3f"
    property color textColor: "#f5f5f5"
    property color mutedTextColor: "#b6b6b6"
    property color accentBuyColor: "#24c2cb"

    property var createdTabs: ({})
    property var floatingWindows: ({})
    property var compressionVmObject: null
    property string workspaceErrorText: ""
    property var tabPrewarmQueue: []
    property bool tabPrewarmRunning: false

    AppViewModel { id: rootAppVm; objectName: "appVm" }
    CaptureViewModel { id: rootCaptureVm; objectName: "captureVm" }
    BacktestViewModel { id: rootBacktestVm; objectName: "backtestVm" }
    WorkspaceViewModel { id: rootWorkspaceVm; objectName: "workspaceVm" }

    Item { id: inactiveTabStorage; visible: false; anchors.fill: parent }
    Component { id: compressionVmComponent; CompressionViewModel { objectName: "compressionVm" } }
    Component { id: captureComponent; CaptureView { captureVm: rootCaptureVm; tabActive: false } }
    Component { id: viewerComponent; ViewerView { appVm: rootAppVm; captureVm: rootCaptureVm; backtestVm: rootBacktestVm; tabActive: false } }
    Component { id: moexBasisComponent; MoexBasisView { appVm: rootAppVm; backtestVm: rootBacktestVm; tabActive: false } }
    Component { id: compressComponent; CompressView { compressionVm: root.ensureCompressionVm(); tabActive: false } }
    Component { id: backtestsComponent; BacktestResultsView { backtestVm: rootBacktestVm; captureVm: rootCaptureVm; tabActive: false } }
    Component { id: quantComponent; QuantView { backtestVm: rootBacktestVm; tabActive: false } }
    Component { id: resultsComponent; ResultsView { backtestVm: rootBacktestVm; tabActive: false } }

    function ensureCompressionVm() {
        if (compressionVmObject !== null)
            return compressionVmObject
        compressionVmObject = compressionVmComponent.createObject(root)
        if (compressionVmObject === null) {
            workspaceErrorText = "Failed to create compression model"
            console.error(workspaceErrorText)
        }
        return compressionVmObject
    }

    function shouldPrewarmTab(tabId) {
        return false
    }

    function componentForTab(tabId) {
        if (tabId === "capture") return captureComponent
        if (tabId === "viewer") return viewerComponent
        if (tabId === "moex_basis") return moexBasisComponent
        if (tabId === "compress") return compressComponent
        if (tabId === "backtests") return backtestsComponent
        if (tabId === "quant") return quantComponent
        if (tabId === "results") return resultsComponent
        return null
    }

    function ensureTab(tabId) {
        if (createdTabs[tabId] !== undefined)
            return createdTabs[tabId]
        var component = componentForTab(tabId)
        if (component === null)
            return null
        var item = component.createObject(inactiveTabStorage)
        if (item === null) {
            workspaceErrorText = "Failed to create tab: " + tabId
            console.error(workspaceErrorText)
            return null
        }
        item.visible = false
        item.enabled = false
        createdTabs[tabId] = item
        return item
    }

    function queueTabPrewarm() {
        if (tabPrewarmRunning)
            return

        var hosts = rootWorkspaceVm.hosts()
        var activeTabs = ({})
        var seenTabs = ({})
        var queue = []

        for (var i = 0; i < hosts.length; ++i) {
            var active = String(hosts[i].activeTab || "")
            if (active.length > 0)
                activeTabs[active] = true
        }

        for (var h = 0; h < hosts.length; ++h) {
            var tabs = hosts[h].tabs || []
            for (var t = 0; t < tabs.length; ++t) {
                var tabId = String(tabs[t] || "")
                if (tabId.length === 0 || seenTabs[tabId] || activeTabs[tabId])
                    continue
                seenTabs[tabId] = true
                if (!root.shouldPrewarmTab(tabId) || createdTabs[tabId] !== undefined)
                    continue
                queue.push(tabId)
            }
        }

        tabPrewarmQueue = queue
        tabPrewarmRunning = tabPrewarmQueue.length > 0
        if (tabPrewarmRunning)
            tabPrewarmTimer.restart()
    }

    function prewarmNextTab() {
        if (tabPrewarmQueue.length <= 0) {
            tabPrewarmRunning = false
            return
        }

        var tabId = tabPrewarmQueue.shift()
        if (createdTabs[tabId] === undefined) {
            var item = ensureTab(tabId)
            if (item !== null) {
                item.parent = inactiveTabStorage
                item.anchors.fill = inactiveTabStorage
                item.visible = false
                item.enabled = false
                item.tabActive = false
            }
        }

        tabPrewarmRunning = tabPrewarmQueue.length > 0
        if (tabPrewarmRunning)
            tabPrewarmTimer.restart()
    }

    function deactivateHost(hostId) {
        var tabs = rootWorkspaceVm.hostTabs(hostId)
        for (var i = 0; i < tabs.length; ++i) {
            var item = createdTabs[tabs[i]]
            if (item !== undefined) {
                item.tabActive = false
                item.visible = false
                item.enabled = false
                item.parent = inactiveTabStorage
                item.anchors.fill = inactiveTabStorage
            }
        }
    }

    function attachTabToHost(tabId, targetParent, hostId) {
        if (targetParent === null || targetParent === undefined)
            return
        workspaceErrorText = ""
        deactivateHost(hostId)

        var item = ensureTab(tabId)
        if (item === null) {
            workspaceErrorText = "Tab is not available: " + rootWorkspaceVm.tabTitle(tabId)
            return
        }
        item.parent = targetParent
        item.anchors.fill = targetParent
        item.visible = true
        item.enabled = true
        item.tabActive = true
    }

    function syncFloatingWindows() {
        var hosts = rootWorkspaceVm.floatingHosts()
        var wanted = ({})
        for (var i = 0; i < hosts.length; ++i) {
            var host = hosts[i]
            wanted[host.id] = true
            if (floatingWindows[host.id] === undefined) {
                var window = floatingWindowComponent.createObject(root, {
                    workspaceVm: rootWorkspaceVm,
                    hostId: host.id,
                    hostState: host,
                    attachTab: attachTabToHost
                })
                floatingWindows[host.id] = window
            }
        }

        for (var id in floatingWindows) {
            if (wanted[id]) continue
            deactivateHost(id)
            floatingWindows[id].destroy()
            delete floatingWindows[id]
        }
    }

    Timer {
        id: tabPrewarmStartTimer
        interval: 700
        repeat: false
        onTriggered: root.queueTabPrewarm()
    }

    Timer {
        id: tabPrewarmTimer
        interval: 350
        repeat: false
        onTriggered: root.prewarmNextTab()
    }

    onClosing: {
        for (var id in floatingWindows)
            floatingWindows[id].applicationClosing = true
    }

    header: ToolBar {
        contentHeight: 48
        background: Rectangle { color: root.panelColor; border.color: root.borderColor; border.width: 1 }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 16
            Label { text: "hft-recorder"; font.bold: true; font.pixelSize: 20; color: root.textColor }
            Label { text: rootAppVm.renderDiagnosticsText; color: root.mutedTextColor; font.pixelSize: 12 }
            Item { Layout.fillWidth: true }
            Button {
                text: "Reset layout"
                visible: rootWorkspaceVm.floatingHosts().length > 0
                onClicked: rootWorkspaceVm.restoreAllTabsToMain()
            }
        }
    }

    WorkspaceHost {
        id: mainHost
        anchors.fill: parent
        workspaceVm: rootWorkspaceVm
        hostId: rootWorkspaceVm.mainHostId
        floating: false
        attachTab: root.attachTabToHost
        panelColor: root.panelColor
        panelAltColor: root.panelAltColor
        borderColor: root.borderColor
        textColor: root.textColor
        mutedTextColor: root.mutedTextColor
        accentBuyColor: root.accentBuyColor
    }

    Rectangle {
        anchors.fill: parent
        anchors.topMargin: 94
        visible: root.workspaceErrorText !== ""
        color: root.color
        z: 100

        Label {
            anchors.centerIn: parent
            text: root.workspaceErrorText
            color: root.textColor
        }
    }

    Component {
        id: floatingWindowComponent
        WorkspaceFloatingWindow {}
    }

    Connections {
        target: rootWorkspaceVm
        function onLayoutChanged() { root.syncFloatingWindows() }
    }

    Component.onCompleted: {
        root.syncFloatingWindows()
        tabPrewarmStartTimer.start()
    }
}
