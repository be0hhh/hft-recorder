if(NOT Qt6_FOUND)
    message(STATUS "Qt6 not found: hft-recorder GUI target is disabled")
    return()
endif()

qt_add_executable(hft-recorder-gui
    src/App/Main.cpp
    ../Process/src/MetricsBootstrap.cpp
    src/Backtests/BacktestSessionSummary.cpp
    src/Models/RecordingCatalog.cpp
    src/Models/SessionListModel.cpp
    src/Models/ViewerSourceListModel.cpp
    src/Viewmodels/AppViewModel.cpp
    src/Backtests/BacktestExecutionConfigHelpers.cpp
    src/Backtests/BacktestResultHelpers.cpp
    src/Backtests/BacktestSessionHelpers.cpp
    src/Backtests/BacktestStrategyConfigHelpers.cpp
    src/Backtests/BacktestSweepHelpers.cpp
    src/Backtests/BacktestBatchAnalysisHelpers.cpp
    src/Backtests/BacktestBatchSweepHelpers.cpp
    src/Backtests/BacktestViewModel.cpp
    src/Backtests/BacktestViewModelBatch.cpp
    src/Backtests/BacktestViewModelConfig.cpp
    src/Backtests/BacktestViewModelLegSelection.cpp
    src/Backtests/BacktestViewModelResultRefresh.cpp
    src/Backtests/BacktestViewModelResults.cpp
    src/Viewmodels/CaptureViewModel.cpp
    src/Viewmodels/CaptureViewModelBasisChain.cpp
    src/Viewmodels/CaptureViewModelBasisChainHelpers.cpp
    src/Viewmodels/CaptureViewModelBatch.cpp
    src/Viewmodels/FinamCatalog.cpp
    src/Viewmodels/CaptureViewModelRequests.cpp
    src/Viewmodels/CaptureViewModelState.cpp
    src/Viewmodels/CompressionViewModel.cpp
    src/Viewmodels/CompressionViewModelHelpers.cpp
    src/Viewmodels/CompressionViewModelPreview.cpp
    src/Viewmodels/WorkspaceViewModel.cpp
    src/Viewer/ChartController.cpp
    src/Viewer/ChartControllerSession.cpp
    src/Viewer/ChartControllerViewport.cpp
    src/Viewer/ChartControllerSelection.cpp
    src/Api/ChartApiServer.cpp
    src/Viewer/LiveDataProvider.cpp
    src/Viewer/CompareLowerPane.cpp
    src/Viewer/StrategyOverlay.cpp
    src/Viewer/StrategyIndicator.cpp
    src/Viewer/StrategyIndicatorItem.cpp
    src/Viewer/RateLimitUsage.cpp
    src/Viewer/RateLimitGraphPainter.cpp
    src/Viewer/RateLimitStripItem.cpp
    src/Viewer/BookTickerCompareController.cpp
    src/Viewer/BookTickerCompareCandlePaint.cpp
    src/Viewer/BookTickerCompareItem.cpp
    src/Viewer/MoexBasisSeries.cpp
    src/Viewer/Moex/MoexBasisDataLoad.cpp
    src/Viewer/MoexBasisController.cpp
    src/Viewer/MoexBasisItem.cpp
    src/Viewer/ChartItem.cpp
    src/Viewer/ChartItemHover.cpp
    src/Viewer/ChartItemPaint.cpp
    src/Viewer/ChartItemBookTicker.cpp
    src/Viewer/Gpu/GpuChartItem.cpp
    src/Viewer/HitTest/HoverDetection.cpp
    src/Viewer/Renderers/BookRenderer.cpp
    src/Viewer/Renderers/BookTickerRenderer.cpp
    src/Viewer/Renderers/CandleRenderer.cpp
    src/Viewer/Renderers/TradeRenderer.cpp
    src/Viewer/Renderers/StrategyOverlayRenderer.cpp
    src/Viewer/Renderers/OverlayRenderer.cpp
)

target_include_directories(hft-recorder-gui
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/Runtime/include
        ${PROJECT_SOURCE_DIR}/src/Runtime/src
        ${PROJECT_SOURCE_DIR}/src/Process/src
        ${PROJECT_SOURCE_DIR}/src/Gui/src
)

