include(GoogleTest)

set(HFTREC_UNIT_TEST_SOURCES
    SmokeTests.cpp
    CaptureSerializersTests.cpp
    Candles2BulkStateTests.cpp
    CaptureCliTests.cpp
    HistoryBinanceVisionTests.cpp
    CorpusLoaderTests.cpp
    IdentityCodecTests.cpp
    JsonLineParserTests.cpp
    ValidationRunnerTests.cpp
    RecorderApiTests.cpp
    RecordingDiscoveryTests.cpp
    BookStateTests.cpp
    SessionReplayTests.cpp
    BooktickerExactnessTests.cpp
    BooktickerSpreadTests.cpp
    CandleSpreadTests.cpp
    EventStorageTests.cpp
    Finam/FinamEnvSyncTests.cpp
    CaptureChannelSupportTests.cpp
    TuiLaunchTests.cpp
    TuiPresetTests.cpp
    TuiShardTests.cpp
    TuiShardStatusTests.cpp
    TuiSymbolsTests.cpp
    TuiTerminalRenderTests.cpp
)

if(HFTREC_WITH_CXET)
    list(APPEND HFTREC_UNIT_TEST_SOURCES
        CaptureCoordinatorTests.cpp
        CxetCaptureBridgeTests.cpp
    )
endif()

add_executable(hftrec_unit_tests ${HFTREC_UNIT_TEST_SOURCES})

if(Qt6_FOUND)
    target_sources(hftrec_unit_tests
        PRIVATE
            ${PROJECT_SOURCE_DIR}/src/Gui/src/Models/RecordingCatalog.cpp
            ViewerOrderbookDepthWindowTests.cpp
            ViewerBooktickerTraceTests.cpp
            ChartMarkersTests.cpp
            WorkspaceViewmodelTests.cpp
            MoexBasisSeriesTests.cpp
            Backtests/BacktestBatchAnalysisHelpersTests.cpp
            Backtests/BacktestBatchSweepHelpersTests.cpp
            Backtests/BacktestLegSelectionTests.cpp
            Backtests/BacktestViewmodelTests.cpp
            Backtests/BacktestViewmodelHelpersTests.cpp
            RecordedRenderOnceTests.cpp
            CaptureViewmodelRequestsTests.cpp
            BasisChainManifestTests.cpp
            StrategyOverlayTests.cpp
            StrategyIndicatorTests.cpp
            RateLimitUsageTests.cpp
            ${PROJECT_SOURCE_DIR}/src/Gui/src/Viewer/ChartController.cpp
            ${PROJECT_SOURCE_DIR}/src/Gui/src/Viewer/ChartControllerSession.cpp
            ${PROJECT_SOURCE_DIR}/src/Gui/src/Viewer/ChartControllerViewport.cpp
            ${PROJECT_SOURCE_DIR}/src/Gui/src/Viewer/ChartControllerSelection.cpp
            ${PROJECT_SOURCE_DIR}/src/Gui/src/Viewer/LiveDataProvider.cpp
            ${PROJECT_SOURCE_DIR}/src/Gui/src/Viewer/CompareLowerPane.cpp
            ${PROJECT_SOURCE_DIR}/src/Gui/src/Viewer/StrategyOverlay.cpp
            ${PROJECT_SOURCE_DIR}/src/Gui/src/Viewer/StrategyIndicator.cpp
            ${PROJECT_SOURCE_DIR}/src/Gui/src/Viewer/RateLimitUsage.cpp
            ${PROJECT_SOURCE_DIR}/src/Gui/src/Viewer/BookTickerCompareController.cpp
            ${PROJECT_SOURCE_DIR}/src/Gui/src/Viewer/MoexBasisSeries.cpp
            ${PROJECT_SOURCE_DIR}/src/Gui/src/Viewer/HitTest/HoverDetection.cpp
            ${PROJECT_SOURCE_DIR}/src/Gui/src/Viewer/ChartController.hpp
            ${PROJECT_SOURCE_DIR}/src/Gui/src/Viewmodels/CaptureViewModelRequests.cpp
            ${PROJECT_SOURCE_DIR}/src/Gui/src/Viewmodels/CaptureViewModelBasisChainHelpers.cpp
            ${PROJECT_SOURCE_DIR}/src/Gui/src/Viewmodels/FinamCatalog.cpp
            ${PROJECT_SOURCE_DIR}/src/Gui/src/Viewmodels/WorkspaceViewModel.cpp
            ${PROJECT_SOURCE_DIR}/src/Gui/src/Backtests/BacktestExecutionConfigHelpers.cpp
            ${PROJECT_SOURCE_DIR}/src/Gui/src/Backtests/BacktestBatchAnalysisHelpers.cpp
            ${PROJECT_SOURCE_DIR}/src/Gui/src/Backtests/BacktestBatchSweepHelpers.cpp
            ${PROJECT_SOURCE_DIR}/src/Gui/src/Backtests/BacktestResultHelpers.cpp
            ${PROJECT_SOURCE_DIR}/src/Gui/src/Backtests/BacktestSessionHelpers.cpp
            ${PROJECT_SOURCE_DIR}/src/Gui/src/Backtests/BacktestStrategyConfigHelpers.cpp
            ${PROJECT_SOURCE_DIR}/src/Gui/src/Backtests/BacktestSweepHelpers.cpp
            ${PROJECT_SOURCE_DIR}/src/Gui/src/Backtests/BacktestViewModel.cpp
            ${PROJECT_SOURCE_DIR}/src/Gui/src/Backtests/BacktestViewModelBatch.cpp
            ${PROJECT_SOURCE_DIR}/src/Gui/src/Backtests/BacktestViewModelConfig.cpp
            ${PROJECT_SOURCE_DIR}/src/Gui/src/Backtests/BacktestViewModelLegSelection.cpp
            ${PROJECT_SOURCE_DIR}/src/Gui/src/Backtests/BacktestViewModelResultRefresh.cpp
            ${PROJECT_SOURCE_DIR}/src/Gui/src/Backtests/BacktestViewModelResults.cpp
            ${PROJECT_SOURCE_DIR}/src/Gui/src/Backtests/BacktestSessionSummary.cpp
    )
endif()

target_include_directories(hftrec_unit_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/Runtime/include
        ${PROJECT_SOURCE_DIR}/src/Runtime/src
        ${PROJECT_SOURCE_DIR}/src/Process/src
        ${PROJECT_SOURCE_DIR}/src/Gui/src
        ${PROJECT_SOURCE_DIR}/src/Lab/Variants/OrderBook/Var01RawUpdatesCpp/src
)

target_link_libraries(hftrec_unit_tests
    PRIVATE
        hftrec_recorder_api
        hftrec_variant_orderbook_var01
        GTest::gtest
        GTest::gtest_main
)

if(Qt6_FOUND)
    target_link_libraries(hftrec_unit_tests
        PRIVATE
            Qt6::Core
            Qt6::Gui
            Qt6::Quick
            Qt6::Network
            hft_backtest_core
            ${CMAKE_DL_LIBS}
    )
    target_compile_definitions(hftrec_unit_tests PRIVATE HFT_BACKTEST_TRADER_SOURCE_DIR="${PROJECT_SOURCE_DIR}/../hft-trader")
endif()

target_compile_features(hftrec_unit_tests PRIVATE cxx_std_23)
target_compile_definitions(hftrec_unit_tests PRIVATE _GNU_SOURCE HFTRREC_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

gtest_discover_tests(hftrec_unit_tests)
