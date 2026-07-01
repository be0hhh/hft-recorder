import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: table

    required property var backtestVm
    property color panelDeepColor: "#15171c"
    property color borderColor: "#343844"
    property color textColor: "#f1f4f8"
    property color mutedTextColor: "#a8afbd"

    implicitHeight: Math.min(320, Math.max(104, 44 + backtestVm.selectedSessionLegs.length * 74))

    component SeedField: Item {
        property string caption: ""
        property alias text: input.text
        property int fieldWidth: 82
        signal edited(string value)

        Layout.preferredWidth: fieldWidth
        Layout.minimumWidth: fieldWidth
        Layout.preferredHeight: 42

        ColumnLayout {
            anchors.fill: parent
            spacing: 2
            Label { text: caption; color: table.mutedTextColor; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
            TextField {
                id: input
                Layout.fillWidth: true
                Layout.preferredHeight: 24
                color: table.textColor
                font.pixelSize: 12
                selectByMouse: true
                background: Rectangle { radius: 5; color: table.panelDeepColor; border.color: table.borderColor; border.width: 1 }
                onEditingFinished: edited(text)
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"

        Flickable {
            id: executionFlick
            property int tableWidth: 1160
            anchors.fill: parent
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            contentWidth: Math.max(width, tableWidth)
            contentHeight: executionContent.implicitHeight

            ColumnLayout {
                id: executionContent
                width: Math.max(executionFlick.width, executionFlick.tableWidth)
                spacing: 4

                RowLayout {
                    width: executionContent.width
                    spacing: 8
                    Label { text: "Use"; color: table.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 38 }
                    Label { text: "Leg"; color: table.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 190 }
                    Label { text: "Balance"; color: table.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 82 }
                    Label { text: "MD base"; color: table.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 70 }
                    Label { text: "MD jit"; color: table.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 70 }
                    Label { text: "Mkt base"; color: table.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 74 }
                    Label { text: "Mkt jit"; color: table.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 74 }
                    Label { text: "Limit base"; color: table.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 78 }
                    Label { text: "Limit jit"; color: table.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 78 }
                    Label { text: "Cancel base"; color: table.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 82 }
                    Label { text: "Cancel jit"; color: table.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 78 }
                    Label { text: "User base"; color: table.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 78 }
                    Label { text: "User jit"; color: table.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 74 }
                    SeedField {
                        caption: "Seed"
                        fieldWidth: 82
                        text: table.backtestVm.latencySeed
                        onEdited: function(value) { table.backtestVm.latencySeed = value }
                    }
                    Item { Layout.fillWidth: true }
                }

                Repeater {
                    model: table.backtestVm.selectedSessionLegs
                    delegate: ColumnLayout {
                        required property var modelData
                        width: executionContent.width
                        spacing: 4

                        RowLayout {
                            width: parent.width
                            spacing: 8
                            CheckBox {
                                checked: modelData.enabled !== false
                                Layout.preferredWidth: 38
                                Layout.preferredHeight: 24
                                onToggled: table.backtestVm.setSessionLegEnabled(modelData.path, checked)
                            }
                            Label {
                                text: modelData.label
                                color: modelData.enabled === false ? table.mutedTextColor : table.textColor
                                font.pixelSize: 12
                                elide: Text.ElideRight
                                Layout.preferredWidth: 190
                            }
                            TextField {
                                text: modelData.initialBalanceUsdt
                                selectByMouse: true
                                color: table.textColor
                                font.pixelSize: 12
                                Layout.preferredWidth: 82
                                background: Rectangle { color: table.panelDeepColor; border.color: table.borderColor; radius: 5 }
                                onEditingFinished: table.backtestVm.setVenueExecutionValue(modelData.index, "initial_balance_usdt", text)
                            }
                            TextField {
                                text: modelData.marketDataLatencyUs
                                selectByMouse: true
                                color: table.textColor
                                font.pixelSize: 12
                                Layout.preferredWidth: 70
                                background: Rectangle { color: table.panelDeepColor; border.color: table.borderColor; radius: 5 }
                                onEditingFinished: table.backtestVm.setVenueExecutionValue(modelData.index, "market_data_latency_us", text)
                            }
                            TextField {
                                text: modelData.marketDataJitterUs
                                selectByMouse: true
                                color: table.textColor
                                font.pixelSize: 12
                                Layout.preferredWidth: 70
                                background: Rectangle { color: table.panelDeepColor; border.color: table.borderColor; radius: 5 }
                                onEditingFinished: table.backtestVm.setVenueExecutionValue(modelData.index, "market_data_jitter_us", text)
                            }
                            TextField {
                                text: modelData.marketOrderLatencyUs
                                selectByMouse: true
                                color: table.textColor
                                font.pixelSize: 12
                                Layout.preferredWidth: 74
                                background: Rectangle { color: table.panelDeepColor; border.color: table.borderColor; radius: 5 }
                                onEditingFinished: table.backtestVm.setVenueExecutionValue(modelData.index, "market_order_latency_us", text)
                            }
                            TextField {
                                text: modelData.marketOrderJitterUs
                                selectByMouse: true
                                color: table.textColor
                                font.pixelSize: 12
                                Layout.preferredWidth: 74
                                background: Rectangle { color: table.panelDeepColor; border.color: table.borderColor; radius: 5 }
                                onEditingFinished: table.backtestVm.setVenueExecutionValue(modelData.index, "market_order_jitter_us", text)
                            }
                            TextField {
                                text: modelData.limitOrderLatencyUs
                                selectByMouse: true
                                color: table.textColor
                                font.pixelSize: 12
                                Layout.preferredWidth: 78
                                background: Rectangle { color: table.panelDeepColor; border.color: table.borderColor; radius: 5 }
                                onEditingFinished: table.backtestVm.setVenueExecutionValue(modelData.index, "limit_order_latency_us", text)
                            }
                            TextField {
                                text: modelData.limitOrderJitterUs
                                selectByMouse: true
                                color: table.textColor
                                font.pixelSize: 12
                                Layout.preferredWidth: 78
                                background: Rectangle { color: table.panelDeepColor; border.color: table.borderColor; radius: 5 }
                                onEditingFinished: table.backtestVm.setVenueExecutionValue(modelData.index, "limit_order_jitter_us", text)
                            }
                            TextField {
                                text: modelData.cancelOrderLatencyUs
                                selectByMouse: true
                                color: table.textColor
                                font.pixelSize: 12
                                Layout.preferredWidth: 82
                                background: Rectangle { color: table.panelDeepColor; border.color: table.borderColor; radius: 5 }
                                onEditingFinished: table.backtestVm.setVenueExecutionValue(modelData.index, "cancel_order_latency_us", text)
                            }
                            TextField {
                                text: modelData.cancelOrderJitterUs
                                selectByMouse: true
                                color: table.textColor
                                font.pixelSize: 12
                                Layout.preferredWidth: 78
                                background: Rectangle { color: table.panelDeepColor; border.color: table.borderColor; radius: 5 }
                                onEditingFinished: table.backtestVm.setVenueExecutionValue(modelData.index, "cancel_order_jitter_us", text)
                            }
                            TextField {
                                text: modelData.userDataLatencyUs
                                selectByMouse: true
                                color: table.textColor
                                font.pixelSize: 12
                                Layout.preferredWidth: 78
                                background: Rectangle { color: table.panelDeepColor; border.color: table.borderColor; radius: 5 }
                                onEditingFinished: table.backtestVm.setVenueExecutionValue(modelData.index, "user_data_latency_us", text)
                            }
                            TextField {
                                text: modelData.userDataJitterUs
                                selectByMouse: true
                                color: table.textColor
                                font.pixelSize: 12
                                Layout.preferredWidth: 74
                                background: Rectangle { color: table.panelDeepColor; border.color: table.borderColor; radius: 5 }
                                onEditingFinished: table.backtestVm.setVenueExecutionValue(modelData.index, "user_data_jitter_us", text)
                            }
                            Item { Layout.fillWidth: true }
                        }

                        Label {
                            text: modelData.executionPresetSummary
                            color: table.mutedTextColor
                            font.pixelSize: 11
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            Layout.leftMargin: 0
                            Layout.fillWidth: true
                        }
                        Label {
                            visible: table.backtestVm.selectedSessionCount > 1 && !table.backtestVm.canRun
                            text: visible ? table.backtestVm.statusText : ""
                            color: "#ef6f6c"
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                }
            }
        }
    }
}
