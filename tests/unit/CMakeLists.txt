include(GoogleTest)

set(HFTREC_UNIT_TEST_SOURCES
    test_smoke.cpp
    test_capture_serializers.cpp
    test_corpus_loader.cpp
    test_identity_codec.cpp
    test_json_line_parser.cpp
    test_validation_runner.cpp
    test_book_state.cpp
    test_session_replay.cpp
    test_bookticker_exactness.cpp
    test_event_storage.cpp
)

if(HFTREC_WITH_CXET)
    list(APPEND HFTREC_UNIT_TEST_SOURCES
        test_capture_coordinator.cpp
        test_cxet_capture_bridge.cpp
        test_local_exchange_server.cpp
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
            ${CMAKE_SOURCE_DIR}/src/gui/viewer/ChartController.cpp
            ${CMAKE_SOURCE_DIR}/src/gui/viewer/ChartControllerSession.cpp
            ${CMAKE_SOURCE_DIR}/src/gui/viewer/ChartControllerViewport.cpp
            ${CMAKE_SOURCE_DIR}/src/gui/viewer/ChartControllerSelection.cpp
            ${CMAKE_SOURCE_DIR}/src/gui/viewer/LiveDataProvider.cpp
            ${CMAKE_SOURCE_DIR}/src/gui/viewer/hit_test/HoverDetection.cpp
            ${CMAKE_SOURCE_DIR}/src/gui/viewer/ChartController.hpp
    )
endif()

target_include_directories(hftrec_unit_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(hftrec_unit_tests
    PRIVATE
        hftrec_core
        hftrec_variant_orderbook_var01
        GTest::gtest
        GTest::gtest_main
)

if(Qt6_FOUND)
    target_link_libraries(hftrec_unit_tests
        PRIVATE
            Qt6::Core
            Qt6::Gui
    )
endif()

target_compile_features(hftrec_unit_tests PRIVATE cxx_std_23)
target_compile_definitions(hftrec_unit_tests PRIVATE HFTRREC_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

gtest_discover_tests(hftrec_unit_tests)