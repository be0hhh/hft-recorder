include(GoogleTest)

set(HFTREC_UNIT_TEST_SOURCES
    test_smoke.cpp
    test_capture_serializers.cpp
    test_corpus_loader.cpp
    test_identity_codec.cpp
    test_json_line_parser.cpp
    test_validation_runner.cpp
    test_recorder_api.cpp
    test_recording_discovery.cpp
    test_backtest_runner.cpp
    test_book_state.cpp
    test_session_replay.cpp
    test_bookticker_exactness.cpp
    test_bookticker_spread.cpp
    test_candle_spread.cpp
    test_event_storage.cpp
    test_tui_preset.cpp
)

if(HFTREC_WITH_CXET)
    list(APPEND HFTREC_UNIT_TEST_SOURCES
        test_capture_coordinator.cpp
        test_cxet_capture_bridge.cpp
        test_local_order_engine.cpp
    )
endif()

add_executable(hftrec_unit_tests ${HFTREC_UNIT_TEST_SOURCES})

if(Qt6_FOUND)
    target_sources(hftrec_unit_tests
        PRIVATE
            test_viewer_orderbook_depth_window.cpp
            test_viewer_bookticker_trace.cpp
            test_chart_markers.cpp
            test_workspace_viewmodel.cpp
            test_backtest_viewmodel.cpp
            test_recorded_render_once.cpp
            test_capture_viewmodel_requests.cpp
            test_strategy_overlay.cpp
            test_strategy_indicator.cpp
            ${PROJECT_SOURCE_DIR}/src/gui/viewer/ChartController.cpp
            ${PROJECT_SOURCE_DIR}/src/gui/viewer/ChartControllerSession.cpp
            ${PROJECT_SOURCE_DIR}/src/gui/viewer/ChartControllerViewport.cpp
            ${PROJECT_SOURCE_DIR}/src/gui/viewer/ChartControllerSelection.cpp
            ${PROJECT_SOURCE_DIR}/src/gui/viewer/LiveDataProvider.cpp
            ${PROJECT_SOURCE_DIR}/src/gui/viewer/CompareLowerPane.cpp
            ${PROJECT_SOURCE_DIR}/src/gui/viewer/StrategyOverlay.cpp
            ${PROJECT_SOURCE_DIR}/src/gui/viewer/StrategyIndicator.cpp
            ${PROJECT_SOURCE_DIR}/src/gui/viewer/BookTickerCompareController.cpp
            ${PROJECT_SOURCE_DIR}/src/gui/viewer/hit_test/HoverDetection.cpp
            ${PROJECT_SOURCE_DIR}/src/gui/viewer/ChartController.hpp
            ${PROJECT_SOURCE_DIR}/src/gui/viewmodels/CaptureViewModelRequests.cpp
            ${PROJECT_SOURCE_DIR}/src/gui/viewmodels/WorkspaceViewModel.cpp
            ${PROJECT_SOURCE_DIR}/src/gui/viewmodels/BacktestViewModel.cpp
            ${PROJECT_SOURCE_DIR}/src/gui/models/BacktestSessionSummary.cpp
    )
endif()

if(Qt6_FOUND AND HFTREC_WITH_CXET)
    target_sources(hftrec_unit_tests
        PRIVATE
            test_local_venue_ws_order_parser.cpp
            ${PROJECT_SOURCE_DIR}/src/gui/api/LocalVenueWsServer.cpp
    )
endif()

target_include_directories(hftrec_unit_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/src
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
    )
    target_compile_definitions(hftrec_unit_tests PRIVATE HFT_BACKTEST_TRADER_SOURCE_DIR="${PROJECT_SOURCE_DIR}/../hft-trader")
endif()

target_compile_features(hftrec_unit_tests PRIVATE cxx_std_23)
target_compile_definitions(hftrec_unit_tests PRIVATE HFTRREC_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

gtest_discover_tests(hftrec_unit_tests)
