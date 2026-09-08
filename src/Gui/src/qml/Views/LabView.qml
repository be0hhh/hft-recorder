import QtQuick
import QtQuick.Controls

Pane {
    QtObject {
        id: palette
        readonly property color window: "#161616"
        readonly property color text: "#f5f5f5"
        readonly property color muted: "#b6b6b6"
    }

    background: Rectangle { color: palette.window }

    Column {
        spacing: 12
        anchors.fill: parent
        anchors.margins: 16
        Label { text: "Compression Lab"; color: palette.text }
        Label { text: "Run baseline and custom pipelines against a recorded session."; color: palette.muted }
    }
}
