if(NOT Qt6_FOUND)
    message(STATUS "Qt6 not found: hft-recorder GUI target is disabled")
    return()
endif()

qt_add_executable(hft-recorder-gui
    app/main.cpp
    ../app/metrics_bootstrap.cpp
    models/BacktestSessionSummary.cpp
    models/SessionListModel.cpp
    models/ViewerSourceListModel.cpp
    viewmodels/AppViewModel.cpp
    viewmodels/BacktestViewModel.cpp
    viewmodels/CaptureViewModel.cpp
    viewmodels/CaptureViewModelBatch.cpp
    viewmodels/CaptureViewModelRequests.cpp
    viewmodels/CaptureViewModelState.cpp
    viewmodels/CompressionViewModel.cpp
    viewmodels/WorkspaceViewModel.cpp
    viewer/ChartController.cpp
    viewer/ChartControllerSession.cpp
    viewer/ChartControllerViewport.cpp
    viewer/ChartControllerSelection.cpp
    api/ChartApiServer.cpp
    viewer/LiveDataProvider.cpp
    viewer/StrategyOverlay.cpp
    viewer/StrategyIndicator.cpp
    viewer/StrategyIndicatorItem.cpp
    viewer/BookTickerCompareController.cpp
    viewer/BookTickerCompareItem.cpp
    viewer/ChartItem.cpp
    viewer/ChartItemHover.cpp
    viewer/ChartItemPaint.cpp
    viewer/ChartItemBookTicker.cpp
    viewer/gpu/GpuChartItem.cpp
    viewer/hit_test/HoverDetection.cpp
    viewer/renderers/BookRenderer.cpp
    viewer/renderers/BookTickerRenderer.cpp
    viewer/renderers/CandleRenderer.cpp
    viewer/renderers/TradeRenderer.cpp
    viewer/renderers/StrategyOverlayRenderer.cpp
    viewer/renderers/OverlayRenderer.cpp
)

target_include_directories(hft-recorder-gui
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/src
)

qt_add_qml_module(hft-recorder-gui
    URI HftRecorder
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/components/CaptureDarkButton.qml
        qml/components/CaptureAliasChip.qml
        qml/components/CaptureWeightBadge.qml
        qml/components/CaptureAccentActionButton.qml
        qml/components/CaptureSessionSummaryCard.qml
        qml/components/CaptureChannelCard.qml
        qml/components/RecorderComboBox.qml
        qml/components/ViewerChromeButton.qml
        qml/components/ViewerChannelButton.qml
        qml/components/ViewerInteractionState.qml
        qml/components/ViewerSessionToolbar.qml
        qml/components/ViewerLayerToolbar.qml
        qml/components/ViewerSelectionOverlay.qml
        qml/components/ViewerPriceScale.qml
        qml/components/ViewerTimeScale.qml
        qml/components/WorkspaceHost.qml
        qml/components/WorkspaceFloatingWindow.qml
        qml/views/CaptureView.qml
        qml/views/CompressView.qml
        qml/views/SessionsView.qml
        qml/views/ValidationView.qml
        qml/views/LabView.qml
        qml/views/DashboardView.qml
        qml/views/ViewerView.qml
        qml/views/BacktestResultsView.qml
        qml/views/ResultsView.qml
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
)

target_compile_features(hft-recorder-gui PRIVATE cxx_std_23)
target_compile_definitions(hft-recorder-gui
    PRIVATE
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



