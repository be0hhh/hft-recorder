import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HftRecorder 1.0

Pane {
    id: root

    required property CaptureViewModel captureVm
    required property bool tabActive
    property bool anyChannelRunning: root.captureVm.tradesRunning || root.captureVm.liquidationsRunning || root.captureVm.bookTickerRunning || root.captureVm.orderbookRunning || root.captureVm.markPriceRunning || root.captureVm.indexPriceRunning || root.captureVm.fundingRunning || root.captureVm.priceLimitRunning

    property color windowColor: "#161616"
    property color panelColor: "#2c2c2f"
    property color panelAltColor: "#3c3c3c"
    property color borderColor: "#3c3d3f"
    property color textColor: "#f5f5f5"
    property color mutedTextColor: "#b6b6b6"
    property color accentBuyColor: "#24c2cb"
    property color accentRequiredColor: "#1ed8ff"
    property color accentOptionalColor: "#179ca4"
    property color accentSellColor: "#da2536"

    background: Rectangle { color: root.windowColor }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: 16

            Label { text: "Live Capture"; font.pixelSize: 26; font.bold: true; color: root.textColor }
            Label { text: root.captureVm.captureAvailable ? "Multi-venue spot / futures / margin / canonical normalized JSON corpus" : root.captureVm.captureUnavailableReason; color: root.captureVm.captureAvailable ? root.mutedTextColor : root.accentSellColor; wrapMode: Text.WordWrap }

            CaptureSessionSummaryCard {
                captureVm: root.captureVm
                panelColor: root.panelColor
                panelAltColor: root.panelAltColor
                borderColor: root.borderColor
                textColor: root.textColor
                mutedTextColor: root.mutedTextColor
                accentBuyColor: root.accentBuyColor
            }

            Rectangle {
                Layout.fillWidth: true
                radius: 12
                color: root.panelColor
                border.color: root.borderColor
                border.width: 1
                implicitHeight: controlsColumn.implicitHeight + 24

                ColumnLayout {
                    id: controlsColumn
                    enabled: root.captureVm.captureAvailable
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12

                    Label { text: "Capture Controls"; font.bold: true; color: root.textColor }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Label {
                            text: "Venues and markets"
                            color: root.mutedTextColor
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            TextField {
                                Layout.fillWidth: true
                                text: root.captureVm.symbolsText
                                placeholderText: "Global symbols, for example BTCUSDT ETHUSDT"
                                selectByMouse: true
                                color: root.textColor
                                placeholderTextColor: root.mutedTextColor
                                onTextEdited: root.captureVm.symbolsText = text
                                onAccepted: {
                                    root.captureVm.applyGlobalSymbolsToVenues()
                                    venueGrid.refreshVenueSymbolFields()
                                }
                                background: Rectangle {
                                    radius: 8
                                    color: root.panelAltColor
                                    border.color: root.borderColor
                                    border.width: 1
                                }
                            }

                            CaptureAccentActionButton {
                                text: "Apply to venues"
                                accentColor: root.accentRequiredColor
                                actionTextColor: "#071419"
                                mutedTextColor: root.mutedTextColor
                                enabled: root.captureVm.normalizedSymbolsText !== ""
                                onClicked: {
                                    root.captureVm.applyGlobalSymbolsToVenues()
                                    venueGrid.refreshVenueSymbolFields()
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            TextField {
                                Layout.fillWidth: true
                                text: root.captureVm.envPath
                                placeholderText: "Env file"
                                selectByMouse: true
                                enabled: !root.anyChannelRunning
                                color: root.textColor
                                placeholderTextColor: root.mutedTextColor
                                onTextEdited: root.captureVm.envPath = text
                                background: Rectangle {
                                    radius: 8
                                    color: root.panelAltColor
                                    border.color: root.borderColor
                                    border.width: 1
                                }
                            }

                            Label {
                                text: "API slot"
                                color: root.mutedTextColor
                            }

                            SpinBox {
                                from: 1
                                to: 255
                                value: root.captureVm.apiSlot
                                editable: true
                                enabled: !root.anyChannelRunning
                                onValueModified: root.captureVm.apiSlot = value
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            radius: 8
                            color: root.panelAltColor
                            border.color: root.borderColor
                            border.width: 1
                            implicitHeight: detailedCandlesColumn.implicitHeight + 20

                            ColumnLayout {
                                id: detailedCandlesColumn
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 10

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10

                                    Label {
                                        Layout.fillWidth: true
                                        text: "Detailed Candles v2"
                                        font.bold: true
                                        color: root.textColor
                                    }

                                    RecorderComboBox {
                                        id: detailedCandlesModeCombo
                                        Layout.preferredWidth: 150
                                        Layout.minimumWidth: 140
                                        caption: "Mode"
                                        textRole: "label"
                                        valueRole: "value"
                                        popupWidth: 220
                                        panelColor: root.panelColor
                                        panelAltColor: root.panelAltColor
                                        panelDeepColor: root.panelColor
                                        borderColor: root.borderColor
                                        textColor: root.textColor
                                        mutedTextColor: root.mutedTextColor
                                        accentColor: root.accentRequiredColor
                                        model: root.captureVm.detailedCandlesModeChoices
                                        Component.onCompleted: currentIndex = Math.max(0, indexOfValue(root.captureVm.detailedCandlesMode))
                                        onActivated: root.captureVm.detailedCandlesMode = currentValue
                                    }

                                    Label {
                                        text: "Rows " + root.captureVm.candles2Count
                                        color: root.mutedTextColor
                                    }
                                }

                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: 5
                                    columnSpacing: 12
                                    rowSpacing: 8

                                    Label {
                                        text: root.captureVm.detailedCandlesMode === "basis_chain" ? "Spot" : "Leg 1"
                                        color: root.mutedTextColor
                                    }

                                    RecorderComboBox {
                                        id: detailedCandlesLeg1VenueCombo
                                        Layout.preferredWidth: 170
                                        Layout.minimumWidth: 150
                                        caption: "Venue"
                                        textRole: "label"
                                        valueRole: "key"
                                        popupWidth: 240
                                        panelColor: root.panelColor
                                        panelAltColor: root.panelAltColor
                                        panelDeepColor: root.panelColor
                                        borderColor: root.borderColor
                                        textColor: root.textColor
                                        mutedTextColor: root.mutedTextColor
                                        accentColor: root.accentRequiredColor
                                        model: root.captureVm.detailedCandlesVenueChoices
                                        Component.onCompleted: currentIndex = Math.max(0, indexOfValue(root.captureVm.detailedCandlesLeg1VenueKey))
                                        onActivated: root.captureVm.detailedCandlesLeg1VenueKey = currentValue
                                    }

                                    TextField {
                                        id: detailedCandlesLeg1SymbolField
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 180
                                        text: root.captureVm.detailedCandlesLeg1SymbolsText
                                        placeholderText: root.captureVm.venueSymbolPlaceholder(root.captureVm.detailedCandlesLeg1VenueKey)
                                        selectByMouse: true
                                        color: root.textColor
                                        placeholderTextColor: root.mutedTextColor
                                        onTextEdited: root.captureVm.detailedCandlesLeg1SymbolsText = text
                                        onAccepted: detailedCandlesColumn.openDetailedSymbolSuggestions(1)
                                        background: Rectangle {
                                            radius: 8
                                            color: root.panelAltColor
                                            border.color: root.borderColor
                                            border.width: 1
                                        }
                                    }

                                    CaptureAccentActionButton {
                                        Layout.preferredWidth: 82
                                        text: "Find"
                                        accentColor: root.accentRequiredColor
                                        actionTextColor: "#071419"
                                        mutedTextColor: root.mutedTextColor
                                        onClicked: detailedCandlesColumn.openDetailedSymbolSuggestions(1)
                                    }

                                    ColumnLayout {
                                        Layout.preferredWidth: 170
                                        Layout.minimumWidth: 155
                                        spacing: 4

                                        Label {
                                            Layout.fillWidth: true
                                            text: root.captureVm.detailedCandlesLimitHint
                                            color: root.mutedTextColor
                                            elide: Text.ElideRight
                                            font.pixelSize: 11
                                        }

                                        SpinBox {
                                            Layout.fillWidth: true
                                            from: 1
                                            to: 1000000
                                            stepSize: 1000
                                            editable: true
                                            value: root.captureVm.detailedCandlesLimit
                                            onValueModified: root.captureVm.detailedCandlesLimit = value
                                        }
                                    }

                                    Label {
                                        text: "Leg 2"
                                        color: root.mutedTextColor
                                        visible: root.captureVm.detailedCandlesMode !== "single" && root.captureVm.detailedCandlesMode !== "basis_chain"
                                    }

                                    RecorderComboBox {
                                        id: detailedCandlesLeg2VenueCombo
                                        visible: root.captureVm.detailedCandlesMode !== "single" && root.captureVm.detailedCandlesMode !== "basis_chain"
                                        Layout.preferredWidth: 170
                                        Layout.minimumWidth: 150
                                        caption: "Venue"
                                        textRole: "label"
                                        valueRole: "key"
                                        popupWidth: 240
                                        panelColor: root.panelColor
                                        panelAltColor: root.panelAltColor
                                        panelDeepColor: root.panelColor
                                        borderColor: root.borderColor
                                        textColor: root.textColor
                                        mutedTextColor: root.mutedTextColor
                                        accentColor: root.accentRequiredColor
                                        model: root.captureVm.detailedCandlesVenueChoices
                                        Component.onCompleted: currentIndex = Math.max(0, indexOfValue(root.captureVm.detailedCandlesLeg2VenueKey))
                                        onActivated: root.captureVm.detailedCandlesLeg2VenueKey = currentValue
                                    }

                                    TextField {
                                        id: detailedCandlesLeg2SymbolField
                                        visible: root.captureVm.detailedCandlesMode !== "single" && root.captureVm.detailedCandlesMode !== "basis_chain"
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 180
                                        text: root.captureVm.detailedCandlesLeg2SymbolsText
                                        placeholderText: root.captureVm.venueSymbolPlaceholder(root.captureVm.detailedCandlesLeg2VenueKey)
                                        selectByMouse: true
                                        color: root.textColor
                                        placeholderTextColor: root.mutedTextColor
                                        enabled: root.captureVm.detailedCandlesLeg1SymbolsText !== ""
                                        onTextEdited: root.captureVm.detailedCandlesLeg2SymbolsText = text
                                        onAccepted: detailedCandlesColumn.openDetailedSymbolSuggestions(2)
                                        background: Rectangle {
                                            radius: 8
                                            color: root.panelAltColor
                                            border.color: root.borderColor
                                            border.width: 1
                                        }
                                    }

                                    CaptureAccentActionButton {
                                        visible: root.captureVm.detailedCandlesMode !== "single" && root.captureVm.detailedCandlesMode !== "basis_chain"
                                        Layout.preferredWidth: 82
                                        text: "Find"
                                        accentColor: root.accentRequiredColor
                                        actionTextColor: "#071419"
                                        mutedTextColor: root.mutedTextColor
                                        enabled: root.captureVm.detailedCandlesLeg1SymbolsText !== ""
                                        onClicked: detailedCandlesColumn.openDetailedSymbolSuggestions(2)
                                    }

                                    RecorderComboBox {
                                        id: detailedCandlesTimeframeCombo
                                        Layout.preferredWidth: 170
                                        Layout.minimumWidth: 150
                                        caption: "Timeframe"
                                        textRole: "label"
                                        valueRole: "value"
                                        popupWidth: 240
                                        panelColor: root.panelColor
                                        panelAltColor: root.panelAltColor
                                        panelDeepColor: root.panelColor
                                        borderColor: root.borderColor
                                        textColor: root.textColor
                                        mutedTextColor: root.mutedTextColor
                                        accentColor: root.accentRequiredColor
                                        model: root.captureVm.detailedCandlesTimeframeChoices
                                        Component.onCompleted: currentIndex = Math.max(0, indexOfValue(root.captureVm.detailedCandlesTimeframe))
                                        onActivated: root.captureVm.detailedCandlesTimeframe = currentValue
                                    }

                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10

                                    Label {
                                        text: "End"
                                        color: root.mutedTextColor
                                    }

                                    RecorderComboBox {
                                        id: detailedCandlesEndModeCombo
                                        Layout.preferredWidth: 170
                                        Layout.minimumWidth: 150
                                        caption: "End"
                                        textRole: "label"
                                        valueRole: "value"
                                        popupWidth: 220
                                        panelColor: root.panelColor
                                        panelAltColor: root.panelAltColor
                                        panelDeepColor: root.panelColor
                                        borderColor: root.borderColor
                                        textColor: root.textColor
                                        mutedTextColor: root.mutedTextColor
                                        accentColor: root.accentRequiredColor
                                        model: root.captureVm.detailedCandlesEndModeChoices
                                        Component.onCompleted: currentIndex = Math.max(0, indexOfValue(root.captureVm.detailedCandlesEndMode))
                                        onActivated: root.captureVm.detailedCandlesEndMode = currentValue
                                    }

                                    TextField {
                                        id: detailedCandlesManualEndField
                                        Layout.preferredWidth: 220
                                        Layout.minimumWidth: 180
                                        visible: root.captureVm.detailedCandlesEndMode === "manual_utc"
                                        text: root.captureVm.detailedCandlesEndUtcText
                                        placeholderText: "2026-06-19 20:45:00Z"
                                        selectByMouse: true
                                        color: root.textColor
                                        placeholderTextColor: root.mutedTextColor
                                        onTextEdited: root.captureVm.detailedCandlesEndUtcText = text
                                        background: Rectangle {
                                            radius: 8
                                            color: root.panelAltColor
                                            border.color: root.borderColor
                                            border.width: 1
                                        }
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: root.captureVm.detailedCandlesResolvedEndText
                                        color: root.mutedTextColor
                                        elide: Text.ElideRight
                                    }
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    visible: root.captureVm.detailedCandlesMode === "basis_chain"
                                    implicitHeight: visible ? basisChainColumn.implicitHeight + 16 : 0
                                    radius: 8
                                    color: root.panelColor
                                    border.color: root.borderColor
                                    border.width: 1
                                    clip: true

                                    ColumnLayout {
                                        id: basisChainColumn
                                        anchors.fill: parent
                                        anchors.margins: 8
                                        spacing: 8

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 8

                                            Label {
                                                Layout.fillWidth: true
                                                text: root.captureVm.detailedCandlesBasisStatus === "" ? "FINAM futures chain" : root.captureVm.detailedCandlesBasisStatus
                                                color: root.mutedTextColor
                                                elide: Text.ElideRight
                                            }

                                            Label {
                                                text: "Max"
                                                color: root.mutedTextColor
                                            }

                                            SpinBox {
                                                Layout.preferredWidth: 96
                                                from: 1
                                                to: 80
                                                editable: true
                                                value: root.captureVm.detailedCandlesBasisMaxFutures
                                                onValueModified: root.captureVm.detailedCandlesBasisMaxFutures = value
                                            }

                                            CaptureAccentActionButton {
                                                Layout.preferredWidth: 112
                                                text: "Build Chain"
                                                accentColor: root.accentRequiredColor
                                                actionTextColor: "#071419"
                                                mutedTextColor: root.mutedTextColor
                                                onClicked: root.captureVm.refreshDetailedCandlesBasisCandidates()
                                            }
                                        }

                                        Rectangle {
                                            Layout.fillWidth: true
                                            implicitHeight: Math.min(286, Math.max(48, basisChainList.contentHeight + 2))
                                            radius: 6
                                            color: root.panelAltColor
                                            border.color: root.borderColor
                                            border.width: 1
                                            clip: true

                                            ListView {
                                                id: basisChainList
                                                anchors.fill: parent
                                                clip: true
                                                model: root.captureVm.detailedCandlesBasisCandidateRows
                                                delegate: Rectangle {
                                                    required property int index
                                                    required property var modelData
                                                    width: basisChainList.width
                                                    height: 44
                                                    color: index % 2 === 0 ? root.panelAltColor : root.panelColor

                                                    RowLayout {
                                                        anchors.fill: parent
                                                        anchors.leftMargin: 8
                                                        anchors.rightMargin: 8
                                                        spacing: 8

                                                        CheckBox {
                                                            checked: modelData.enabled === true
                                                            onToggled: root.captureVm.setDetailedCandlesBasisCandidateEnabled(index, checked)
                                                        }

                                                        Label {
                                                            Layout.preferredWidth: 128
                                                            text: modelData.symbol || ""
                                                            color: root.textColor
                                                            font.bold: modelData.enabled === true
                                                            elide: Text.ElideRight
                                                        }

                                                        Label {
                                                            Layout.fillWidth: true
                                                            text: modelData.detail || ""
                                                            color: root.mutedTextColor
                                                            font.pixelSize: 11
                                                            elide: Text.ElideRight
                                                        }

                                                        Label {
                                                            Layout.preferredWidth: 150
                                                            text: modelData.rightText || ""
                                                            color: modelData.archived === true ? root.mutedTextColor : root.accentBuyColor
                                                            font.pixelSize: 11
                                                            horizontalAlignment: Text.AlignRight
                                                            elide: Text.ElideRight
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }

                                function openDetailedSymbolSuggestions(leg) {
                                    var venueKey = leg === 2 ? root.captureVm.detailedCandlesLeg2VenueKey : root.captureVm.detailedCandlesLeg1VenueKey
                                    var query = leg === 2 ? detailedCandlesLeg2SymbolField.text : detailedCandlesLeg1SymbolField.text
                                    symbolSuggestPopup.targetLeg = leg
                                    symbolSuggestPopup.rows = root.captureVm.detailedCandlesSymbolSuggestions(
                                        venueKey,
                                        query,
                                        root.captureVm.detailedCandlesLeg1VenueKey,
                                        root.captureVm.detailedCandlesLeg1SymbolsText)
                                    symbolSuggestPopup.open()
                                }

                                Popup {
                                    id: symbolSuggestPopup
                                    property int targetLeg: 1
                                    property var rows: []
                                    modal: false
                                    focus: true
                                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                                    x: Math.max(0, (detailedCandlesColumn.width - width) / 2)
                                    y: 88
                                    width: Math.min(detailedCandlesColumn.width - 20, 640)
                                    height: Math.min(320, Math.max(86, symbolSuggestionsList.contentHeight + 18))
                                    padding: 8
                                    background: Rectangle {
                                        radius: 8
                                        color: root.panelColor
                                        border.color: root.borderColor
                                        border.width: 1
                                    }

                                    ListView {
                                        id: symbolSuggestionsList
                                        anchors.fill: parent
                                        clip: true
                                        model: symbolSuggestPopup.rows
                                        delegate: Rectangle {
                                            required property var modelData
                                            width: symbolSuggestionsList.width
                                            height: 54
                                            color: mouseArea.containsMouse ? root.panelAltColor : root.panelColor

                                            ColumnLayout {
                                                anchors.fill: parent
                                                anchors.leftMargin: 10
                                                anchors.rightMargin: 10
                                                spacing: 2

                                                Label {
                                                    Layout.fillWidth: true
                                                    text: modelData.label
                                                    color: root.textColor
                                                    elide: Text.ElideRight
                                                }

                                                Label {
                                                    Layout.fillWidth: true
                                                    text: modelData.detail
                                                    color: root.mutedTextColor
                                                    font.pixelSize: 11
                                                    elide: Text.ElideRight
                                                }
                                            }

                                            MouseArea {
                                                id: mouseArea
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                onClicked: {
                                                    root.captureVm.applyDetailedCandlesSymbolSuggestion(symbolSuggestPopup.targetLeg, modelData.symbol)
                                                    symbolSuggestPopup.close()
                                                }
                                            }
                                        }
                                    }
                                }

                                Connections {
                                    target: root.captureVm
                                    function onDetailedCandlesChanged() {
                                        var modeIdx = detailedCandlesModeCombo.indexOfValue(root.captureVm.detailedCandlesMode)
                                        if (modeIdx >= 0 && detailedCandlesModeCombo.currentIndex !== modeIdx)
                                            detailedCandlesModeCombo.currentIndex = modeIdx

                                        var leg1VenueIdx = detailedCandlesLeg1VenueCombo.indexOfValue(root.captureVm.detailedCandlesLeg1VenueKey)
                                        if (leg1VenueIdx >= 0 && detailedCandlesLeg1VenueCombo.currentIndex !== leg1VenueIdx)
                                            detailedCandlesLeg1VenueCombo.currentIndex = leg1VenueIdx

                                        var leg2VenueIdx = detailedCandlesLeg2VenueCombo.indexOfValue(root.captureVm.detailedCandlesLeg2VenueKey)
                                        if (leg2VenueIdx >= 0 && detailedCandlesLeg2VenueCombo.currentIndex !== leg2VenueIdx)
                                            detailedCandlesLeg2VenueCombo.currentIndex = leg2VenueIdx

                                        var tfIdx = detailedCandlesTimeframeCombo.indexOfValue(root.captureVm.detailedCandlesTimeframe)
                                        if (tfIdx >= 0 && detailedCandlesTimeframeCombo.currentIndex !== tfIdx)
                                            detailedCandlesTimeframeCombo.currentIndex = tfIdx

                                        var endIdx = detailedCandlesEndModeCombo.indexOfValue(root.captureVm.detailedCandlesEndMode)
                                        if (endIdx >= 0 && detailedCandlesEndModeCombo.currentIndex !== endIdx)
                                            detailedCandlesEndModeCombo.currentIndex = endIdx
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: root.captureVm.detailedCandlesRequestPreview
                                    color: root.mutedTextColor
                                    wrapMode: Text.WordWrap
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: root.captureVm.detailedCandlesLimitWarning
                                    color: root.mutedTextColor
                                    wrapMode: Text.WordWrap
                                }

                                CaptureAccentActionButton {
                                    text: root.captureVm.detailedCandlesMode === "basis_chain" ? "Download Chain" : "Download Candles2"
                                    accentColor: root.accentRequiredColor
                                    actionTextColor: "#071419"
                                    mutedTextColor: root.mutedTextColor
                                    enabled: root.captureVm.captureAvailable
                                    onClicked: root.captureVm.startDetailedCandles()
                                }
                            }
                        }

                        GridLayout {
                            id: venueGrid
                            Layout.fillWidth: true
                            columns: 3
                            columnSpacing: 12
                            rowSpacing: 12

                            function refreshVenueSymbolFields() {
                                for (var i = 0; i < venueRepeater.count; ++i) {
                                    var item = venueRepeater.itemAt(i)
                                    if (!item) continue
                                    item.symbolsEditor.text = root.captureVm.venueSymbolsText(item.modelData.key)
                                }
                            }

                            Repeater {
                                id: venueRepeater
                                model: root.captureVm.venueChoices
                                delegate: ColumnLayout {
                                    id: venueRow
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 220
                                    spacing: 6
                                    property alias symbolsEditor: symbolsArea

                                    CheckBox {
                                        Layout.fillWidth: true
                                        text: venueRow.modelData.label
                                        checked: root.captureVm.isVenueSelected(venueRow.modelData.key)
                                        onClicked: root.captureVm.toggleVenue(venueRow.modelData.key)
                                    }

                                    TextArea {
                                        id: symbolsArea
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: Math.max(56, contentHeight + topPadding + bottomPadding + 4)
                                        text: root.captureVm.venueSymbolsText(venueRow.modelData.key)
                                        placeholderText: root.captureVm.venueSymbolPlaceholder(venueRow.modelData.key)
                                        wrapMode: TextEdit.Wrap
                                        selectByMouse: true
                                        color: root.textColor
                                        placeholderTextColor: root.mutedTextColor
                                        selectedTextColor: root.textColor
                                        selectionColor: root.accentBuyColor
                                        onTextChanged: root.captureVm.setVenueSymbolsText(venueRow.modelData.key, text)
                                        background: Rectangle {
                                            radius: 8
                                            color: root.panelAltColor
                                            border.color: root.borderColor
                                            border.width: 1
                                        }
                                    }
                                }
                            }
                        }
                    }

                    CaptureAccentActionButton {
                        Layout.fillWidth: true
                        text: root.anyChannelRunning ? "Stop All" : "Start All"
                        accentColor: root.anyChannelRunning ? root.accentSellColor : root.accentRequiredColor
                        actionTextColor: root.anyChannelRunning ? "#fff4f5" : "#071419"
                        mutedTextColor: root.mutedTextColor
                        enabled: root.captureVm.captureAvailable
                        onClicked: {
                            if (root.anyChannelRunning) root.captureVm.stopAllChannels()
                            else root.captureVm.startAllChannels()
                        }
                    }

                    CaptureChannelCard {
                        captureVm: root.captureVm
                        channelKey: "trades"
                        titleText: "Trades Request"
                        emptyText: "No trade aliases available."
                        availableAliases: root.captureVm.tradesAvailableAliases
                        requestPreview: root.captureVm.tradesRequestPreview
                        weightSummary: root.captureVm.channelWeightSummary("trades")
                        running: root.captureVm.tradesRunning
                        actionText: root.captureVm.tradesRunning ? "Stop Trades" : "Start Trades"
                        panelColor: root.panelColor
                        panelAltColor: root.panelAltColor
                        borderColor: root.borderColor
                        textColor: root.textColor
                        mutedTextColor: root.mutedTextColor
                        accentRequiredColor: root.accentRequiredColor
                        accentOptionalColor: root.accentOptionalColor
                        accentSellColor: root.accentSellColor
                        actionAccentColor: root.captureVm.tradesRunning ? root.accentSellColor : root.accentRequiredColor
                        actionTextColor: root.captureVm.tradesRunning ? "#fff4f5" : "#071419"

                        actionVisible: true
                        onActionTriggered: root.captureVm.tradesRunning ? root.captureVm.stopTrades() : root.captureVm.startTrades()

                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Label {
                            Layout.fillWidth: true
                            text: "Trades warmup"
                            color: root.mutedTextColor
                        }

                        SpinBox {
                            id: tradesWarmupSpin
                            from: 0
                            to: 86400
                            stepSize: 300
                            value: root.captureVm.tradesHistoryWarmupSec
                            editable: true
                            enabled: !root.captureVm.tradesRunning
                            onValueModified: root.captureVm.tradesHistoryWarmupSec = value
                        }

                        Label {
                            text: "sec"
                            color: root.mutedTextColor
                        }

                        CaptureAccentActionButton {
                            Layout.preferredWidth: 140
                            text: "Fetch History"
                            accentColor: root.accentRequiredColor
                            actionTextColor: "#071419"
                            mutedTextColor: root.mutedTextColor
                            enabled: root.captureVm.captureAvailable && !root.captureVm.tradesRunning
                            onClicked: root.captureVm.startTradesHistory()
                        }
                    }

                    CaptureChannelCard {
                        captureVm: root.captureVm
                        channelKey: "liquidations"
                        titleText: "Liquidations Request"
                        emptyText: "No liquidation aliases available."
                        availableAliases: root.captureVm.liquidationsAvailableAliases
                        requestPreview: root.captureVm.liquidationsRequestPreview
                        weightSummary: root.captureVm.channelWeightSummary("liquidations")
                        running: root.captureVm.liquidationsRunning
                        actionText: root.captureVm.liquidationsRunning ? "Stop Liquidations" : "Start Liquidations"
                        panelColor: root.panelColor
                        panelAltColor: root.panelAltColor
                        borderColor: root.borderColor
                        textColor: root.textColor
                        mutedTextColor: root.mutedTextColor
                        accentRequiredColor: root.accentRequiredColor
                        accentOptionalColor: root.accentOptionalColor
                        accentSellColor: root.accentSellColor
                        actionAccentColor: root.captureVm.liquidationsRunning ? root.accentSellColor : root.accentRequiredColor
                        actionTextColor: root.captureVm.liquidationsRunning ? "#fff4f5" : "#071419"

                        actionVisible: true
                        onActionTriggered: root.captureVm.liquidationsRunning ? root.captureVm.stopLiquidations() : root.captureVm.startLiquidations()

                    }
                    CaptureChannelCard {
                        captureVm: root.captureVm
                        channelKey: "bookticker"
                        titleText: "BookTicker Request"
                        emptyText: "No book-ticker aliases available."
                        availableAliases: root.captureVm.bookTickerAvailableAliases
                        requestPreview: root.captureVm.bookTickerRequestPreview
                        weightSummary: root.captureVm.channelWeightSummary("bookticker")
                        running: root.captureVm.bookTickerRunning
                        actionText: root.captureVm.bookTickerRunning ? "Stop BookTicker" : "Start BookTicker"
                        panelColor: root.panelColor
                        panelAltColor: root.panelAltColor
                        borderColor: root.borderColor
                        textColor: root.textColor
                        mutedTextColor: root.mutedTextColor
                        accentRequiredColor: root.accentRequiredColor
                        accentOptionalColor: root.accentOptionalColor
                        accentSellColor: root.accentSellColor
                        actionAccentColor: root.captureVm.bookTickerRunning ? root.accentSellColor : root.accentRequiredColor
                        actionTextColor: root.captureVm.bookTickerRunning ? "#fff4f5" : "#071419"

                        actionVisible: true
                        onActionTriggered: root.captureVm.bookTickerRunning ? root.captureVm.stopBookTicker() : root.captureVm.startBookTicker()

                    }

                    CaptureChannelCard {
                        captureVm: root.captureVm
                        channelKey: "mark_price"
                        titleText: "MarkPrice"
                        emptyText: "CXET runtime stream: mark price."
                        availableAliases: []
                        requestPreview: "subscribe().object(mark_price) -> jsonl/mark_price.jsonl"
                        weightSummary: "Rows " + root.captureVm.markPriceCount
                        running: root.captureVm.markPriceRunning
                        actionText: root.captureVm.markPriceRunning ? "Stop MarkPrice" : "Start MarkPrice"
                        panelColor: root.panelColor
                        panelAltColor: root.panelAltColor
                        borderColor: root.borderColor
                        textColor: root.textColor
                        mutedTextColor: root.mutedTextColor
                        accentRequiredColor: root.accentRequiredColor
                        accentOptionalColor: root.accentOptionalColor
                        accentSellColor: root.accentSellColor
                        actionAccentColor: root.captureVm.markPriceRunning ? root.accentSellColor : root.accentRequiredColor
                        actionTextColor: root.captureVm.markPriceRunning ? "#fff4f5" : "#071419"
                        actionVisible: true
                        onActionTriggered: root.captureVm.markPriceRunning ? root.captureVm.stopMarkPrice() : root.captureVm.startMarkPrice()
                    }

                    CaptureChannelCard {
                        captureVm: root.captureVm
                        channelKey: "index_price"
                        titleText: "IndexPrice"
                        emptyText: "CXET runtime stream: index price."
                        availableAliases: []
                        requestPreview: "subscribe().object(index_price) -> jsonl/index_price.jsonl"
                        weightSummary: "Rows " + root.captureVm.indexPriceCount
                        running: root.captureVm.indexPriceRunning
                        actionText: root.captureVm.indexPriceRunning ? "Stop IndexPrice" : "Start IndexPrice"
                        panelColor: root.panelColor
                        panelAltColor: root.panelAltColor
                        borderColor: root.borderColor
                        textColor: root.textColor
                        mutedTextColor: root.mutedTextColor
                        accentRequiredColor: root.accentRequiredColor
                        accentOptionalColor: root.accentOptionalColor
                        accentSellColor: root.accentSellColor
                        actionAccentColor: root.captureVm.indexPriceRunning ? root.accentSellColor : root.accentRequiredColor
                        actionTextColor: root.captureVm.indexPriceRunning ? "#fff4f5" : "#071419"
                        actionVisible: true
                        onActionTriggered: root.captureVm.indexPriceRunning ? root.captureVm.stopIndexPrice() : root.captureVm.startIndexPrice()
                    }

                    CaptureChannelCard {
                        captureVm: root.captureVm
                        channelKey: "funding"
                        titleText: "Funding"
                        emptyText: "CXET funding stream/poller; writes only changed funding tuples."
                        availableAliases: []
                        requestPreview: "subscribe().object(funding) -> jsonl/funding.jsonl"
                        weightSummary: "Rows " + root.captureVm.fundingCount
                        running: root.captureVm.fundingRunning
                        actionText: root.captureVm.fundingRunning ? "Stop Funding" : "Start Funding"
                        panelColor: root.panelColor
                        panelAltColor: root.panelAltColor
                        borderColor: root.borderColor
                        textColor: root.textColor
                        mutedTextColor: root.mutedTextColor
                        accentRequiredColor: root.accentRequiredColor
                        accentOptionalColor: root.accentOptionalColor
                        accentSellColor: root.accentSellColor
                        actionAccentColor: root.captureVm.fundingRunning ? root.accentSellColor : root.accentRequiredColor
                        actionTextColor: root.captureVm.fundingRunning ? "#fff4f5" : "#071419"
                        actionVisible: true
                        onActionTriggered: root.captureVm.fundingRunning ? root.captureVm.stopFunding() : root.captureVm.startFunding()
                    }

                    CaptureChannelCard {
                        captureVm: root.captureVm
                        channelKey: "price_limit"
                        titleText: "PriceLimit"
                        emptyText: "CXET runtime stream: buy/sell limits."
                        availableAliases: []
                        requestPreview: "subscribe().object(price_limit) -> jsonl/price_limit.jsonl"
                        weightSummary: "Rows " + root.captureVm.priceLimitCount
                        running: root.captureVm.priceLimitRunning
                        actionText: root.captureVm.priceLimitRunning ? "Stop PriceLimit" : "Start PriceLimit"
                        panelColor: root.panelColor
                        panelAltColor: root.panelAltColor
                        borderColor: root.borderColor
                        textColor: root.textColor
                        mutedTextColor: root.mutedTextColor
                        accentRequiredColor: root.accentRequiredColor
                        accentOptionalColor: root.accentOptionalColor
                        accentSellColor: root.accentSellColor
                        actionAccentColor: root.captureVm.priceLimitRunning ? root.accentSellColor : root.accentRequiredColor
                        actionTextColor: root.captureVm.priceLimitRunning ? "#fff4f5" : "#071419"
                        actionVisible: true
                        onActionTriggered: root.captureVm.priceLimitRunning ? root.captureVm.stopPriceLimit() : root.captureVm.startPriceLimit()
                    }

                    CaptureChannelCard {
                        captureVm: root.captureVm
                        channelKey: "candles"
                        titleText: "Candles History"
                        emptyText: "Tiered REST candles: M1, M15, D1."
                        availableAliases: []
                        requestPreview: "subscribe().object(candles).tiered(M1,M15,D1) -> jsonl/candles.jsonl"
                        weightSummary: "Rows " + root.captureVm.candlesCount
                        running: false
                        actionText: "Start Candles"
                        panelColor: root.panelColor
                        panelAltColor: root.panelAltColor
                        borderColor: root.borderColor
                        textColor: root.textColor
                        mutedTextColor: root.mutedTextColor
                        accentRequiredColor: root.accentRequiredColor
                        accentOptionalColor: root.accentOptionalColor
                        accentSellColor: root.accentSellColor
                        actionAccentColor: root.accentRequiredColor
                        actionTextColor: "#071419"
                        actionVisible: true
                        onActionTriggered: root.captureVm.startCandles()
                    }

                    CaptureChannelCard {
                        captureVm: root.captureVm
                        channelKey: "orderbook"
                        titleText: "Orderbook Request"
                        emptyText: "No orderbook aliases available."
                        availableAliases: root.captureVm.orderbookAvailableAliases
                        requestPreview: root.captureVm.orderbookRequestPreview
                        weightSummary: root.captureVm.channelWeightSummary("orderbook")
                        running: root.captureVm.orderbookRunning
                        actionText: root.captureVm.orderbookRunning ? "Stop Orderbook" : "Start Orderbook"
                        panelColor: root.panelColor
                        panelAltColor: root.panelAltColor
                        borderColor: root.borderColor
                        textColor: root.textColor
                        mutedTextColor: root.mutedTextColor
                        accentRequiredColor: root.accentRequiredColor
                        accentOptionalColor: root.accentOptionalColor
                        accentSellColor: root.accentSellColor
                        actionAccentColor: root.captureVm.orderbookRunning ? root.accentSellColor : root.accentRequiredColor
                        actionTextColor: root.captureVm.orderbookRunning ? "#fff4f5" : "#071419"

                        actionVisible: true
                        onActionTriggered: root.captureVm.orderbookRunning ? root.captureVm.stopOrderbook() : root.captureVm.startOrderbook()

                    }
                    CaptureDarkButton {
                        text: "Finalize Session"
                        panelAltColor: root.panelAltColor
                        borderColor: root.borderColor
                        textColor: root.textColor
                        mutedTextColor: root.mutedTextColor
                        enabled: root.captureVm.captureAvailable
                        onClicked: root.captureVm.finalizeSession()
                    }

                    Label {
                        text: "Orderbook capture writes WS depth as depth_tape.jsonl plus depth_sidecar.jsonl; REST snapshot is optional."
                        color: root.mutedTextColor
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }
}
