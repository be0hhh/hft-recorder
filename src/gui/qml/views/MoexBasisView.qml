import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HftRecorder 1.0

Pane {
    id: root
    padding: 0
    focus: true
    required property AppViewModel appVm
    required property var backtestVm
    required property bool tabActive

    property color windowColor: "#161616"
    property color chromeColor: "#202024"
    property color panelColor: "#2c2c2f"
    property color panelAltColor: "#35353a"
    property color borderColor: "#49494f"
    property color textColor: "#f5f5f5"
    property color mutedTextColor: "#aaaaaf"
    property color accentBuyColor: "#24c2cb"
    property color chartColor: "#202022"
    property color panelDeepColor: "#161616"

    function syncGroupPicker() {
        groupPicker.rows = basis.groupRows
        groupPicker.setCurrentId(basis.groupPath)
    }

    function syncModePicker() {
        for (var i = 0; i < basis.displayModeChoices.length; ++i) {
            var row = basis.displayModeChoices[i]
            if ((row.id || row.value) === basis.displayMode) {
                modePicker.currentIndex = i
                return
            }
        }
        modePicker.currentIndex = 0
    }

    background: Rectangle { color: root.windowColor }

    MoexBasisController {
        id: basis
        Component.onCompleted: root.syncGroupPicker()
    }
    ViewerInteractionState { id: interaction }
    Timer { id: interactiveModeTimer; interval: 120; repeat: false; onTriggered: interaction.interactiveMode = false }

    Connections {
        target: basis
        function onGroupsChanged() { root.syncGroupPicker() }
        function onDataChanged() { root.syncModePicker() }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            color: root.chromeColor
            implicitHeight: basisControls.implicitHeight + 12

            RowLayout {
                id: basisControls
                anchors.fill: parent
                anchors.margins: 6
                spacing: 8

                Label { text: "Basis group"; color: root.mutedTextColor }

                SessionPickerCombo {
                    id: groupPicker
                    Layout.preferredWidth: 620
                    rows: basis.groupRows
                    caption: "MOEX basis chain"
                    emptyLabel: "Select spot + futures group"
                    popupWidth: 860
                    panelColor: root.panelColor
                    panelAltColor: root.panelAltColor
                    panelDeepColor: root.panelDeepColor
                    borderColor: root.borderColor
                    textColor: root.textColor
                    mutedTextColor: root.mutedTextColor
                    accentColor: root.accentBuyColor
                    onPicked: function(groupPath) { basis.loadGroup(groupPath) }
                }

                Button {
                    id: reloadButton
                    text: "Reload"
                    contentItem: Text {
                        text: reloadButton.text
                        color: root.textColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 7
                        color: reloadButton.down ? root.panelAltColor : root.panelColor
                        border.color: root.borderColor
                        border.width: 1
                    }
                    onClicked: {
                        basis.reloadGroups()
                        if (basis.groupPath !== "")
                            basis.loadGroup(basis.groupPath)
                    }
                }

                Button {
                    id: fitButton
                    text: "Fit"
                    enabled: basis.ready
                    contentItem: Text {
                        text: fitButton.text
                        color: fitButton.enabled ? root.textColor : root.mutedTextColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 7
                        color: fitButton.down ? root.panelAltColor : root.panelColor
                        border.color: fitButton.enabled ? root.borderColor : "#343438"
                        border.width: 1
                    }
                    onClicked: basis.autoFit()
                }

                ComboBox {
                    id: modePicker
                    Layout.preferredWidth: 150
                    model: basis.displayModeChoices
                    textRole: "label"
                    valueRole: "id"
                    onActivated: basis.setDisplayMode(currentValue)
                    Component.onCompleted: root.syncModePicker()
                    contentItem: Text {
                        leftPadding: 9
                        rightPadding: 20
                        text: modePicker.displayText
                        color: root.textColor
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    background: Rectangle {
                        radius: 7
                        color: root.panelColor
                        border.color: root.borderColor
                        border.width: 1
                    }
                }

                SpinBox {
                    id: rankPicker
                    visible: basis.displayMode === "front_rank"
                    Layout.preferredWidth: visible ? 78 : 0
                    from: 1
                    to: 10
                    value: basis.frontRank
                    onValueModified: basis.setFrontRank(value)
                    contentItem: TextInput {
                        text: "F" + rankPicker.value
                        color: root.textColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        readOnly: true
                    }
                    background: Rectangle {
                        radius: 7
                        color: root.panelColor
                        border.color: root.borderColor
                        border.width: 1
                    }
                }

                Button {
                    id: batchButton
                    text: "Run Chain"
                    enabled: basis.groupPath !== "" && root.backtestVm && !root.backtestVm.running
                    contentItem: Text {
                        text: batchButton.text
                        color: batchButton.enabled ? root.textColor : root.mutedTextColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 7
                        color: batchButton.down ? root.panelAltColor : root.panelColor
                        border.color: batchButton.enabled ? root.accentBuyColor : "#343438"
                        border.width: 1
                    }
                    onClicked: root.backtestVm.startBasisChainBatchBacktest(basis.groupPath)
                }

                Label {
                    Layout.fillWidth: true
                    text: basis.statusText + (basis.ready ? " | futures: " + basis.enabledFutureCount + " | points: " + basis.basisPointCount : "")
                    color: root.mutedTextColor
                    elide: Text.ElideRight
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 46
            color: root.chromeColor
            border.color: root.borderColor
            border.width: 1
            clip: true

            Flickable {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                contentWidth: legRow.implicitWidth
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds

                RowLayout {
                    id: legRow
                    height: parent.height
                    spacing: 8

                    Repeater {
                        model: basis.legRows
                        delegate: Rectangle {
                            required property var modelData
                            Layout.preferredWidth: Math.max(170, legLabel.implicitWidth + legRight.implicitWidth + 58)
                            Layout.preferredHeight: 32
                            radius: 7
                            color: modelData.role === "spot" ? root.panelAltColor : root.panelColor
                            border.color: modelData.valid === false ? "#ef6f6c" : (modelData.enabled ? root.accentBuyColor : root.borderColor)
                            border.width: 1

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                spacing: 6

                                CheckBox {
                                    visible: modelData.role === "future"
                                    checked: modelData.enabled === true
                                    onToggled: basis.setFutureEnabled(modelData.futureIndex, checked)
                                }
                                Label {
                                    id: legLabel
                                    Layout.fillWidth: true
                                    text: modelData.role === "spot" ? "SPOT " + modelData.label : modelData.label
                                    color: root.textColor
                                    font.pixelSize: 12
                                    font.bold: modelData.role === "spot"
                                    elide: Text.ElideRight
                                }
                                Label {
                                    id: legRight
                                    text: modelData.rightText || ""
                                    color: modelData.valid === false ? "#ef6f6c" : root.mutedTextColor
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            color: root.chromeColor
            implicitHeight: 28
            Label {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                text: "Top: spot candles and futures overlays. Bottom: basis bps, convergence lines, and expiry markers."
                color: root.mutedTextColor
                font.pixelSize: 12
                elide: Text.ElideRight
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Rectangle { anchors.fill: parent; color: root.chartColor }

            MoexBasisItem {
                id: basisSurface
                anchors.fill: parent
                controller: basis
            }

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: Qt.ArrowCursor
                property real lastX: 0
                property real lastY: 0
                property real pressX: 0
                property real pressY: 0
                property bool dragActive: false
                property string dragValuePanel: "none"

                onPressed: function(mouse) {
                    lastX = mouse.x
                    lastY = mouse.y
                    pressX = mouse.x
                    pressY = mouse.y
                    dragActive = false
                    dragValuePanel = basisSurface.isPricePanelPoint(mouse.x, mouse.y) ? "price" : (basisSurface.isBasisPanelPoint(mouse.x, mouse.y) ? "basis" : "none")
                    interaction.plotDragging = true
                    basisSurface.clearHover()
                }
                onPositionChanged: function(mouse) {
                    if (!(mouse.buttons & Qt.LeftButton) && !(mouse.buttons & Qt.RightButton)) {
                        basisSurface.setHoverPoint(mouse.x, mouse.y)
                        return
                    }
                    if (!dragActive && Math.abs(mouse.x - pressX) + Math.abs(mouse.y - pressY) < 4)
                        return
                    if (!dragActive) {
                        dragActive = true
                        interaction.startInteractiveMode(interactiveModeTimer)
                        basisSurface.clearHover()
                    }
                    var dx = mouse.x - lastX
                    var dy = mouse.y - lastY
                    lastX = mouse.x
                    lastY = mouse.y
                    if (mouse.buttons & Qt.RightButton) {
                        if (dragValuePanel === "price")
                            basis.panPrice(dy / Math.max(1, height))
                        else if (dragValuePanel === "basis")
                            basis.panBasis(dy / Math.max(1, height))
                    } else {
                        basis.panTime(-dx / Math.max(1, width))
                        if (dragValuePanel === "price")
                            basis.panPrice(dy / Math.max(1, height))
                        else if (dragValuePanel === "basis")
                            basis.panBasis(dy / Math.max(1, height))
                    }
                }
                onReleased: {
                    interaction.plotDragging = false
                    if (dragActive) interaction.stopInteractiveModeSoon(interactiveModeTimer)
                    dragActive = false
                    dragValuePanel = "none"
                }
                onCanceled: {
                    interaction.plotDragging = false
                    if (dragActive) interaction.stopInteractiveModeSoon(interactiveModeTimer)
                    dragActive = false
                    dragValuePanel = "none"
                }
                onExited: basisSurface.clearHover()
                onWheel: function(wheel) {
                    var factor = wheel.angleDelta.y > 0 ? 1.3 : (1.0 / 1.3)
                    if (wheel.modifiers & Qt.ControlModifier) {
                        if (basisSurface.isPricePanelPoint(wheel.x, wheel.y))
                            basis.zoomPriceAt(factor, basisSurface.priceAnchorFraction(wheel.y))
                        else
                            basis.zoomBasisAt(factor, basisSurface.basisAnchorFraction(wheel.y))
                    } else {
                        basis.zoomTimeAt(factor, Math.max(0, Math.min(1, wheel.x / Math.max(1, width))))
                    }
                    basisSurface.setHoverPoint(wheel.x, wheel.y)
                    wheel.accepted = true
                }
                onDoubleClicked: basis.autoFit()
            }
        }
    }
}
