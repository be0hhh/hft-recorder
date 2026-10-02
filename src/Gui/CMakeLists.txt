if(NOT Qt6_FOUND)
    message(STATUS "Qt6 not found: hft-recorder GUI target is disabled")
    return()
endif()

qt_add_executable(hft-recorder-gui
    src/App/Main.cpp
    src/Backtests/Sessions/BacktestSessionSummary.cpp
    src/Models/RecordingCatalog.cpp
    src/Models/SessionListModel.cpp
    src/Models/ViewerSourceListModel.cpp
    src/Viewmodels/App/AppViewModel.cpp
    src/Backtests/Config/BacktestExecutionConfigHelpers.cpp
    src/Backtests/Results/BacktestResultHelpers.cpp
    src/Backtests/Sessions/BacktestSessionHelpers.cpp
    src/Backtests/Config/BacktestStrategyConfigHelpers.cpp
    src/Backtests/Batch/BacktestSweepHelpers.cpp
    src/Backtests/Batch/BacktestBatchAnalysisHelpers.cpp
    src/Backtests/Batch/BacktestBatchSweepHelpers.cpp
    src/Backtests/Viewmodel/BacktestViewModel.cpp
    src/Backtests/Viewmodel/BacktestViewModelBatch.cpp
    src/Backtests/Viewmodel/Config/BacktestViewModelConfig.cpp
    src/Backtests/Viewmodel/Config/BacktestViewModelPersistence.cpp
    src/Backtests/Viewmodel/Config/BacktestViewModelRunConfig.cpp
    src/Backtests/Viewmodel/BacktestViewModelLegSelection.cpp
    src/Backtests/Viewmodel/BacktestViewModelResultRefresh.cpp
    src/Backtests/Viewmodel/BacktestViewModelResults.cpp
    src/Viewmodels/Capture/CaptureViewModel.cpp
    src/Viewmodels/Capture/CaptureViewModelBasisChain.cpp
    src/Viewmodels/Capture/CaptureViewModelBasisChainHelpers.cpp
    src/Viewmodels/Capture/CaptureViewModelBatch.cpp
    src/Viewmodels/Capture/FinamCatalog.cpp
    src/Viewmodels/Capture/CaptureViewModelRequests.cpp
    src/Viewmodels/Capture/CaptureViewModelState.cpp
    src/Viewmodels/Compression/CompressionViewModel.cpp
    src/Viewmodels/Compression/CompressionViewModelSelection.cpp
    src/Viewmodels/Compression/CompressionViewModelRuns.cpp
    src/Viewmodels/Compression/CompressionViewModelVerification.cpp
    src/Viewmodels/Compression/CompressionViewModelArtifacts.cpp
    src/Viewmodels/Compression/CompressionViewModelMetrics.cpp
    src/Viewmodels/Compression/CompressionViewModelHelpers.cpp
    src/Viewmodels/Compression/CompressionViewModelPreview.cpp
    src/Viewmodels/App/WorkspaceViewModel.cpp
    src/Viewer/Chart/Controller/ChartController.cpp
    src/Viewer/Chart/Controller/ChartControllerSession.cpp
    src/Viewer/Chart/Controller/ChartControllerViewport.cpp
    src/Viewer/Chart/Controller/ChartControllerSelection.cpp
    src/Api/ChartApiServer.cpp
    src/Viewer/Chart/Controller/LiveDataProvider.cpp
    src/Viewer/Compare/CompareLowerPane.cpp
    src/Viewer/Strategy/StrategyOverlay.cpp
    src/Viewer/Strategy/StrategyIndicator.cpp
    src/Viewer/Strategy/StrategyIndicatorItem.cpp
    src/Viewer/RateLimit/RateLimitUsage.cpp
    src/Viewer/RateLimit/RateLimitGraphPainter.cpp
    src/Viewer/RateLimit/RateLimitStripItem.cpp
    src/Viewer/Compare/BookTickerCompareController.cpp
    src/Viewer/Compare/BookTickerCompareCandlePaint.cpp
    src/Viewer/Compare/BookTickerCompareItem.cpp
    src/Viewer/Compare/BookTickerCompareRanges.cpp
    src/Viewer/Compare/BookTickerCompareLayout.cpp
    src/Viewer/Compare/BookTickerComparePaint.cpp
    src/Viewer/Compare/BookTickerCompareInteraction.cpp
    src/Viewer/Chart/Moex/MoexBasisSeries.cpp
    src/Viewer/Chart/Moex/MoexBasisDataLoad.cpp
    src/Viewer/Chart/Moex/MoexBasisController.cpp
    src/Viewer/Chart/Moex/MoexBasisItem.cpp
    src/Viewer/Chart/Item/ChartItem.cpp
    src/Viewer/Chart/Item/ChartItemHover.cpp
    src/Viewer/Chart/Item/ChartItemPaint.cpp
    src/Viewer/Chart/Item/ChartItemBookTicker.cpp
    src/Viewer/Chart/Gpu/GpuChartItem.cpp
    src/Viewer/Chart/HitTest/HoverDetection.cpp
    src/Viewer/Chart/Renderers/BookRenderer.cpp
    src/Viewer/Chart/Renderers/BookTickerRenderer.cpp
    src/Viewer/Chart/Renderers/CandleRenderer.cpp
    src/Viewer/Chart/Renderers/TradeRenderer.cpp
    src/Viewer/Strategy/Renderers/StrategyOverlayRenderer.cpp
    src/Viewer/Chart/Renderers/OverlayRenderer.cpp
)

