import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property var workspaceVm
    required property string hostId
    required property bool floating
    required property var attachTab
    property string activeTabId: ""

    property color panelColor: "#2c2c2f"
    property color panelAltColor: "#3c3c3c"
    property color borderColor: "#3c3d3f"
    property color textColor: "#f5f5f5"
    property color mutedTextColor: "#b6b6b6"
    property color accentBuyColor: "#24c2cb"

    function reloadTabs() {
        tabsRepeater.model = root.workspaceVm.hostTabs(root.hostId)
        root.activeTabId = root.workspaceVm.activeTab(root.hostId)
        root.attachCurrentTab()
    }

    function attachCurrentTab() {
        var active = root.workspaceVm.activeTab(root.hostId)
        root.activeTabId = active
        if (active !== "") root.attachTab(active, contentParent, root.hostId)
    }

    function activateTab(tabId) {
        root.workspaceVm.setActiveTab(root.hostId, tabId)
        root.activeTabId = tabId
        root.attachTab(tabId, contentParent, root.hostId)
    }

    function syncGeometry() {
        var window = root.Window.window
        if (!window) return
        var local = root.mapToItem(null, 0, 0)
        root.workspaceVm.setHostGeometry(root.hostId, window.x + local.x, window.y + local.y, root.width, root.height)
    }

    onHostIdChanged: { reloadTabs(); Qt.callLater(syncGeometry) }
    onWidthChanged: Qt.callLater(syncGeometry)
    onHeightChanged: Qt.callLater(syncGeometry)
    Component.onCompleted: { reloadTabs(); Qt.callLater(syncGeometry) }

    Connections {
        target: root.workspaceVm
        function onLayoutChanged() { root.reloadTabs() }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            id: tabStrip
            Layout.fillWidth: true
            implicitHeight: 46
            color: root.panelColor
            border.color: root.borderColor
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 6

                Repeater {
                    id: tabsRepeater
                    delegate: Rectangle {
                        id: tabButton
                        required property string modelData
                        readonly property bool selected: root.activeTabId === modelData

                        Layout.preferredWidth: Math.max(112, titleText.implicitWidth + 34)
                        Layout.fillHeight: true
                        color: selected ? root.panelAltColor : root.panelColor
                        border.color: selected ? root.accentBuyColor : root.borderColor
                        border.width: 1

                        Text {
                            id: titleText
                            anchors.centerIn: parent
                            text: root.workspaceVm.tabTitle(tabButton.modelData)
                            color: tabButton.selected ? root.textColor : root.mutedTextColor
                            font.pixelSize: 13
                        }

                        MouseArea {
                            id: tabMouse
                            anchors.fill: parent
                            property bool dragged: false
                            property real pressX: 0
                            property real pressY: 0
                            drag.target: dragProxy
                            drag.threshold: 12
                            onClicked: if (!dragged) root.activateTab(tabButton.modelData)
                            onPressed: function(mouse) {
                                dragged = false
                                pressX = mouse.x
                                pressY = mouse.y
                                dragLabel.text = root.workspaceVm.tabTitle(tabButton.modelData)
                                dragProxy.visible = false
                            }
                            onPositionChanged: function(mouse) {
                                if (!dragged && Math.abs(mouse.x - pressX) + Math.abs(mouse.y - pressY) >= 12) {
                                    dragged = true
                                    dragProxy.x = tabButton.mapToItem(localDragOverlay, 0, 0).x
                                    dragProxy.y = tabButton.mapToItem(localDragOverlay, 0, 0).y
                                    dragProxy.visible = true
                                }
                            }
                            onReleased: function(mouse) {
                                dragProxy.visible = false
                                if (!dragged)
                                    return
                                var point = dragProxy.mapToItem(null, dragProxy.width / 2, dragProxy.height / 2)
                                var targetHost = root.workspaceVm.hostAtGlobal(point.x, point.y)
                                if (targetHost !== "" && targetHost !== root.hostId) {
                                    root.workspaceVm.dockTab(tabButton.modelData, targetHost)
                                    return
                                }
                                var localY = tabButton.mapToItem(root, mouse.x, mouse.y).y
                                if (localY < -8 || localY > tabStrip.height + 24) {
                                    root.workspaceVm.detachTab(tabButton.modelData, point.x - 120, point.y - 18, 1120, 760)
                                }
                            }
                        }

                        Rectangle {
                            id: dragProxy
                            parent: localDragOverlay
                            z: 1000
                            width: Math.max(112, dragLabel.implicitWidth + 34)
                            height: 36
                            radius: 6
                            visible: false
                            color: root.panelAltColor
                            border.color: root.accentBuyColor
                            border.width: 1
                            Text { id: dragLabel; anchors.centerIn: parent; color: root.textColor; font.pixelSize: 13 }
                        }
                    }
                }

                Item { Layout.fillWidth: true }
            }
        }

        Item {
            id: contentParent
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
        }
    }

    Item { id: localDragOverlay; anchors.fill: parent; z: 1000 }
}
