import QtQuick
import QtQuick.Controls

Window {
    id: root

    required property var workspaceVm
    required property string hostId
    required property var attachTab

    property var hostState: ({})
    property bool applicationClosing: false

    title: "hft-recorder"
    color: "#161616"
    visible: true
    x: hostState.x === undefined ? 160 : hostState.x
    y: hostState.y === undefined ? 120 : hostState.y
    width: hostState.width === undefined ? 1120 : hostState.width
    height: hostState.height === undefined ? 760 : hostState.height

    function persistGeometry() {
        root.workspaceVm.setHostGeometry(root.hostId, root.x, root.y, root.width, root.height)
    }

    onXChanged: persistGeometry()
    onYChanged: persistGeometry()
    onWidthChanged: persistGeometry()
    onHeightChanged: persistGeometry()

    onClosing: function(close) {
        if (root.applicationClosing)
            return
        close.accepted = false
        root.workspaceVm.closeHost(root.hostId)
    }

    WorkspaceHost {
        anchors.fill: parent
        workspaceVm: root.workspaceVm
        hostId: root.hostId
        floating: true
        attachTab: root.attachTab
    }
}