target_include_directories(hft-recorder-gui
    PRIVATE
        ${HFTREC_SOURCE_DIR}/src/Runtime/include
        ${HFTREC_SOURCE_DIR}/src/Runtime/src
        ${HFTREC_SOURCE_DIR}/src/Process/src
        ${HFTREC_SOURCE_DIR}/src/Gui/src
)

set(HFTREC_QML_FILES
    src/qml/Main.qml
    src/qml/Components/Capture/CaptureDarkButton.qml
    src/qml/Components/Capture/CaptureAliasChip.qml
    src/qml/Components/Capture/CaptureWeightBadge.qml
    src/qml/Components/Capture/CaptureAccentActionButton.qml
    src/qml/Components/Capture/CaptureSessionSummaryCard.qml
    src/qml/Components/Capture/CaptureChannelCard.qml
    src/qml/Components/Backtest/BacktestActionButton.qml
    src/qml/Components/Backtest/BacktestCompactField.qml
    src/qml/Components/Backtest/BacktestMessagePanel.qml
    src/qml/Components/Backtest/BacktestMetricCard.qml
    src/qml/Components/Backtest/BacktestStrategyParametersPanel.qml
    src/qml/Components/Backtest/BacktestLegExecutionTable.qml
    src/qml/Components/Backtest/BacktestLegSelector.qml
    src/qml/Components/Backtest/BacktestPrimaryTradeControls.qml
    src/qml/Components/Common/QuantContextStrip.qml
    src/qml/Components/Common/QuantResultsPanel.qml
    src/qml/Components/Common/RecorderComboBox.qml
    src/qml/Components/Common/SessionPickerCombo.qml
    src/qml/Components/Viewer/ViewerChromeButton.qml
    src/qml/Components/Viewer/ViewerBacktestPicker.qml
    src/qml/Components/Viewer/ViewerChannelButton.qml
    src/qml/Components/Viewer/ViewerInteractionState.qml
    src/qml/Components/Viewer/ViewerSessionToolbar.qml
    src/qml/Components/Viewer/ViewerLayerToolbar.qml
    src/qml/Components/Viewer/ViewerSelectionOverlay.qml
    src/qml/Components/Viewer/ViewerPriceScale.qml
    src/qml/Components/Viewer/ViewerTimeScale.qml
    src/qml/Components/Workspace/WorkspaceHost.qml
    src/qml/Components/Workspace/WorkspaceFloatingWindow.qml
    src/qml/Views/CaptureView.qml
    src/qml/Views/CompressView.qml
    src/qml/Views/SessionsView.qml
    src/qml/Views/ValidationView.qml
    src/qml/Views/LabView.qml
    src/qml/Views/DashboardView.qml
    src/qml/Views/ViewerView.qml
    src/qml/Views/MoexBasisView.qml
    src/qml/Views/BacktestResultsView.qml
    src/qml/Components/Backtest/BacktestResultsToolbar.qml
    src/qml/Components/Backtest/BacktestResultsRunList.qml
    src/qml/Components/Backtest/BacktestResultsSummary.qml
    src/qml/Components/Backtest/BacktestResultsMetricChart.qml
    src/qml/Components/Backtest/BacktestResultsSweep.qml
    src/qml/Components/Backtest/BacktestExecutionEdgeChart.qml
    src/qml/Views/QuantView.qml
    src/qml/Views/ResultsView.qml
)

foreach(_hftrec_qml_file IN LISTS HFTREC_QML_FILES)
    string(REGEX REPLACE "^src/" "" _hftrec_qml_alias "${_hftrec_qml_file}")
    if(_hftrec_qml_file MATCHES "^src/qml/Components/")
        get_filename_component(_hftrec_qml_name "${_hftrec_qml_file}" NAME)
        set(_hftrec_qml_alias "qml/Components/${_hftrec_qml_name}")
    endif()
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
        hft_compressor::core
        hft_backtest::core
        ${CMAKE_DL_LIBS}
)

target_compile_features(hft-recorder-gui PRIVATE cxx_std_23)
target_compile_definitions(hft-recorder-gui
    PRIVATE
        _GNU_SOURCE
        HFTRREC_SOURCE_DIR="${HFTREC_SOURCE_DIR}"
        HFT_COMPRESSOR_SOURCE_DIR="${HFTREC_SOURCE_DIR}/../hft-compressor"
        HFT_BACKTEST_TRADER_SOURCE_DIR="${HFTREC_SOURCE_DIR}/../hft-trader"
)