set(HFTREC_QML_FILES
    src/qml/Main.qml
    src/qml/Components/CaptureDarkButton.qml
    src/qml/Components/CaptureAliasChip.qml
    src/qml/Components/CaptureWeightBadge.qml
    src/qml/Components/CaptureAccentActionButton.qml
    src/qml/Components/CaptureSessionSummaryCard.qml
    src/qml/Components/CaptureChannelCard.qml
    src/qml/Components/BacktestActionButton.qml
    src/qml/Components/BacktestCompactField.qml
    src/qml/Components/BacktestMessagePanel.qml
    src/qml/Components/BacktestMetricCard.qml
    src/qml/Components/BacktestStrategyParametersPanel.qml
    src/qml/Components/BacktestLegExecutionTable.qml
    src/qml/Components/BacktestLegSelector.qml
    src/qml/Components/BacktestPrimaryTradeControls.qml
    src/qml/Components/QuantContextStrip.qml
    src/qml/Components/QuantResultsPanel.qml
    src/qml/Components/RecorderComboBox.qml
    src/qml/Components/SessionPickerCombo.qml
    src/qml/Components/ViewerChromeButton.qml
    src/qml/Components/ViewerChannelButton.qml
    src/qml/Components/ViewerInteractionState.qml
    src/qml/Components/ViewerSessionToolbar.qml
    src/qml/Components/ViewerLayerToolbar.qml
    src/qml/Components/ViewerSelectionOverlay.qml
    src/qml/Components/ViewerPriceScale.qml
    src/qml/Components/ViewerTimeScale.qml
    src/qml/Components/WorkspaceHost.qml
    src/qml/Components/WorkspaceFloatingWindow.qml
    src/qml/Views/CaptureView.qml
    src/qml/Views/CompressView.qml
    src/qml/Views/SessionsView.qml
    src/qml/Views/ValidationView.qml
    src/qml/Views/LabView.qml
    src/qml/Views/DashboardView.qml
    src/qml/Views/ViewerView.qml
    src/qml/Views/MoexBasisView.qml
    src/qml/Views/BacktestResultsView.qml
    src/qml/Views/QuantView.qml
    src/qml/Views/ResultsView.qml
)

foreach(_hftrec_qml_file IN LISTS HFTREC_QML_FILES)
    string(REGEX REPLACE "^src/" "" _hftrec_qml_alias "${_hftrec_qml_file}")
    set_source_files_properties("${_hftrec_qml_file}" PROPERTIES
        QT_RESOURCE_ALIAS "${_hftrec_qml_alias}")
endforeach()

qt_add_qml_module(hft-recorder-gui
    URI HftRecorder
    VERSION 1.0
    QML_FILES ${HFTREC_QML_FILES}
)

set_source_files_properties(src/Data/FinamAssets.json PROPERTIES
    QT_RESOURCE_ALIAS data/finam_assets.json)

qt_add_resources(hft-recorder-gui hft_recorder_data
    PREFIX "/HftRecorder"
    FILES
        src/Data/FinamAssets.json
)

target_link_libraries(hft-recorder-gui
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::QuickDialogs2
        Qt6::Network
        hftrec_core
        hftrec_support
        hft_compressor_core
        hft_backtest_core
        ${CMAKE_DL_LIBS}
)

target_compile_features(hft-recorder-gui PRIVATE cxx_std_23)
target_compile_definitions(hft-recorder-gui
    PRIVATE
        _GNU_SOURCE
        HFTRREC_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        HFT_COMPRESSOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}/../hft-compressor"
        HFT_BACKTEST_TRADER_SOURCE_DIR="${PROJECT_SOURCE_DIR}/../hft-trader"
)

if(WIN32 AND HFT_COMPRESSOR_RUNTIME_LIB)
    add_custom_command(TARGET hft-recorder-gui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${HFT_COMPRESSOR_RUNTIME_LIB}"
            "$<TARGET_FILE_DIR:hft-recorder-gui>"
    )
endif()



