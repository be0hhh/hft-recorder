import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: panel

    required property var backtestVm
    property int parameterCount: backtestVm && backtestVm.strategyParameters ? backtestVm.strategyParameters.length : 0
    property color textColor: "#d7dce6"
    property color mutedTextColor: "#8a92a0"
    property color panelColor: "#1c2028"
    property color panelDeepColor: "#151820"
    property color panelAltColor: "#253044"
    property color borderColor: "#343946"
    property color accentColor: "#51a7ff"

    Layout.fillWidth: true
    Layout.preferredHeight: Math.max(78, paramsFlow.childrenRect.height + 16)
    Layout.minimumHeight: Layout.preferredHeight
    Layout.maximumHeight: Layout.preferredHeight
    Layout.leftMargin: 10
    Layout.rightMargin: 10
    color: panel.panelColor
    border.color: panel.borderColor
    radius: 6

    Item {
        anchors.fill: parent
        anchors.margins: 8

        Flow {
            id: paramsFlow
            width: Math.max(1, parent.width)
            spacing: 10

            Repeater {
                model: panel.backtestVm && panel.backtestVm.strategyParameters ? panel.backtestVm.strategyParameters : []
                delegate: Item {
                    required property var modelData
                    property bool choiceRow: modelData.isChoice === true
                    property bool fixedRow: modelData.mode === "fixed"
                    width: 292
                    height: choiceRow || fixedRow ? 62 : 92
                    ToolTip.visible: paramHover.hovered && String(modelData.description || "").length > 0
                    ToolTip.text: modelData.description || ""
                    ToolTip.delay: 350

                    HoverHandler { id: paramHover }

                    Label {
                        x: 0
                        y: 0
                        width: parent.width
                        height: 18
                        text: modelData.label
                        color: panel.textColor
                        font.pixelSize: 11
                        font.bold: true
                        elide: Text.ElideRight
                    }

                    RecorderComboBox {
                        visible: choiceRow
                        x: 0
                        y: 24
                        width: parent.width
                        height: 28
                        caption: ""
                        textRole: "label"
                        valueRole: "id"
                        model: modelData.choices || []
                        popupWidth: 170
                        Component.onCompleted: currentIndex = indexOfValue(modelData.value)
                        onActivated: panel.backtestVm.setStrategyParameterGroup(modelData.group, currentValue)
                    }

                    RecorderComboBox {
                        visible: !choiceRow
                        x: 0
                        y: 24
                        width: 110
                        height: 28
                        caption: ""
                        textRole: "label"
                        valueRole: "id"
                        model: modelData.modeChoices || []
                        popupWidth: 120
                        Component.onCompleted: currentIndex = indexOfValue(modelData.mode)
                        onActivated: panel.backtestVm.setStrategyParameterMode(modelData.key, currentValue)
                    }

                    TextField {
                        visible: !choiceRow && fixedRow
                        x: 118
                        y: 24
                        width: 112
                        height: 26
                        text: modelData.value
                        selectByMouse: true
                        color: panel.textColor
                        font.pixelSize: 12
                        background: Rectangle { color: panel.panelDeepColor; border.color: panel.borderColor; radius: 5 }
                        onEditingFinished: panel.backtestVm.setStrategyParameter(modelData.key, text)
                    }

                    TextField {
                        id: minField
                        visible: !choiceRow && !fixedRow
                        x: 0
                        y: 56
                        width: 88
                        height: 24
                        placeholderText: "min"
                        text: modelData.min || ""
                        selectByMouse: true
                        color: panel.textColor
                        font.pixelSize: 11
                        background: Rectangle { color: panel.panelDeepColor; border.color: panel.borderColor; radius: 5 }
                        onEditingFinished: panel.backtestVm.setStrategyParameterRange(modelData.key, text, maxField.text, stepField.text)
                    }

                    TextField {
                        id: maxField
                        visible: !choiceRow && !fixedRow
                        x: 96
                        y: 56
                        width: 88
                        height: 24
                        placeholderText: "max"
                        text: modelData.max || ""
                        selectByMouse: true
                        color: panel.textColor
                        font.pixelSize: 11
                        background: Rectangle { color: panel.panelDeepColor; border.color: panel.borderColor; radius: 5 }
                        onEditingFinished: panel.backtestVm.setStrategyParameterRange(modelData.key, minField.text, text, stepField.text)
                    }

                    TextField {
                        id: stepField
                        visible: !choiceRow && !fixedRow
                        x: 192
                        y: 56
                        width: 88
                        height: 24
                        placeholderText: "step"
                        text: modelData.step || ""
                        selectByMouse: true
                        color: panel.textColor
                        font.pixelSize: 11
                        background: Rectangle { color: panel.panelDeepColor; border.color: panel.borderColor; radius: 5 }
                        onEditingFinished: panel.backtestVm.setStrategyParameterRange(modelData.key, minField.text, maxField.text, text)
                    }
                }
            }

            Item {
                width: 150
                height: 62

                Label {
                    x: 0
                    y: 0
                    width: parent.width
                    height: 18
                    text: "Rate limits"
                    color: panel.textColor
                    font.pixelSize: 11
                    font.bold: true
                }

                CheckBox {
                    id: rateLimitsEnabledBox
                    x: 0
                    y: 22
                    width: 78
                    height: 28
                    checked: panel.backtestVm.rateLimitsEnabled
                    text: "On"
                    font.pixelSize: 12
                    palette.text: panel.textColor
                    indicator: Rectangle {
                        implicitWidth: 18
                        implicitHeight: 18
                        width: 18
                        height: 18
                        x: 0
                        y: parent.height / 2 - height / 2
                        radius: 4
                        color: rateLimitsEnabledBox.checked ? panel.panelAltColor : panel.panelDeepColor
                        border.width: 1
                        border.color: rateLimitsEnabledBox.checked ? panel.accentColor : panel.borderColor

                        Rectangle {
                            width: 8
                            height: 8
                            anchors.centerIn: parent
                            radius: 2
                            color: panel.accentColor
                            visible: rateLimitsEnabledBox.checked
                        }
                    }
                    contentItem: Text {
                        leftPadding: rateLimitsEnabledBox.indicator.width + rateLimitsEnabledBox.spacing
                        text: rateLimitsEnabledBox.text
                        color: panel.textColor
                        font: rateLimitsEnabledBox.font
                        verticalAlignment: Text.AlignVCenter
                    }
                    onToggled: panel.backtestVm.rateLimitsEnabled = checked
                }

                CheckBox {
                    id: strictRateLimitsBox
                    x: 68
                    y: 22
                    width: 82
                    height: 28
                    checked: panel.backtestVm.strictRateLimitsEnabled
                    enabled: panel.backtestVm.rateLimitsEnabled
                    opacity: enabled ? 1.0 : 0.45
                    text: "Strict"
                    font.pixelSize: 12
                    palette.text: panel.textColor
                    contentItem: Text {
                        leftPadding: strictRateLimitsBox.indicator.width + strictRateLimitsBox.spacing
                        text: strictRateLimitsBox.text
                        color: panel.textColor
                        font: strictRateLimitsBox.font
                        verticalAlignment: Text.AlignVCenter
                    }
                    onToggled: panel.backtestVm.strictRateLimitsEnabled = checked
                }
            }

            Item {
                width: 620
                height: 62

                Label {
                    x: 0
                    y: 0
                    width: parent.width
                    height: 18
                    text: "Risk"
                    color: panel.textColor
                    font.pixelSize: 11
                    font.bold: true
                }

                CheckBox {
                    id: riskEnabledBox
                    x: 0
                    y: 22
                    width: 62
                    height: 28
                    checked: panel.backtestVm.riskEnabled
                    text: "On"
                    font.pixelSize: 12
                    palette.text: panel.textColor
                    contentItem: Text {
                        leftPadding: riskEnabledBox.indicator.width + riskEnabledBox.spacing
                        text: riskEnabledBox.text
                        color: panel.textColor
                        font: riskEnabledBox.font
                        verticalAlignment: Text.AlignVCenter
                    }
                    onToggled: panel.backtestVm.riskEnabled = checked
                }

                TextField {
                    x: 70
                    y: 24
                    width: 92
                    height: 26
                    placeholderText: "equity %"
                    text: panel.backtestVm.riskMinEquityPct
                    enabled: panel.backtestVm.riskEnabled
                    opacity: enabled ? 1.0 : 0.45
                    selectByMouse: true
                    color: panel.textColor
                    placeholderTextColor: panel.mutedTextColor
                    font.pixelSize: 12
                    background: Rectangle { color: panel.panelDeepColor; border.color: panel.borderColor; radius: 5 }
                    onEditingFinished: panel.backtestVm.riskMinEquityPct = text
                }

                TextField {
                    x: 170
                    y: 24
                    width: 92
                    height: 26
                    placeholderText: "leg %"
                    text: panel.backtestVm.riskMinLegEquityPct
                    enabled: panel.backtestVm.riskEnabled
                    opacity: enabled ? 1.0 : 0.45
                    selectByMouse: true
                    color: panel.textColor
                    placeholderTextColor: panel.mutedTextColor
                    font.pixelSize: 12
                    background: Rectangle { color: panel.panelDeepColor; border.color: panel.borderColor; radius: 5 }
                    onEditingFinished: panel.backtestVm.riskMinLegEquityPct = text
                }

                TextField {
                    x: 270
                    y: 24
                    width: 112
                    height: 26
                    placeholderText: "leg USDT"
                    text: panel.backtestVm.riskMinLegEquityUsdt
                    enabled: panel.backtestVm.riskEnabled
                    opacity: enabled ? 1.0 : 0.45
                    selectByMouse: true
                    color: panel.textColor
                    placeholderTextColor: panel.mutedTextColor
                    font.pixelSize: 12
                    background: Rectangle { color: panel.panelDeepColor; border.color: panel.borderColor; radius: 5 }
                    onEditingFinished: panel.backtestVm.riskMinLegEquityUsdt = text
                }

                TextField {
                    x: 390
                    y: 24
                    width: 120
                    height: 26
                    placeholderText: "max pos USDT"
                    text: panel.backtestVm.riskMaxPositionUsdt
                    enabled: panel.backtestVm.riskEnabled
                    opacity: enabled ? 1.0 : 0.45
                    selectByMouse: true
                    color: panel.textColor
                    placeholderTextColor: panel.mutedTextColor
                    font.pixelSize: 12
                    background: Rectangle { color: panel.panelDeepColor; border.color: panel.borderColor; radius: 5 }
                    onEditingFinished: panel.backtestVm.riskMaxPositionUsdt = text
                }

                TextField {
                    x: 520
                    y: 24
                    width: 92
                    height: 26
                    placeholderText: "RL min"
                    text: panel.backtestVm.riskRateLimitGuardMinRemaining
                    enabled: panel.backtestVm.rateLimitsEnabled
                    opacity: enabled ? 1.0 : 0.45
                    selectByMouse: true
                    color: panel.textColor
                    placeholderTextColor: panel.mutedTextColor
                    font.pixelSize: 12
                    background: Rectangle { color: panel.panelDeepColor; border.color: panel.borderColor; radius: 5 }
                    onEditingFinished: panel.backtestVm.riskRateLimitGuardMinRemaining = text
                }
            }
        }
    }
}
