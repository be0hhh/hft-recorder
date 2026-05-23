import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ComboBox {
    id: combo

    property string caption: ""
    property color panelColor: "#1b1d23"
    property color panelAltColor: "#22252d"
    property color panelDeepColor: "#15171c"
    property color borderColor: "#343844"
    property color textColor: "#f1f4f8"
    property color mutedTextColor: "#a8afbd"
    property color accentColor: "#24c2cb"
    property int popupWidth: width

    Layout.fillWidth: true
    Layout.preferredHeight: 42
    font.pixelSize: 12

    contentItem: Column {
        leftPadding: 10
        rightPadding: 28
        topPadding: 5
        spacing: 2
        Text { text: combo.caption; color: combo.mutedTextColor; font.pixelSize: 10; elide: Text.ElideRight; width: combo.width - 40 }
        Text { text: combo.displayText.length > 0 ? combo.displayText : "not selected"; color: combo.textColor; font.pixelSize: 12; font.bold: true; elide: Text.ElideRight; width: combo.width - 40 }
    }

    indicator: Text { x: combo.width - 22; y: 15; text: "v"; color: combo.mutedTextColor; font.pixelSize: 12 }
    background: Rectangle { radius: 7; color: combo.panelDeepColor; border.color: combo.hovered ? combo.accentColor : combo.borderColor; border.width: 1 }

    popup: Popup {
        y: combo.height + 4
        width: Math.max(combo.width, combo.popupWidth)
        implicitHeight: Math.min(contentItem.implicitHeight, 360)
        padding: 1
        background: Rectangle { color: combo.panelColor; border.color: combo.borderColor; radius: 7 }
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: combo.popup.visible ? combo.delegateModel : null
            currentIndex: combo.highlightedIndex
        }
    }

    delegate: ItemDelegate {
        required property var modelData
        width: combo.popup.width
        height: 32
        contentItem: Text {
            leftPadding: 10
            rightPadding: 10
            text: modelData.label || modelData.id || ""
            color: combo.textColor
            font.pixelSize: 12
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle { color: highlighted ? Qt.rgba(combo.accentColor.r, combo.accentColor.g, combo.accentColor.b, 0.16) : combo.panelColor }
    }
}
